#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[2]
EXE_SUFFIX = ".exe" if os.name == "nt" else ""


@dataclass
class CaseResult:
    name: str
    status: str
    detail: str


def candidate_bin_dirs() -> list[Path]:
    dirs: list[Path] = []
    env_bin = os.environ.get("STDIOLINK_BIN_DIR")
    if env_bin:
        dirs.append(Path(env_bin))
    dirs.append(ROOT_DIR / "build" / "runtime_release" / "bin")
    return dirs


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


def format_missing(name: str) -> str:
    candidates = "\n".join(str(d / f"{name}{EXE_SUFFIX}") for d in candidate_bin_dirs())
    return f"missing executable: {name}{EXE_SUFFIX}\ncandidates:\n{candidates}"


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
    request = urllib.request.Request(url, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=timeout_s) as response:
            return response.status, response.read().decode("utf-8", errors="replace")
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


def wait_for_file_text(path: Path, needle: str, timeout_s: float) -> bool:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if path.exists():
            content = path.read_text(encoding="utf-8", errors="replace")
            if needle in content:
                return True
        time.sleep(0.1)
    return path.exists() and needle in path.read_text(encoding="utf-8", errors="replace")


def wait_project_runtime(
    base_url: str,
    project_id: str,
    predicate,
    timeout_s: float = 10.0,
) -> tuple[bool, dict | None]:
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


def write_json(path: Path, obj: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2), encoding="utf-8")


def write_project(data_root: Path, project_id: str, config_obj: dict, param_obj: dict) -> None:
    project_dir = data_root / "projects" / project_id
    write_json(project_dir / "config.json", config_obj)
    write_json(project_dir / "param.json", param_obj)


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def cleanup_dir(path: Path) -> None:
    for _ in range(20):
        try:
            shutil.rmtree(path)
            return
        except FileNotFoundError:
            return
        except PermissionError:
            time.sleep(0.25)
    print(f"[WARN] failed to cleanup temp dir: {path}")


def create_server_timeout_stub_service(service_dir: Path) -> None:
    write_json(
        service_dir / "manifest.json",
        {
            "manifestVersion": "1",
            "id": "m101a_timeout_service",
            "name": "M101A Timeout Stub Service",
            "version": "1.0.0",
        },
    )
    write_json(
        service_dir / "config.schema.json",
        {
            "_test": {
                "type": "object",
                "fields": {
                    "exitCode": {"type": "int", "default": 0},
                    "sleepMs": {"type": "int", "default": 0, "constraints": {"min": 0, "max": 600000}},
                    "stderrText": {"type": "string", "default": ""},
                },
            }
        },
    )
    write_text(
        service_dir / "index.js",
        "console.error('index.js should not run when serviceProgram is test_service_stub');\n",
    )


def create_proxy_timeout_service(service_dir: Path) -> None:
    write_json(
        service_dir / "manifest.json",
        {
            "manifestVersion": "1",
            "id": "m101a_proxy_timeout_service",
            "name": "M101A Proxy Timeout Service",
            "version": "1.0.0",
        },
    )
    write_json(
        service_dir / "config.schema.json",
        {
            "driverPath": {"type": "string", "required": True},
            "delayMs": {
                "type": "int",
                "default": 0,
                "constraints": {"min": 0, "max": 600000},
            },
            "timeoutMs": {
                "type": "int",
                "default": 0,
                "constraints": {"min": 0, "max": 600000},
            },
        },
    )
    write_text(
        service_dir / "index.js",
        """import { getConfig, openDriver } from "stdiolink";

const cfg = getConfig();

(async () => {
    let drv = null;
    try {
        drv = await openDriver(String(cfg.driverPath));
        await drv.delayed_done(
            { delayMs: Number(cfg.delayMs ?? 0) },
            { timeoutMs: Number(cfg.timeoutMs ?? 0) }
        );
        console.error("command completed");
    } finally {
        if (drv) {
            drv.$close();
        }
    }
})();
""",
    )


def run_server_case(case_name: str, sleep_ms: int, run_timeout_ms: int, expect_timeout: bool) -> CaseResult:
    server_exe = find_executable("stdiolink_server")
    if server_exe is None:
        return CaseResult(case_name, "fail", format_missing("stdiolink_server"))
    if find_executable("stdiolink_service") is None:
        return CaseResult(case_name, "fail", format_missing("stdiolink_service"))
    test_service_stub = find_executable("test_service_stub")
    if test_service_stub is None:
        return CaseResult(case_name, "fail", format_missing("test_service_stub"))

    tmp_dir = Path(tempfile.mkdtemp(prefix="m101a_server_"))
    try:
        data_root = tmp_dir / "data_root"
        for sub in ("services", "projects", "logs"):
            (data_root / sub).mkdir(parents=True, exist_ok=True)

        service_dir = data_root / "services" / "m101a_timeout_service"
        create_server_timeout_stub_service(service_dir)
        write_json(
            data_root / "config.json",
            {
                "serviceProgram": str(test_service_stub),
            },
        )

        project_id = "m101a_timeout_project"
        write_project(
            data_root,
            project_id,
            {
                "id": project_id,
                "name": project_id,
                "serviceId": "m101a_timeout_service",
                "enabled": True,
                "schedule": {"type": "manual", "runTimeoutMs": run_timeout_ms},
            },
            {
                "_test": {
                    "exitCode": 0,
                    "sleepMs": sleep_ms,
                    "stderrText": "test_service_stub started",
                }
            },
        )

        port = allocate_port()
        env = make_env()
        proc = subprocess.Popen(
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
        try:
            base_url = f"http://127.0.0.1:{port}"
            if not wait_http_ready(base_url):
                stdout, stderr = stop_process(proc)
                return CaseResult(
                    case_name,
                    "fail",
                    f"server did not become ready\nstdout:\n{stdout}\nstderr:\n{stderr}",
                )

            status, body = http_request("POST", f"{base_url}/api/projects/{project_id}/start")
            if status != 200:
                stdout, stderr = stop_process(proc)
                return CaseResult(
                    case_name,
                    "fail",
                    f"start request failed: status={status}\nbody:\n{body}\nstdout:\n{stdout}\nstderr:\n{stderr}",
                )

            log_path = data_root / "logs" / f"{project_id}.log"
            expected_text = "runtime cleanup observed"
            if expect_timeout:
                expected_text = f"service run timeout ({run_timeout_ms} ms)"
                if not wait_for_file_text(log_path, expected_text, timeout_s=15.0):
                    stdout, stderr = stop_process(proc)
                    log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
                    return CaseResult(
                        case_name,
                        "fail",
                        f"log did not contain expected text: {expected_text}\nlog:\n{log_text}\nstdout:\n{stdout}\nstderr:\n{stderr}",
                    )

            log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
            cleaned, runtime_obj = wait_project_runtime(
                base_url,
                project_id,
                lambda obj: obj.get("runningInstances") == 0 and not obj.get("instances"),
                timeout_s=15.0,
            )
            if not cleaned:
                stdout, stderr = stop_process(proc)
                return CaseResult(
                    case_name,
                    "fail",
                    "project runtime did not clean up after completion\n"
                    f"runtime:\n{json.dumps(runtime_obj, ensure_ascii=False, indent=2) if runtime_obj else '<none>'}\n"
                    f"log:\n{log_text}\nstdout:\n{stdout}\nstderr:\n{stderr}",
                )

            log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
            if "test_service_stub started" not in log_text:
                stdout, stderr = stop_process(proc)
                return CaseResult(
                    case_name,
                    "fail",
                    f"test_service_stub marker missing from log\nlog:\n{log_text}\nstdout:\n{stdout}\nstderr:\n{stderr}",
                )
            if not expect_timeout and "service run timeout" in log_text:
                stdout, stderr = stop_process(proc)
                return CaseResult(
                    case_name,
                    "fail",
                    f"unexpected timeout marker\nlog:\n{log_text}\nstdout:\n{stdout}\nstderr:\n{stderr}",
                )

            return CaseResult(case_name, "pass", f"{expected_text}; runtime cleaned")
        finally:
            if proc.poll() is None:
                stop_process(proc)
    finally:
        cleanup_dir(tmp_dir)


def run_service_case(case_name: str, delay_ms: int, timeout_ms: int, expect_success: bool) -> CaseResult:
    service_exe = find_executable("stdiolink_service")
    if service_exe is None:
        return CaseResult(case_name, "fail", format_missing("stdiolink_service"))
    slow_driver_exe = find_executable("test_slow_command_driver")
    if slow_driver_exe is None:
        return CaseResult(case_name, "fail", format_missing("test_slow_command_driver"))

    tmp_dir = Path(tempfile.mkdtemp(prefix="m101a_service_"))
    try:
        service_dir = tmp_dir / "service"
        create_proxy_timeout_service(service_dir)

        config_path = tmp_dir / "config.json"
        write_json(
            config_path,
            {
                "driverPath": str(slow_driver_exe),
                "delayMs": delay_ms,
                "timeoutMs": timeout_ms,
            },
        )

        env = make_env()
        result = subprocess.run(
            [
                str(service_exe),
                str(service_dir),
                f"--config-file={config_path}",
                f"--data-root={tmp_dir}",
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
            if result.returncode != 0:
                return CaseResult(
                    case_name,
                    "fail",
                    f"expected success but exit={result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
                )
            if "command completed" not in result.stderr:
                return CaseResult(
                    case_name,
                    "fail",
                    f"missing success marker in stderr\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
                )
            return CaseResult(case_name, "pass", "command completed")

        if result.returncode == 0:
            return CaseResult(
                case_name,
                "fail",
                f"expected failure but exit=0\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )
        if "Command timeout: delayed_done" not in result.stderr:
            return CaseResult(
                case_name,
                "fail",
                f"missing timeout marker in stderr\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )
        return CaseResult(case_name, "pass", "command timeout observed")
    finally:
        cleanup_dir(tmp_dir)


CASES = {
    "S01": lambda: run_server_case("S01", sleep_ms=5000, run_timeout_ms=200, expect_timeout=True),
    "S02": lambda: run_server_case("S02", sleep_ms=20, run_timeout_ms=1000, expect_timeout=False),
    "S03": lambda: run_service_case("S03", delay_ms=5000, timeout_ms=50, expect_success=False),
    "S04": lambda: run_service_case("S04", delay_ms=20, timeout_ms=1000, expect_success=True),
}


def main() -> int:
    parser = argparse.ArgumentParser(description="M101A timeout smoke tests.")
    parser.add_argument("--case", default="all", choices=["all", *CASES.keys()])
    args = parser.parse_args()

    selected = CASES.keys() if args.case == "all" else [args.case]
    results = [CASES[name]() for name in selected]

    failed = 0
    for result in results:
        print(f"[{result.status.upper()}] {result.name}: {result.detail}")
        if result.status != "pass":
            failed += 1

    print(f"[SUMMARY] total={len(results)} pass={len(results) - failed} fail={failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
