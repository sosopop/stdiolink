#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import queue
import socket
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent
EXE_SUFFIX = ".exe" if os.name == "nt" else ""

CLIENT_DRIVER = "stdio.drv.opcua"
SERVER_DRIVER = "stdio.drv.opcua_server"


@dataclass
class CaseResult:
    name: str
    status: str  # pass | fail | skip
    detail: str


def candidate_bin_dirs() -> list[Path]:
    dirs: list[Path] = []
    env_bin = os.environ.get("STDIOLINK_BIN_DIR")
    if env_bin:
        dirs.append(Path(env_bin).resolve())
    dirs.append((PROJECT_ROOT / "build" / "runtime_release" / "bin").resolve())
    return dirs


def candidate_runtime_roots() -> list[Path]:
    roots: list[Path] = []
    env_bin = os.environ.get("STDIOLINK_BIN_DIR")
    if env_bin:
        roots.append(Path(env_bin).resolve().parent)
    roots.append((PROJECT_ROOT / "build" / "runtime_release").resolve())
    return roots


def make_env() -> dict[str, str]:
    env = os.environ.copy()
    extra = os.pathsep.join(str(d) for d in candidate_bin_dirs() if d.exists())
    if extra:
        env["PATH"] = extra + os.pathsep + env.get("PATH", "")
    return env


def runtime_data_root() -> Path | None:
    env_root = os.environ.get("STDIOLINK_DATA_ROOT")
    candidates: list[Path] = []
    if env_root:
        candidates.append(Path(env_root).resolve())
    for runtime_root in candidate_runtime_roots():
        candidates.append(runtime_root / "data_root")
    for candidate in candidates:
        if (candidate / "drivers").exists() and (candidate / "services").exists():
            return candidate
    return None


def find_driver_exe(driver_name: str) -> Path | None:
    filename = f"{driver_name}{EXE_SUFFIX}"
    for runtime_root in candidate_runtime_roots():
        path = runtime_root / "data_root" / "drivers" / driver_name / filename
        if path.exists():
            return path
    return None


def find_service_exe() -> Path | None:
    filename = f"stdiolink_service{EXE_SUFFIX}"
    for bin_dir in candidate_bin_dirs():
        path = bin_dir / filename
        if path.exists():
            return path
    return None


def find_service_dir(service_name: str) -> Path | None:
    data_root = runtime_data_root()
    if data_root is None:
        return None
    path = data_root / "services" / service_name
    return path if (path / "index.js").exists() else None


def allocate_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def parse_jsonl_lines(text: str) -> list[dict]:
    rows: list[dict] = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            rows.append(value)
    return rows


def run_process(args: list[str], timeout_s: float = 20.0) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout_s,
        env=make_env(),
    )


def console_arg_value(value: object) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def run_client_command(cmd: str, params: dict[str, object], timeout_s: float = 15.0) -> dict:
    client_exe = find_driver_exe(CLIENT_DRIVER)
    if client_exe is None:
        raise RuntimeError(f"{CLIENT_DRIVER} executable not found")

    args = [
        str(client_exe),
        "--profile=oneshot",
        "--mode=console",
        f"--cmd={cmd}",
    ]
    for key, value in params.items():
        args.append(f"--{key}={console_arg_value(value)}")

    proc = run_process(args, timeout_s=timeout_s)
    if proc.returncode != 0:
        raise RuntimeError(
            f"{cmd} exited with {proc.returncode}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
    rows = parse_jsonl_lines(proc.stdout)
    for row in reversed(rows):
        if row.get("status") in {"done", "error"}:
            return row
    raise RuntimeError(f"{cmd} missing terminal JSONL row\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}")


def sample_nodes() -> list[dict[str, object]]:
    return [
        {
            "node_id": "ns=1;s=Plant",
            "parent_node_id": "i=85",
            "node_class": "folder",
            "browse_name": "Plant",
            "display_name": "Plant",
            "description": "Plant root",
        },
        {
            "node_id": "ns=1;s=Plant.Line1",
            "parent_node_id": "ns=1;s=Plant",
            "node_class": "folder",
            "browse_name": "Line1",
            "display_name": "Line1",
            "description": "Line 1",
        },
        {
            "node_id": "ns=1;s=Plant.Line1.Temp",
            "parent_node_id": "ns=1;s=Plant.Line1",
            "node_class": "variable",
            "browse_name": "Temp",
            "display_name": "Temp",
            "description": "Process temperature",
            "data_type": "double",
            "access": "read_only",
            "initial_value": 36.5,
        },
        {
            "node_id": "ns=1;s=Plant.Line1.SetPoint",
            "parent_node_id": "ns=1;s=Plant.Line1",
            "node_class": "variable",
            "browse_name": "SetPoint",
            "display_name": "SetPoint",
            "description": "Temperature setpoint",
            "data_type": "double",
            "access": "read_write",
            "initial_value": 40.0,
        },
        {
            "node_id": "ns=1;s=Plant.Line1.Mode",
            "parent_node_id": "ns=1;s=Plant.Line1",
            "node_class": "variable",
            "browse_name": "Mode",
            "display_name": "Mode",
            "description": "Operating mode",
            "data_type": "string",
            "access": "read_write",
            "initial_value": "Auto",
        },
    ]


class DriverSession:
    def __init__(self, executable: Path):
        self.proc = subprocess.Popen(
            [str(executable), "--profile=keepalive"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            env=make_env(),
        )
        self._queue: queue.Queue[dict] = queue.Queue()
        self._stderr_lines: list[str] = []
        self._stdout_thread = threading.Thread(target=self._pump_stdout, daemon=True)
        self._stderr_thread = threading.Thread(target=self._pump_stderr, daemon=True)
        self._stdout_thread.start()
        self._stderr_thread.start()

    def _pump_stdout(self) -> None:
        assert self.proc.stdout is not None
        for raw in self.proc.stdout:
            line = raw.strip()
            if not line:
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError:
                value = {"status": "raw", "line": line}
            if isinstance(value, dict):
                self._queue.put(value)

    def _pump_stderr(self) -> None:
        assert self.proc.stderr is not None
        for raw in self.proc.stderr:
            self._stderr_lines.append(raw.rstrip())

    def request(self, cmd: str, data: dict[str, object], timeout_s: float = 10.0) -> tuple[dict, list[dict]]:
        if self.proc.stdin is None:
            raise RuntimeError("driver stdin is not available")
        payload = json.dumps({"cmd": cmd, "data": data}, ensure_ascii=False) + "\n"
        self.proc.stdin.write(payload)
        self.proc.stdin.flush()

        deadline = time.time() + timeout_s
        events: list[dict] = []
        while time.time() < deadline:
            remaining = max(0.1, deadline - time.time())
            try:
                row = self._queue.get(timeout=remaining)
            except queue.Empty as exc:
                raise RuntimeError(
                    f"{cmd} timed out waiting for response\nstderr:\n{self.stderr_text()}"
                ) from exc

            status = row.get("status")
            if status == "event":
                events.append(row)
                continue
            if status in {"done", "error"}:
                return row, events

        raise RuntimeError(f"{cmd} timed out\nstderr:\n{self.stderr_text()}")

    def stderr_text(self) -> str:
        return "\n".join(self._stderr_lines)

    def close(self) -> None:
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=3)


def wait_until(predicate, timeout_s: float) -> bool:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if predicate():
            return True
        time.sleep(0.1)
    return predicate()


def inspect_node(host: str, port: int, node_id: str, recurse: bool = False) -> dict:
    row = run_client_command(
        "inspect_node",
        {"host": host, "port": port, "node_id": node_id, "recurse": recurse},
    )
    if row.get("status") != "done":
        raise RuntimeError(f"inspect_node failed: {row}")
    return row.get("data", {})


def snapshot_nodes(host: str, port: int) -> dict:
    row = run_client_command(
        "snapshot_nodes",
        {"host": host, "port": port},
        timeout_s=20.0,
    )
    if row.get("status") != "done":
        raise RuntimeError(f"snapshot_nodes failed: {row}")
    return row.get("data", {})


def stop_process(proc: subprocess.Popen[str]) -> tuple[str, str]:
    stdout = ""
    stderr = ""
    if proc.poll() is None:
        proc.terminate()
        try:
            stdout, stderr = proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            stdout, stderr = proc.communicate(timeout=3)
    else:
        stdout, stderr = proc.communicate(timeout=1)
    return stdout, stderr


def case_service_snapshot_roundtrip() -> CaseResult:
    service_exe = find_service_exe()
    service_dir = find_service_dir("opcua_server_service")
    data_root = runtime_data_root()
    if service_exe is None:
        return CaseResult("S01_service_snapshot", "skip", "stdiolink_service executable not found")
    if service_dir is None or data_root is None:
        return CaseResult("S01_service_snapshot", "skip", "runtime service/data_root not found")
    if find_driver_exe(CLIENT_DRIVER) is None:
        return CaseResult("S01_service_snapshot", "skip", f"{CLIENT_DRIVER} executable not found")

    port = allocate_port()
    config = {
        "bind_host": "127.0.0.1",
        "listen_port": port,
        "endpoint_path": "",
        "server_name": "stdiolink OPC UA Server",
        "application_uri": "urn:stdiolink:opcua:server",
        "namespace_uri": "urn:stdiolink:opcua:nodes",
        "event_mode": "write",
        "nodes": sample_nodes(),
    }

    with tempfile.TemporaryDirectory(prefix="opcua_server_service_") as temp_dir:
        config_path = Path(temp_dir) / "config.json"
        config_path.write_text(json.dumps(config, ensure_ascii=False, indent=2), encoding="utf-8")

        proc = subprocess.Popen(
            [
                str(service_exe),
                str(service_dir),
                f"--data-root={data_root}",
                f"--config-file={config_path}",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=make_env(),
        )
        try:
            ready = wait_until(
                lambda: _service_is_ready(proc, port),
                timeout_s=12.0,
            )
            if not ready:
                stdout, stderr = stop_process(proc)
                return CaseResult(
                    "S01_service_snapshot",
                    "fail",
                    f"service did not become ready\nstdout:\n{stdout}\nstderr:\n{stderr}",
                )

            inspected = inspect_node("127.0.0.1", port, "ns=1;s=Plant.Line1.SetPoint")
            node = inspected.get("node", {})
            if node.get("value") != 40.0:
                return CaseResult(
                    "S01_service_snapshot",
                    "fail",
                    f"inspect_node returned unexpected value: {node}",
                )

            snapshot = snapshot_nodes("127.0.0.1", port)
            snapshot_text = json.dumps(snapshot, ensure_ascii=False)
            if "ns=1;s=Plant.Line1.Temp" not in snapshot_text or "ns=1;s=Plant.Line1.Mode" not in snapshot_text:
                return CaseResult(
                    "S01_service_snapshot",
                    "fail",
                    f"snapshot_nodes missing expected business nodes: {snapshot_text[:800]}",
                )

            return CaseResult(
                "S01_service_snapshot",
                "pass",
                "service launched and client driver can inspect/snapshot the business tree",
            )
        finally:
            stop_process(proc)


def _service_is_ready(proc: subprocess.Popen[str], port: int) -> bool:
    if proc.poll() is not None:
        return False
    try:
        inspected = inspect_node("127.0.0.1", port, "ns=1;s=Plant.Line1.SetPoint")
    except Exception:
        return False
    node = inspected.get("node", {})
    return node.get("value") == 40.0


def case_driver_command_write_roundtrip() -> CaseResult:
    server_exe = find_driver_exe(SERVER_DRIVER)
    client_exe = find_driver_exe(CLIENT_DRIVER)
    if server_exe is None:
        return CaseResult("S02_driver_write_roundtrip", "skip", f"{SERVER_DRIVER} executable not found")
    if client_exe is None:
        return CaseResult("S02_driver_write_roundtrip", "skip", f"{CLIENT_DRIVER} executable not found")

    port = allocate_port()
    session = DriverSession(server_exe)
    try:
        row, _ = session.request(
            "start_server",
            {"bind_host": "127.0.0.1", "listen_port": port},
        )
        if row.get("status") != "done":
            return CaseResult("S02_driver_write_roundtrip", "fail", f"start_server failed: {row}")

        row, _ = session.request("upsert_nodes", {"nodes": sample_nodes()})
        if row.get("status") != "done":
            return CaseResult("S02_driver_write_roundtrip", "fail", f"upsert_nodes failed: {row}")

        row, events = session.request(
            "write_values",
            {
                "items": [
                    {
                        "node_id": "ns=1;s=Plant.Line1.SetPoint",
                        "value": 44.5,
                    }
                ]
            },
        )
        if row.get("status") != "done":
            return CaseResult("S02_driver_write_roundtrip", "fail", f"write_values failed: {row}")

        inspect_data = inspect_node("127.0.0.1", port, "ns=1;s=Plant.Line1.SetPoint")
        node = inspect_data.get("node", {})
        if node.get("value") != 44.5:
            return CaseResult(
                "S02_driver_write_roundtrip",
                "fail",
                f"client inspect_node did not observe updated SetPoint: {node}",
            )

        snapshot_data = snapshot_nodes("127.0.0.1", port)
        snapshot_text = json.dumps(snapshot_data, ensure_ascii=False)
        if "ns=1;s=Plant.Line1.SetPoint" not in snapshot_text:
            return CaseResult(
                "S02_driver_write_roundtrip",
                "fail",
                f"snapshot_nodes missing SetPoint after command write: {snapshot_text[:800]}",
            )

        def is_command_write_event(event: dict) -> bool:
            nested = event.get("data")
            if isinstance(nested, dict) and event.get("event") == "node_value_changed":
                return nested.get("source") == "command_write"
            if isinstance(nested, dict):
                return nested.get("event") == "node_value_changed" and isinstance(nested.get("data"), dict) \
                    and nested["data"].get("source") == "command_write"
            return False

        if not any(is_command_write_event(event) for event in events):
            return CaseResult(
                "S02_driver_write_roundtrip",
                "fail",
                f"write_values did not emit command_write event: {events}",
            )

        session.request("stop_server", {})
        return CaseResult(
            "S02_driver_write_roundtrip",
            "pass",
            "server driver write_values updated the OPC UA node and client driver read back the new value",
        )
    except Exception as exc:
        return CaseResult(
            "S02_driver_write_roundtrip",
            "fail",
            f"{exc}\nstderr:\n{session.stderr_text()}",
        )
    finally:
        session.close()


CASES = {
    "s01_service_snapshot": case_service_snapshot_roundtrip,
    "s02_driver_write_roundtrip": case_driver_command_write_roundtrip,
}


def main() -> int:
    parser = argparse.ArgumentParser(description="M107 OPC UA server smoke tests")
    parser.add_argument("--case", default="all", choices=["all", *CASES.keys()])
    args = parser.parse_args()

    selected = CASES.values() if args.case == "all" else [CASES[args.case]]
    results = [case() for case in selected]

    failed = 0
    skipped = 0
    for result in results:
        if result.status == "pass":
            print(f"[PASS] {result.name}: {result.detail}")
        elif result.status == "skip":
            skipped += 1
            print(f"[SKIP] {result.name}: {result.detail}")
        else:
            failed += 1
            print(f"[FAIL] {result.name}: {result.detail}")

    passed = len(results) - failed - skipped
    print(f"[SUMMARY] total={len(results)} pass={passed} skip={skipped} fail={failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
