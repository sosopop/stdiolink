#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from collections import deque
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[2]
EXE_SUFFIX = ".exe" if os.name == "nt" else ""


@dataclass
class CaseResult:
    name: str
    status: str
    detail: str


class QuietThreadingHTTPServer(ThreadingHTTPServer):
    def handle_error(self, request, client_address) -> None:  # type: ignore[override]
        exc_type, _, _ = sys.exc_info()
        if exc_type in (BrokenPipeError, ConnectionResetError):
            return
        super().handle_error(request, client_address)


def candidate_bin_dirs() -> list[Path]:
    dirs: list[Path] = []
    env_bin = os.environ.get("STDIOLINK_BIN_DIR")
    if env_bin:
        dirs.append(Path(env_bin))
    dirs.append(ROOT_DIR / "build" / "runtime_release" / "bin")
    return dirs


def candidate_data_roots() -> list[Path]:
    env_data_root = os.environ.get("STDIOLINK_DATA_ROOT")
    dirs: list[Path] = []
    if env_data_root:
        dirs.append(Path(env_data_root))
    dirs.append(ROOT_DIR / "build" / "runtime_release" / "data_root")
    return dirs


def candidate_runtime_roots() -> list[Path]:
    roots: list[Path] = []
    env_bin = os.environ.get("STDIOLINK_BIN_DIR")
    if env_bin:
        roots.append(Path(env_bin).resolve().parent)
    roots.append((ROOT_DIR / "build" / "runtime_release").resolve())
    return roots


def make_env() -> dict[str, str]:
    env = os.environ.copy()
    extra = os.pathsep.join(str(d) for d in candidate_bin_dirs() if d.exists())
    if extra:
        env["PATH"] = extra + os.pathsep + env.get("PATH", "")
    return env


def find_executable(base_name: str) -> Path | None:
    file_name = f"{base_name}{EXE_SUFFIX}"
    for directory in candidate_bin_dirs():
        candidate = directory / file_name
        if candidate.exists():
            return candidate
    return None


def find_driver_executable(base_name: str) -> Path | None:
    file_name = f"{base_name}{EXE_SUFFIX}"
    for runtime_root in candidate_runtime_roots():
        candidate = runtime_root / "data_root" / "drivers" / base_name / file_name
        if candidate.exists():
            return candidate
    return None


def find_driver_dir(driver_name: str) -> Path | None:
    for data_root in candidate_data_roots():
        candidate = data_root / "drivers" / driver_name
        if candidate.exists():
            return candidate
    return None


def format_missing(name: str) -> str:
    candidates = "\n".join(str(d / f"{name}{EXE_SUFFIX}") for d in candidate_bin_dirs())
    return f"missing executable: {name}{EXE_SUFFIX}\ncandidates:\n{candidates}"


def format_missing_driver_executable(name: str) -> str:
    file_name = f"{name}{EXE_SUFFIX}"
    candidates = "\n".join(
        str(root / "data_root" / "drivers" / name / file_name) for root in candidate_runtime_roots()
    )
    return f"missing driver executable: {file_name}\ncandidates:\n{candidates}"


def format_missing_driver(name: str) -> str:
    candidates = "\n".join(str(d / "drivers" / name) for d in candidate_data_roots())
    return f"missing driver dir: {name}\ncandidates:\n{candidates}"


def allocate_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def http_request(method: str, url: str, body: dict | None = None, timeout_s: float = 3.0) -> tuple[int, str]:
    data = None
    headers = {}
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout_s) as resp:
            return resp.status, resp.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read().decode("utf-8", errors="replace")


def wait_http_ready(base_url: str, timeout_s: float = 10.0) -> bool:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            status, _ = http_request("GET", f"{base_url}/api/projects", timeout_s=1.0)
            if status == 200:
                return True
        except Exception:
            pass
        time.sleep(0.1)
    return False


def wait_for_log(path: Path, needle: str, timeout_s: float) -> tuple[bool, str]:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        if needle in text:
            return True, text
        time.sleep(0.1)
    text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
    return needle in text, text


def wait_runtime(base_url: str, project_id: str, predicate, timeout_s: float) -> tuple[bool, dict | None]:
    deadline = time.time() + timeout_s
    last_obj: dict | None = None
    while time.time() < deadline:
        status, body = http_request("GET", f"{base_url}/api/projects/{project_id}/runtime", timeout_s=1.0)
        if status == 200:
            try:
                last_obj = json.loads(body)
            except json.JSONDecodeError:
                last_obj = None
            if isinstance(last_obj, dict) and predicate(last_obj):
                return True, last_obj
        time.sleep(0.1)
    return False, last_obj


def stop_process(proc: subprocess.Popen[str]) -> tuple[str, str]:
    proc.terminate()
    try:
        return proc.communicate(timeout=3.0)
    except subprocess.TimeoutExpired:
        proc.kill()
        return proc.communicate(timeout=3.0)


def copytree(src: Path, dst: Path) -> None:
    shutil.copytree(src, dst, dirs_exist_ok=True)


def write_json(path: Path, obj: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2), encoding="utf-8")


def write_project(data_root: Path, project_id: str, config_obj: dict, param_obj: dict) -> None:
    project_dir = data_root / "projects" / project_id
    write_json(project_dir / "config.json", config_obj)
    write_json(project_dir / "param.json", param_obj)


def cleanup_dir(path: Path) -> None:
    for _ in range(20):
        try:
            shutil.rmtree(path)
            return
        except FileNotFoundError:
            return
        except PermissionError:
            time.sleep(0.25)


class FakeVisionServer:
    def __init__(self) -> None:
        self.login_queue: deque[tuple[str, object]] = deque()
        self.scan_queue: deque[tuple[str, object]] = deque()
        self.last_log_queue: deque[tuple[str, object]] = deque()
        self.scan_call_count = 0
        self.last_log_call_count = 0
        self.login_call_count = 0
        self._lock = threading.Lock()
        self._ws_clients: dict[socket.socket, set[str]] = {}
        self._server = QuietThreadingHTTPServer(("127.0.0.1", 0), self._make_handler())
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()

    def close(self) -> None:
        with self._lock:
            clients = list(self._ws_clients.keys())
            self._ws_clients.clear()
        for conn in clients:
            try:
                conn.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                conn.close()
            except OSError:
                pass
        self._server.shutdown()
        self._server.server_close()
        self._thread.join(timeout=2.0)

    @property
    def addr(self) -> str:
        return f"127.0.0.1:{self._server.server_port}"

    def enqueue_login_done(self, token: str) -> None:
        self.login_queue.append(("done", {"token": token, "role": 0}))

    def enqueue_scan_done(self) -> None:
        self.scan_queue.append(("done", {"accepted": True}))

    def enqueue_scan_error(self, message: str) -> None:
        self.scan_queue.append(("error", message))

    def enqueue_last_log_newer_than_now(self) -> None:
        self.last_log_queue.append(("fresh", None))

    def enqueue_last_log_older_than_now(self) -> None:
        self.last_log_queue.append(("stale", None))

    def _make_handler(self):
        parent = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def do_GET(self) -> None:  # noqa: N802
                if self.path != "/ws":
                    self.send_response(501)
                    self.end_headers()
                    return

                if self.headers.get("Upgrade", "").lower() != "websocket":
                    self.send_response(400)
                    self.end_headers()
                    return

                key = self.headers.get("Sec-WebSocket-Key", "").strip()
                if not key:
                    self.send_response(400)
                    self.end_headers()
                    return

                accept = base64.b64encode(
                    hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("utf-8")).digest()
                ).decode("ascii")
                self.send_response(101, "Switching Protocols")
                self.send_header("Upgrade", "websocket")
                self.send_header("Connection", "Upgrade")
                self.send_header("Sec-WebSocket-Accept", accept)
                self.end_headers()
                parent._serve_ws_client(self.connection)

            def do_POST(self) -> None:  # noqa: N802
                length = int(self.headers.get("Content-Length", "0"))
                body = self.rfile.read(length) if length else b""
                try:
                    _ = json.loads(body.decode("utf-8")) if body else {}
                except json.JSONDecodeError:
                    _ = {}

                if self.path == "/api/user/login":
                    with parent._lock:
                        parent.login_call_count += 1
                        item = parent.login_queue.popleft() if parent.login_queue else ("error", "unexpected login request")
                    self._respond(item)
                    return

                if self.path == "/api/vessel/command":
                    with parent._lock:
                        parent.scan_call_count += 1
                        item = parent.scan_queue.popleft() if parent.scan_queue else ("error", "unexpected scan request")
                    self._respond(item)
                    return

                if self.path == "/api/vessellog/last":
                    with parent._lock:
                        parent.last_log_call_count += 1
                        item = parent.last_log_queue.popleft() if parent.last_log_queue else ("error", "unexpected vessellog.last request")
                    self._respond(item)
                    return

                self.send_response(404)
                self.end_headers()

            def _respond(self, item: tuple[str, object]) -> None:
                kind, payload = item
                if kind == "hang":
                    time.sleep(60.0)
                    return
                if kind == "done":
                    obj = {"code": 0, "message": "ok", "data": payload}
                elif kind == "fresh":
                    obj = {
                        "code": 0,
                        "message": "ok",
                        "data": {
                            "logTime": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(time.time() + 1)),
                            "pointCloudPath": "/tmp/pc/latest.pcd",
                            "volume": 12.34,
                        },
                    }
                elif kind == "stale":
                    obj = {
                        "code": 0,
                        "message": "ok",
                        "data": {
                            "logTime": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(time.time() - 3600)),
                            "pointCloudPath": "/tmp/pc/old.pcd",
                            "volume": 8.76,
                        },
                    }
                else:
                    obj = {"code": 1, "message": str(payload), "data": {}}

                data = json.dumps(obj).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def log_message(self, format: str, *args) -> None:  # noqa: A003
                return

        return Handler

    def _serve_ws_client(self, conn: socket.socket) -> None:
        with self._lock:
            self._ws_clients[conn] = set()

        try:
            while True:
                message = self._recv_ws_text(conn)
                if message is None:
                    return
                try:
                    obj = json.loads(message)
                except json.JSONDecodeError:
                    continue

                msg_type = obj.get("type")
                topic = str(obj.get("topic", ""))
                with self._lock:
                    topics = self._ws_clients.get(conn)
                    if topics is None:
                        return
                    if msg_type == "sub" and topic:
                        topics.add(topic)
                    elif msg_type == "unsub" and topic:
                        topics.discard(topic)
                if msg_type == "ping":
                    self._send_ws_frame(conn, b"", opcode=0xA)
        finally:
            with self._lock:
                self._ws_clients.pop(conn, None)
            try:
                conn.close()
            except OSError:
                pass

    def _recv_exact(self, conn: socket.socket, size: int) -> bytes | None:
        chunks = bytearray()
        while len(chunks) < size:
            try:
                chunk = conn.recv(size - len(chunks))
            except OSError:
                return None
            if not chunk:
                return None
            chunks.extend(chunk)
        return bytes(chunks)

    def _recv_ws_text(self, conn: socket.socket) -> str | None:
        header = self._recv_exact(conn, 2)
        if not header:
            return None

        b0, b1 = header[0], header[1]
        opcode = b0 & 0x0F
        masked = (b1 & 0x80) != 0
        payload_len = b1 & 0x7F

        if payload_len == 126:
            extended = self._recv_exact(conn, 2)
            if not extended:
                return None
            payload_len = int.from_bytes(extended, "big")
        elif payload_len == 127:
            extended = self._recv_exact(conn, 8)
            if not extended:
                return None
            payload_len = int.from_bytes(extended, "big")

        mask = self._recv_exact(conn, 4) if masked else b""
        if masked and not mask:
            return None
        payload = self._recv_exact(conn, payload_len)
        if payload is None:
            return None
        if masked:
            payload = bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload))

        if opcode == 0x8:
            return None
        if opcode != 0x1:
            return ""
        return payload.decode("utf-8", errors="replace")

    def _send_ws_frame(self, conn: socket.socket, payload: bytes, opcode: int = 0x1) -> None:
        header = bytearray([0x80 | (opcode & 0x0F)])
        length = len(payload)
        if length < 126:
            header.append(length)
        elif length <= 0xFFFF:
            header.append(126)
            header.extend(length.to_bytes(2, "big"))
        else:
            header.append(127)
            header.extend(length.to_bytes(8, "big"))
        try:
            conn.sendall(bytes(header) + payload)
        except OSError:
            pass


def wait_port(host: str, port: int, timeout_s: float) -> bool:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.2):
                return True
        except OSError:
            time.sleep(0.05)
    return False


def start_plc_crane_sim(sim_exe: Path, env: dict[str, str], port: int) -> subprocess.Popen[str]:
    proc = subprocess.Popen(
        [
            str(sim_exe),
            "--profile=oneshot",
            "--mode=console",
            "--cmd=run",
            "--listen_address=127.0.0.1",
            f"--listen_port={port}",
            "--unit_id=1",
            "--event_mode=none",
            "--cylinder_up_delay=40",
            "--cylinder_down_delay=40",
            "--valve_open_delay=40",
            "--valve_close_delay=40",
            "--heartbeat_ms=0",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
    )
    if not wait_port("127.0.0.1", port, 4.0):
        stdout, stderr = stop_process(proc)
        raise RuntimeError(f"plc_crane_sim did not become ready\nstdout:\n{stdout}\nstderr:\n{stderr}")
    return proc


def create_temp_data_root(service_exe: Path) -> tuple[Path, Path]:
    tmp_dir = Path(tempfile.mkdtemp(prefix="m101_bin_scan_"))
    data_root = tmp_dir / "data_root"
    for sub in ("services", "projects", "drivers", "logs"):
        (data_root / sub).mkdir(parents=True, exist_ok=True)

    copytree(ROOT_DIR / "src" / "data_root" / "services" / "bin_scan_orchestrator",
             data_root / "services" / "bin_scan_orchestrator")

    for driver_name in ("stdio.drv.plc_crane", "stdio.drv.3dvision"):
        driver_dir = find_driver_dir(driver_name)
        if driver_dir is None:
            raise RuntimeError(format_missing_driver(driver_name))
        copytree(driver_dir, data_root / "drivers" / driver_name)

    write_json(data_root / "config.json", {"serviceProgram": str(service_exe)})
    return tmp_dir, data_root


def make_config(vision_addr: str, plc_port: int, result_path: Path | None = None) -> dict:
    return {
        "vessel_id": 15,
        "vision": {
            "addr": vision_addr,
            "user_name": "admin",
            "password": "123456",
            "view_mode": False,
        },
        "cranes": [
            {
                "id": "crane_a",
                "host": "127.0.0.1",
                "port": plc_port,
                "unit_id": 1,
                "timeout_ms": 1000,
            }
        ],
        "crane_poll_interval_ms": 100,
        "crane_wait_timeout_ms": 2000,
        "scan_request_timeout_ms": 500,
        "scan_start_retry_count": 2,
        "scan_start_retry_interval_ms": 50,
        "scan_poll_interval_ms": 200,
        "scan_poll_fail_limit": 5,
        "scan_timeout_ms": 2000,
        "clock_skew_tolerance_ms": 2000,
        "on_error_set_manual": True,
        "result_output_path": str(result_path) if result_path else "",
    }


def run_cli_case(case_name: str, expect_success: bool, timeout_case: bool) -> CaseResult:
    service_exe = find_executable("stdiolink_service")
    sim_exe = find_driver_executable("stdio.drv.plc_crane_sim")
    if service_exe is None:
        return CaseResult(case_name, "fail", format_missing("stdiolink_service"))
    if sim_exe is None:
        return CaseResult(case_name, "fail", format_missing_driver_executable("stdio.drv.plc_crane_sim"))

    env = make_env()
    tmp_dir, data_root = create_temp_data_root(service_exe)
    vision = FakeVisionServer()
    plc_proc: subprocess.Popen[str] | None = None
    try:
        plc_port = allocate_port()
        plc_proc = start_plc_crane_sim(sim_exe, env, plc_port)
        result_path = tmp_dir / "result.json"

        vision.enqueue_login_done("token-1")
        vision.enqueue_scan_done()
        if timeout_case:
            for _ in range(12):
                vision.enqueue_last_log_older_than_now()
        else:
            vision.enqueue_last_log_newer_than_now()

        config = make_config(vision.addr, plc_port, result_path if expect_success else None)
        if timeout_case:
            config["scan_timeout_ms"] = 1000
            config["scan_poll_interval_ms"] = 200
            config["result_output_path"] = ""

        cfg_path = tmp_dir / "config.json"
        write_json(cfg_path, config)

        proc = subprocess.run(
            [
                str(service_exe),
                str(data_root / "services" / "bin_scan_orchestrator"),
                f"--data-root={data_root}",
                f"--config-file={cfg_path}",
            ],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=20.0,
            env=env,
        )

        if expect_success:
            if proc.returncode != 0:
                return CaseResult(case_name, "fail", f"expected success, got {proc.returncode}\nstderr:\n{proc.stderr}")
            if not result_path.exists():
                return CaseResult(case_name, "fail", f"missing result file\nstderr:\n{proc.stderr}")
            return CaseResult(case_name, "pass", "exit=0 and result file created")

        if proc.returncode == 0:
            return CaseResult(case_name, "fail", f"expected failure, got 0\nstderr:\n{proc.stderr}")
        if "scan timeout" not in proc.stderr:
            return CaseResult(case_name, "fail", f"missing timeout log\nstderr:\n{proc.stderr}")
        return CaseResult(case_name, "pass", "timeout path returns non-zero and logs reason")
    except Exception as exc:
        return CaseResult(case_name, "fail", str(exc))
    finally:
        vision.close()
        if plc_proc is not None:
            stop_process(plc_proc)
        cleanup_dir(tmp_dir)


def run_server_case(case_name: str, fixed_rate: bool) -> CaseResult:
    server_exe = find_executable("stdiolink_server")
    service_exe = find_executable("stdiolink_service")
    sim_exe = find_driver_executable("stdio.drv.plc_crane_sim")
    if server_exe is None:
        return CaseResult(case_name, "fail", format_missing("stdiolink_server"))
    if service_exe is None:
        return CaseResult(case_name, "fail", format_missing("stdiolink_service"))
    if sim_exe is None:
        return CaseResult(case_name, "fail", format_missing_driver_executable("stdio.drv.plc_crane_sim"))

    env = make_env()
    tmp_dir, data_root = create_temp_data_root(service_exe)
    vision = FakeVisionServer()
    plc_proc: subprocess.Popen[str] | None = None
    server_proc: subprocess.Popen[str] | None = None
    try:
        plc_port = allocate_port()
        plc_proc = start_plc_crane_sim(sim_exe, env, plc_port)

        for _ in range(8 if fixed_rate else 2):
            vision.enqueue_login_done("token-1")
            vision.enqueue_scan_done()
            vision.enqueue_last_log_newer_than_now()

        project_id = "m101_bin_scan_project"
        schedule = {"type": "manual"} if not fixed_rate else {
            "type": "fixed_rate",
            "intervalMs": 1200,
            "maxConcurrent": 1,
        }
        write_project(
            data_root,
            project_id,
            {
                "id": project_id,
                "name": project_id,
                "serviceId": "bin_scan_orchestrator",
                "enabled": True if fixed_rate else False,
                "schedule": schedule,
            },
            make_config(vision.addr, plc_port, None),
        )

        port = allocate_port()
        server_proc = subprocess.Popen(
            [
                str(server_exe),
                f"--data-root={data_root}",
                f"--port={port}",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=env,
        )
        base_url = f"http://127.0.0.1:{port}"
        if not wait_http_ready(base_url):
            stdout, stderr = stop_process(server_proc)
            return CaseResult(case_name, "fail", f"server not ready\nstdout:\n{stdout}\nstderr:\n{stderr}")

        status, body = http_request("GET", f"{base_url}/api/services")
        if status != 200:
            stdout, stderr = stop_process(server_proc)
            return CaseResult(case_name, "fail", f"service list missing status={status}\nbody:\n{body}\nstdout:\n{stdout}\nstderr:\n{stderr}")
        try:
            services = json.loads(body).get("services", [])
        except json.JSONDecodeError:
            services = []
        if not any(isinstance(item, dict) and item.get("id") == "bin_scan_orchestrator" for item in services):
            stdout, stderr = stop_process(server_proc)
            return CaseResult(case_name, "fail", f"service list does not contain bin_scan_orchestrator\nbody:\n{body}\nstdout:\n{stdout}\nstderr:\n{stderr}")

        if not fixed_rate:
            status, body = http_request("POST", f"{base_url}/api/projects/{project_id}/start")
            if status != 200:
                stdout, stderr = stop_process(server_proc)
                return CaseResult(case_name, "fail", f"start failed status={status}\nbody:\n{body}\nstdout:\n{stdout}\nstderr:\n{stderr}")
            ok, runtime_obj = wait_runtime(
                base_url,
                project_id,
                lambda obj: obj.get("runningInstances") == 0 and not obj.get("instances"),
                timeout_s=15.0,
            )
            if not ok:
                stdout, stderr = stop_process(server_proc)
                return CaseResult(case_name, "fail", f"runtime not cleaned\nruntime:\n{runtime_obj}\nstdout:\n{stdout}\nstderr:\n{stderr}")

            status, body = http_request("GET", f"{base_url}/api/services/bin_scan_orchestrator")
            if status != 200:
                return CaseResult(case_name, "fail", f"service detail missing status={status}\nbody:\n{body}")

            log_path = data_root / "logs" / f"{project_id}.log"
            ok, text = wait_for_log(log_path, "scan completed", 10.0)
            if not ok:
                stdout, stderr = stop_process(server_proc)
                return CaseResult(case_name, "fail", f"missing scan completed log\nlog:\n{text}\nstdout:\n{stdout}\nstderr:\n{stderr}")
            return CaseResult(case_name, "pass", "manual start completed and runtime/logs observable")

        log_path = data_root / "logs" / f"{project_id}.log"
        ok, text = wait_for_log(log_path, "scan completed", 20.0)
        if not ok:
            stdout, stderr = stop_process(server_proc)
            return CaseResult(case_name, "fail", f"first fixed-rate run not observed\nlog:\n{text}\nstdout:\n{stdout}\nstderr:\n{stderr}")

        deadline = time.time() + 20.0
        while time.time() < deadline:
            text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
            if text.count("scan completed") >= 2:
                ok_rt, runtime_obj = wait_runtime(
                    base_url,
                    project_id,
                    lambda obj: obj.get("schedule", {}).get("timerActive") is True,
                    timeout_s=2.0,
                )
                if not ok_rt:
                    return CaseResult(case_name, "fail", f"timer not active\nruntime:\n{runtime_obj}")
                return CaseResult(case_name, "pass", "fixed_rate triggered at least twice")
            time.sleep(0.2)

        stdout, stderr = stop_process(server_proc)
        return CaseResult(case_name, "fail", f"did not observe two fixed-rate runs\nlog:\n{text}\nstdout:\n{stdout}\nstderr:\n{stderr}")
    except Exception as exc:
        return CaseResult(case_name, "fail", str(exc))
    finally:
        vision.close()
        if server_proc is not None and server_proc.poll() is None:
            stop_process(server_proc)
        if plc_proc is not None:
            stop_process(plc_proc)
        cleanup_dir(tmp_dir)


CASES = {
    "s01_cli_success": lambda: run_cli_case("S01", expect_success=True, timeout_case=False),
    "s02_cli_timeout": lambda: run_cli_case("S02", expect_success=False, timeout_case=True),
    "s03_server_manual": lambda: run_server_case("S03", fixed_rate=False),
    "s04_server_fixed_rate": lambda: run_server_case("S04", fixed_rate=True),
}


def run_selected(case_name: str) -> list[CaseResult]:
    if case_name == "all":
        return [fn() for fn in CASES.values()]
    fn = CASES.get(case_name)
    if fn is None:
        return [CaseResult(case_name, "fail", "unknown case")]
    return [fn()]


def main() -> int:
    parser = argparse.ArgumentParser(description="M101 bin scan orchestrator smoke tests.")
    parser.add_argument("--case", default="all", choices=["all", *CASES.keys()])
    args = parser.parse_args()

    results = run_selected(args.case)
    failed = 0
    for result in results:
        if result.status == "pass":
            print(f"[PASS] {result.name}: {result.detail}")
        else:
            failed += 1
            print(f"[FAIL] {result.name}: {result.detail}")

    passed = len(results) - failed
    print(f"[SUMMARY] total={len(results)} pass={passed} fail={failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
