#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import socket
import struct
import subprocess
import time
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
    dirs.append(ROOT_DIR / "build" / "runtime_release" / "bin")
    return dirs


def find_driver_exe(base_name: str) -> Path | None:
    name = f"{base_name}{EXE_SUFFIX}"
    for folder in candidate_bin_dirs():
        path = folder / name
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


def parse_jsonl(stdout_text: str) -> list[dict]:
    rows: list[dict] = []
    for raw in stdout_text.splitlines():
        line = raw.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(obj, dict):
            rows.append(obj)
    return rows


def allocate_local_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def read_exact(sock: socket.socket, size: int) -> bytes:
    chunks: list[bytes] = []
    total = 0
    while total < size:
        chunk = sock.recv(size - total)
        if not chunk:
            raise RuntimeError(f"socket closed while reading {size} bytes")
        chunks.append(chunk)
        total += len(chunk)
    return b"".join(chunks)


def modbus_request(host: str, port: int, unit_id: int, function_code: int, pdu: bytes) -> tuple[int, bytes]:
    with socket.create_connection((host, port), timeout=2.0) as sock:
        tx_id = 1
        header = struct.pack(">HHHB", tx_id, 0, len(pdu) + 2, unit_id)
        frame = header + bytes([function_code]) + pdu
        sock.sendall(frame)

        mbap = read_exact(sock, 7)
        _, _, length, _ = struct.unpack(">HHHB", mbap)
        body = read_exact(sock, length - 1)
        return body[0], body[1:]


def write_single_register(host: str, port: int, unit_id: int, address: int, value: int) -> bool:
    fc, data = modbus_request(host, port, unit_id, 0x06, struct.pack(">HH", address, value))
    return fc == 0x06 and data == struct.pack(">HH", address, value)


def read_holding_registers(host: str, port: int, unit_id: int, address: int, count: int) -> list[int]:
    fc, data = modbus_request(host, port, unit_id, 0x03, struct.pack(">HH", address, count))
    if fc != 0x03 or not data:
        return []
    expected_bytes = count * 2
    byte_count = data[0]
    if byte_count != expected_bytes or len(data) < 1 + expected_bytes:
        return []
    payload = data[1:1 + byte_count]
    regs: list[int] = []
    for i in range(count):
        offset = i * 2
        regs.append((payload[offset] << 8) | payload[offset + 1])
    return regs


def has_started_event(rows: list[dict]) -> bool:
    for row in rows:
        if row.get("status") != "event":
            continue
        data = row.get("data")
        if isinstance(data, dict) and data.get("event") == "started":
            return True
    return False


def case_s01_export_meta_single_run() -> CaseResult:
    exe = find_driver_exe("stdio.drv.plc_crane_sim")
    if exe is None:
        return CaseResult("S01_export_meta", "skip", "stdio.drv.plc_crane_sim not found")
    proc = run_process([str(exe), "--export-meta"], timeout_s=10.0)
    if proc.returncode != 0:
        return CaseResult("S01_export_meta", "fail", f"exit={proc.returncode}\n{proc.stderr}")
    try:
        meta = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        return CaseResult("S01_export_meta", "fail", f"invalid json: {exc}")
    commands = meta.get("commands")
    if not isinstance(commands, list):
        return CaseResult("S01_export_meta", "fail", "commands field missing or invalid")
    if len(commands) != 1 or commands[0].get("name") != "run":
        return CaseResult("S01_export_meta", "fail", f"unexpected commands: {commands}")
    return CaseResult("S01_export_meta", "pass", "meta only contains run command")


def case_s02_run_started_event_stream() -> CaseResult:
    exe = find_driver_exe("stdio.drv.plc_crane_sim")
    if exe is None:
        return CaseResult("S02_run_started", "skip", "stdio.drv.plc_crane_sim not found")
    port = allocate_local_port()
    args = [
        str(exe),
        "--profile=oneshot",
        "--mode=console",
        "--cmd=run",
        "--listen_address=127.0.0.1",
        f"--listen_port={port}",
    ]
    try:
        proc = run_process(args, timeout_s=4.0)
    except subprocess.TimeoutExpired as exc:
        rows = parse_jsonl(exc.stdout or "")
        if not has_started_event(rows):
            return CaseResult("S02_run_started", "fail", "missing started event before timeout")
        if any(row.get("status") == "done" for row in rows):
            return CaseResult("S02_run_started", "fail", "unexpected done response in run stream")
        return CaseResult("S02_run_started", "pass", "run emitted started event and kept streaming")

    rows = parse_jsonl(proc.stdout)
    return CaseResult(
        "S02_run_started",
        "fail",
        f"process exited unexpectedly rc={proc.returncode}\nstdout={proc.stdout}\nstderr={proc.stderr}\nrows={rows}",
    )


def case_s03_modbus_rw_path() -> CaseResult:
    exe = find_driver_exe("stdio.drv.plc_crane_sim")
    if exe is None:
        return CaseResult("S03_modbus_rw", "skip", "stdio.drv.plc_crane_sim not found")

    port = allocate_local_port()
    proc = subprocess.Popen(
        [
            str(exe),
            "--profile=oneshot",
            "--mode=console",
            "--cmd=run",
            "--listen_address=127.0.0.1",
            f"--listen_port={port}",
            "--unit_id=1",
            "--cylinder_up_delay=20",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )

    try:
        deadline = time.time() + 4.0
        connected = False
        while time.time() < deadline:
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                    connected = True
                    break
            except OSError:
                time.sleep(0.05)

        if not connected:
            return CaseResult("S03_modbus_rw", "fail", "driver port not reachable in time")

        if not write_single_register("127.0.0.1", port, 1, 0, 1):
            return CaseResult("S03_modbus_rw", "fail", "write HR[0]=1 failed")

        time.sleep(0.15)
        regs = read_holding_registers("127.0.0.1", port, 1, 9, 2)
        if len(regs) < 2 or regs[0] != 1 or regs[1] != 0:
            return CaseResult("S03_modbus_rw", "fail", f"unexpected HR[9..10] values: {regs}")

        return CaseResult("S03_modbus_rw", "pass", "modbus write/read main path works")
    finally:
        proc.terminate()
        try:
            proc.communicate(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate(timeout=3.0)


def case_s04_invalid_arg_exit_code() -> CaseResult:
    exe = find_driver_exe("stdio.drv.plc_crane_sim")
    if exe is None:
        return CaseResult("S04_invalid_arg", "skip", "stdio.drv.plc_crane_sim not found")
    proc = run_process(
        [str(exe), "--mode=console", "--cmd=run", "--listen_port=70000"],
        timeout_s=10.0,
    )
    if proc.returncode not in (3, 400):
        return CaseResult(
            "S04_invalid_arg",
            "fail",
            f"expected exit 3/400, got {proc.returncode}\nstdout={proc.stdout}\nstderr={proc.stderr}",
        )
    return CaseResult("S04_invalid_arg", "pass", "invalid run param exits with error code")


def case_s05_port_conflict_error() -> CaseResult:
    exe = find_driver_exe("stdio.drv.plc_crane_sim")
    if exe is None:
        return CaseResult("S05_port_conflict", "skip", "stdio.drv.plc_crane_sim not found")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as blocker:
        blocker.bind(("127.0.0.1", 0))
        blocker.listen(1)
        port = blocker.getsockname()[1]

        proc = run_process(
            [
                str(exe),
                "--mode=console",
                "--cmd=run",
                "--listen_address=127.0.0.1",
                f"--listen_port={port}",
            ],
            timeout_s=10.0,
        )

    rows = parse_jsonl(proc.stdout)
    has_error = any(
        row.get("status") == "error" and int(row.get("code", -1)) == 1
        for row in rows
    )
    if not has_error:
        return CaseResult(
            "S05_port_conflict",
            "fail",
            f"expected error code 1, got stdout={proc.stdout}, stderr={proc.stderr}",
        )
    return CaseResult("S05_port_conflict", "pass", "run reports startup failure on port conflict")


CASES = {
    "s01_export_meta": case_s01_export_meta_single_run,
    "s02_run_started": case_s02_run_started_event_stream,
    "s03_modbus_rw": case_s03_modbus_rw_path,
    "s04_invalid_arg": case_s04_invalid_arg_exit_code,
    "s05_port_conflict": case_s05_port_conflict_error,
}


def run_selected(case_name: str) -> list[CaseResult]:
    if case_name == "all":
        return [fn() for fn in CASES.values()]
    fn = CASES.get(case_name)
    if fn is None:
        return [CaseResult(case_name, "fail", "unknown case")]
    return [fn()]


def main() -> int:
    parser = argparse.ArgumentParser(description="M97 PLC crane sim smoke tests")
    parser.add_argument("--case", default="all", choices=["all", *CASES.keys()])
    args = parser.parse_args()

    results = run_selected(args.case)
    failed = 0
    skipped = 0
    for item in results:
        if item.status == "pass":
            print(f"[PASS] {item.name}: {item.detail}")
        elif item.status == "skip":
            skipped += 1
            print(f"[SKIP] {item.name}: {item.detail}")
        else:
            failed += 1
            print(f"[FAIL] {item.name}: {item.detail}")

    passed = len(results) - failed - skipped
    print(f"[SUMMARY] total={len(results)} pass={passed} skip={skipped} fail={failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
