#!/usr/bin/env python3
"""M103 - 3D Scan Robot Driver smoke tests.

Since the real device (hardware) is not available in CI, these tests exercise
the driver binary in ways that do not require a physical serial port:
S01 - export-meta produces valid JSON with all expected commands
S02 - console oneshot status returns done+ready
S03 - console oneshot with a missing-port command returns transport error code
S04 - unknown command returns 404
S05 - meta schema version and required fields are present
"""
from __future__ import annotations

import argparse
import json
import os
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
    dirs.extend([
        ROOT_DIR / "build" / "debug",
        ROOT_DIR / "build" / "release",
        ROOT_DIR / "build" / "runtime_debug" / "bin",
        ROOT_DIR / "build" / "runtime_release" / "bin",
    ])
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


DRIVER_NAME = "stdio.drv.3d_scan_robot"

EXPECTED_COMMANDS = [
    "status", "test", "get_addr", "set_addr",
    "get_mode", "set_mode", "get_temp", "get_state",
    "get_version", "get_angles", "get_switch_x", "get_switch_y",
    "get_calib_x", "get_calib_y", "calib", "calib_x", "calib_y",
    "move", "get_distance_at", "get_distance", "get_reg", "set_reg",
    "scan_line", "scan_frame", "get_data", "query",
    "interrupt_test", "scan_progress", "scan_cancel",
]


def case_s01_export_meta() -> CaseResult:
    exe = find_driver_exe(DRIVER_NAME)
    if exe is None:
        return CaseResult("S01_export_meta", "skip", f"{DRIVER_NAME} not found")
    proc = run_process([str(exe), "--export-meta"], timeout_s=10.0)
    if proc.returncode != 0:
        return CaseResult("S01_export_meta", "fail", f"exit={proc.returncode}\n{proc.stderr}")
    try:
        meta = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        return CaseResult("S01_export_meta", "fail", f"invalid json: {exc}")
    commands = meta.get("commands")
    if not isinstance(commands, list):
        return CaseResult("S01_export_meta", "fail", "commands field missing or not a list")
    cmd_names = {c.get("name") for c in commands if isinstance(c, dict)}
    missing = [n for n in EXPECTED_COMMANDS if n not in cmd_names]
    if missing:
        return CaseResult("S01_export_meta", "fail", f"missing commands: {missing}")
    return CaseResult("S01_export_meta", "pass", f"meta contains {len(commands)} commands")


def case_s02_status_command() -> CaseResult:
    exe = find_driver_exe(DRIVER_NAME)
    if exe is None:
        return CaseResult("S02_status", "skip", f"{DRIVER_NAME} not found")
    proc = run_process(
        [str(exe), "--profile=oneshot", "--mode=console", "--cmd=status"],
        timeout_s=10.0,
    )
    if proc.returncode != 0:
        return CaseResult("S02_status", "fail", f"exit={proc.returncode}\n{proc.stderr}")
    rows = parse_jsonl(proc.stdout)
    done_rows = [r for r in rows if r.get("status") == "done"]
    if not done_rows:
        return CaseResult("S02_status", "fail", f"no done response\nstdout={proc.stdout}")
    data = done_rows[0].get("data", {})
    if data.get("status") != "ready":
        return CaseResult("S02_status", "fail", f"unexpected data: {data}")
    return CaseResult("S02_status", "pass", "status returns done+ready")


def case_s03_transport_error() -> CaseResult:
    """Calling a device command with a non-existent serial port should return error code 1."""
    exe = find_driver_exe(DRIVER_NAME)
    if exe is None:
        return CaseResult("S03_transport_error", "skip", f"{DRIVER_NAME} not found")
    proc = run_process(
        [
            str(exe),
            "--profile=oneshot",
            "--mode=console",
            "--cmd=test",
            "--port=COM_NONEXISTENT_99",
            "--addr=1",
            "--value=1234",
        ],
        timeout_s=10.0,
    )
    rows = parse_jsonl(proc.stdout)
    err_rows = [r for r in rows if r.get("status") == "error"]
    if not err_rows:
        return CaseResult("S03_transport_error", "fail", f"no error response\nstdout={proc.stdout}")
    code = err_rows[0].get("code")
    if code != 1:
        return CaseResult("S03_transport_error", "fail", f"expected code=1, got {code}")
    return CaseResult("S03_transport_error", "pass", "transport error returns code=1")


def case_s04_unknown_command() -> CaseResult:
    exe = find_driver_exe(DRIVER_NAME)
    if exe is None:
        return CaseResult("S04_unknown_cmd", "skip", f"{DRIVER_NAME} not found")
    proc = run_process(
        [
            str(exe),
            "--profile=oneshot",
            "--mode=console",
            "--cmd=not_a_real_cmd",
        ],
        timeout_s=10.0,
    )
    rows = parse_jsonl(proc.stdout)
    err_rows = [r for r in rows if r.get("status") == "error"]
    if not err_rows:
        return CaseResult("S04_unknown_cmd", "fail", f"no error response\nstdout={proc.stdout}")
    code = err_rows[0].get("code")
    if code != 404:
        return CaseResult("S04_unknown_cmd", "fail", f"expected code=404, got {code}")
    return CaseResult("S04_unknown_cmd", "pass", "unknown command returns code=404")


def case_s05_meta_schema_fields() -> CaseResult:
    exe = find_driver_exe(DRIVER_NAME)
    if exe is None:
        return CaseResult("S05_meta_schema", "skip", f"{DRIVER_NAME} not found")
    proc = run_process([str(exe), "--export-meta"], timeout_s=10.0)
    if proc.returncode != 0:
        return CaseResult("S05_meta_schema", "fail", f"exit={proc.returncode}")
    try:
        meta = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        return CaseResult("S05_meta_schema", "fail", f"invalid json: {exc}")
    schema_ver = meta.get("schemaVersion")
    if schema_ver != "1.0":
        return CaseResult("S05_meta_schema", "fail", f"schemaVersion={schema_ver}")
    driver_info = meta.get("info")
    if not isinstance(driver_info, dict):
        return CaseResult("S05_meta_schema", "fail", "missing info field")
    if driver_info.get("id") != DRIVER_NAME:
        return CaseResult("S05_meta_schema", "fail", f"info.id={driver_info.get('id')}")
    return CaseResult("S05_meta_schema", "pass", "meta has correct schema version and driver id")


CASES = {
    "s01_export_meta": case_s01_export_meta,
    "s02_status": case_s02_status_command,
    "s03_transport_error": case_s03_transport_error,
    "s04_unknown_cmd": case_s04_unknown_command,
    "s05_meta_schema": case_s05_meta_schema_fields,
}


def run_selected(case_name: str) -> list[CaseResult]:
    if case_name == "all":
        return [fn() for fn in CASES.values()]
    fn = CASES.get(case_name)
    if fn is None:
        return [CaseResult(case_name, "fail", "unknown case")]
    return [fn()]


def main() -> int:
    parser = argparse.ArgumentParser(description="M103 3D Scan Robot smoke tests")
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
