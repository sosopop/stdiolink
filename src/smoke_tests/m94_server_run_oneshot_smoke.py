#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
from dataclasses import dataclass
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[2]
EXE_SUFFIX = ".exe" if os.name == "nt" else ""


@dataclass
class CaseResult:
    name: str
    status: str  # pass | fail | skip
    detail: str


def candidate_bin_dirs() -> list[Path]:
    dirs: list[Path] = []
    env_bin = os.environ.get("STDIOLINK_BIN_DIR")
    if env_bin:
        dirs.append(Path(env_bin))
    dirs.extend(
        [
            ROOT_DIR / "build" / "debug",
            ROOT_DIR / "build" / "release",
            ROOT_DIR / "build" / "runtime_debug" / "bin",
            ROOT_DIR / "build" / "runtime_release" / "bin",
        ]
    )
    return dirs


def find_driver_exe(base_name: str) -> Path | None:
    file_name = f"{base_name}{EXE_SUFFIX}"
    for directory in candidate_bin_dirs():
        path = directory / file_name
        if path.exists():
            return path
    return None


def run_process(args: list[str], timeout_s: float = 12.0) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout_s,
    )


def parse_jsonl_lines(stdout_text: str) -> list[dict]:
    rows: list[dict] = []
    for raw in stdout_text.splitlines():
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


def has_started_event(rows: list[dict]) -> bool:
    for row in rows:
        if row.get("status") != "event":
            continue
        data = row.get("data")
        if isinstance(data, dict) and data.get("event") == "started":
            return True
    return False


def allocate_local_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def case_run_event_only(case_name: str, driver: str, profile: str, args_tail: list[str]) -> CaseResult:
    exe = find_driver_exe(driver)
    if exe is None:
        return CaseResult(case_name, "skip", f"{driver}{EXE_SUFFIX} not found")

    args = [
        str(exe),
        f"--profile={profile}",
        "--mode=console",
        "--cmd=run",
        *args_tail,
    ]
    try:
        result = run_process(args, timeout_s=4.0)
    except subprocess.TimeoutExpired as exc:
        stdout_text = exc.stdout or ""
        stderr_text = exc.stderr or ""
        rows = parse_jsonl_lines(stdout_text)
        if not has_started_event(rows):
            return CaseResult(
                case_name,
                "fail",
                "process stayed alive but missing started event"
                f"\nstdout:\n{stdout_text}\nstderr:\n{stderr_text}",
            )
        return CaseResult(
            case_name,
            "pass",
            "run emitted started event and stayed alive until timeout cleanup",
        )

    rows = parse_jsonl_lines(result.stdout)
    if result.returncode != 0:
        return CaseResult(
            case_name,
            "fail",
            f"exit={result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
        )
    if has_started_event(rows):
        return CaseResult(
            case_name,
            "fail",
            "process exited early after started event (should keep running)"
            f"\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
        )
    return CaseResult(
        case_name,
        "fail",
        "process exited without started event"
        f"\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
    )


def case_tcp_run_oneshot() -> CaseResult:
    port = allocate_local_port()
    return case_run_event_only(
        "tcp_run_oneshot",
        "stdio.drv.modbustcp_server",
        "oneshot",
        [
            "--listen_address=127.0.0.1",
            f"--listen_port={port}",
            '--units=[{"id":1}]',
        ],
    )


def case_rtu_run_oneshot() -> CaseResult:
    port = allocate_local_port()
    return case_run_event_only(
        "rtu_run_oneshot",
        "stdio.drv.modbusrtu_server",
        "oneshot",
        [
            "--listen_address=127.0.0.1",
            f"--listen_port={port}",
            '--units=[{"id":1}]',
        ],
    )


def case_serial_run_oneshot() -> CaseResult:
    exe = find_driver_exe("stdio.drv.modbusrtu_serial_server")
    if exe is None:
        return CaseResult("serial_run_oneshot", "skip", "stdio.drv.modbusrtu_serial_server not found")
    args = [
        str(exe),
        "--profile=oneshot",
        "--mode=console",
        "--cmd=run",
        "--port_name=__NON_EXISTENT_PORT_FOR_SMOKE__",
        '--units=[{"id":1}]',
    ]
    result = run_process(args)
    combined = (result.stdout or "") + (result.stderr or "")
    if result.returncode == 0:
        return CaseResult(
            "serial_run_oneshot",
            "fail",
            f"expected non-zero exit on invalid serial port\n{combined}",
        )
    if "Failed to open serial port" not in combined:
        return CaseResult(
            "serial_run_oneshot",
            "fail",
            f"missing expected serial error message\n{combined}",
        )
    return CaseResult("serial_run_oneshot", "pass", "run error path works on invalid serial port")


def case_tcp_run_keepalive_baseline() -> CaseResult:
    port = allocate_local_port()
    return case_run_event_only(
        "tcp_run_keepalive_baseline",
        "stdio.drv.modbustcp_server",
        "keepalive",
        [
            "--listen_address=127.0.0.1",
            f"--listen_port={port}",
            '--units=[{"id":1}]',
        ],
    )


CASES = {
    "tcp_run_oneshot": case_tcp_run_oneshot,
    "rtu_run_oneshot": case_rtu_run_oneshot,
    "serial_run_oneshot": case_serial_run_oneshot,
    "tcp_run_keepalive_baseline": case_tcp_run_keepalive_baseline,
}


def run_selected(case_name: str) -> list[CaseResult]:
    if case_name == "all":
        return [fn() for fn in CASES.values()]
    fn = CASES.get(case_name)
    if fn is None:
        return [CaseResult(case_name, "fail", "unknown case")]
    return [fn()]


def main() -> int:
    parser = argparse.ArgumentParser(description="M94 server run smoke tests.")
    parser.add_argument("--case", default="all", choices=["all", *CASES.keys()])
    args = parser.parse_args()

    results = run_selected(args.case)
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
