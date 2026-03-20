#!/usr/bin/env python3
"""M106 - 3D temperature scanner Driver smoke tests."""
from __future__ import annotations

import argparse
import json
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parents[2]
EXE_SUFFIX = ".exe" if os.name == "nt" else ""
DRIVER_NAME = "stdio.drv.3d_temp_scanner"
EXPECTED_COMMANDS = {"status", "capture"}


@dataclass
class CaseResult:
    name: str
    status: str
    detail: str


def candidate_bin_dirs() -> list[Path]:
    dirs: list[Path] = []
    env_bin = os.environ.get("STDIOLINK_BIN_DIR")
    if env_bin:
        dirs.append(Path(env_bin).resolve())
    dirs.append((ROOT_DIR / "build" / "runtime_release" / "bin").resolve())
    dirs.append((ROOT_DIR / "build" / "runtime_debug" / "bin").resolve())
    return dirs


def candidate_runtime_roots() -> list[Path]:
    roots: list[Path] = []
    env_bin = os.environ.get("STDIOLINK_BIN_DIR")
    if env_bin:
        roots.append(Path(env_bin).resolve().parent)
    roots.append((ROOT_DIR / "build" / "runtime_release").resolve())
    roots.append((ROOT_DIR / "build" / "runtime_debug").resolve())
    return roots


def make_env() -> dict[str, str]:
    env = os.environ.copy()
    extra = os.pathsep.join(str(d) for d in candidate_bin_dirs() if d.exists())
    if extra:
        env["PATH"] = extra + os.pathsep + env.get("PATH", "")
    return env


def find_driver_exe(base_name: str) -> Path | None:
    name = f"{base_name}{EXE_SUFFIX}"
    for runtime_root in candidate_runtime_roots():
        path = runtime_root / "data_root" / "drivers" / base_name / name
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
        env=make_env(),
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


def case_s01_meta_describe() -> CaseResult:
    exe = find_driver_exe(DRIVER_NAME)
    if exe is None:
        return CaseResult("S01_meta_describe", "skip", f"{DRIVER_NAME} not found")
    proc = run_process([str(exe), "--mode=console", "--cmd=meta.describe"])
    if proc.returncode != 0:
        return CaseResult("S01_meta_describe", "fail", f"exit={proc.returncode}\n{proc.stderr}")
    rows = parse_jsonl(proc.stdout)
    done_rows = [r for r in rows if r.get("status") == "done" and isinstance(r.get("data"), dict)]
    if not done_rows:
        return CaseResult("S01_meta_describe", "fail", f"missing done payload\nstdout={proc.stdout}")
    meta = done_rows[-1]["data"]
    commands = meta.get("commands")
    if not isinstance(commands, list):
        return CaseResult("S01_meta_describe", "fail", "commands field missing")
    names = {item.get("name") for item in commands if isinstance(item, dict)}
    missing = sorted(EXPECTED_COMMANDS - names)
    if missing:
        return CaseResult("S01_meta_describe", "fail", f"missing commands: {missing}")
    extras = sorted(names - EXPECTED_COMMANDS)
    if extras:
        return CaseResult("S01_meta_describe", "fail", f"unexpected commands: {extras}")
    return CaseResult("S01_meta_describe", "pass", "meta.describe returned expected commands")


def case_s02_export_meta() -> CaseResult:
    exe = find_driver_exe(DRIVER_NAME)
    if exe is None:
        return CaseResult("S02_export_meta", "skip", f"{DRIVER_NAME} not found")
    proc = run_process([str(exe), "--export-meta"])
    if proc.returncode != 0:
        return CaseResult("S02_export_meta", "fail", f"exit={proc.returncode}\n{proc.stderr}")
    try:
        meta = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        return CaseResult("S02_export_meta", "fail", f"invalid json: {exc}")
    if meta.get("schemaVersion") != "1.0":
        return CaseResult("S02_export_meta", "fail", f"schemaVersion={meta.get('schemaVersion')}")
    if meta.get("info", {}).get("id") != DRIVER_NAME:
        return CaseResult("S02_export_meta", "fail", f"info.id={meta.get('info', {}).get('id')}")
    return CaseResult("S02_export_meta", "pass", "export-meta returned valid metadata")


def case_s03_status() -> CaseResult:
    exe = find_driver_exe(DRIVER_NAME)
    if exe is None:
        return CaseResult("S03_status", "skip", f"{DRIVER_NAME} not found")
    proc = run_process([str(exe), "--profile=oneshot", "--mode=console", "--cmd=status"])
    if proc.returncode != 0:
        return CaseResult("S03_status", "fail", f"exit={proc.returncode}\n{proc.stderr}")
    rows = parse_jsonl(proc.stdout)
    done_rows = [r for r in rows if r.get("status") == "done"]
    if not done_rows:
        return CaseResult("S03_status", "fail", f"no done row\nstdout={proc.stdout}")
    if done_rows[0].get("data", {}).get("status") != "ready":
        return CaseResult("S03_status", "fail", f"unexpected payload: {done_rows[0].get('data')}")
    return CaseResult("S03_status", "pass", "status returns done+ready")


def case_s04_capture_validation() -> CaseResult:
    exe = find_driver_exe(DRIVER_NAME)
    if exe is None:
        return CaseResult("S04_capture_validation", "skip", f"{DRIVER_NAME} not found")
    proc = run_process([
        str(exe),
        "--profile=oneshot",
        "--mode=console",
        "--cmd=capture",
    ])
    rows = parse_jsonl(proc.stdout)
    err_rows = [r for r in rows if r.get("status") == "error"]
    if not err_rows:
        return CaseResult("S04_capture_validation", "fail", f"no error row\nstdout={proc.stdout}")
    if err_rows[0].get("code") != 3:
        return CaseResult("S04_capture_validation", "fail", f"expected code=3, got {err_rows[0].get('code')}")
    return CaseResult("S04_capture_validation", "pass", "capture validates missing serial params before hardware access")


def case_s05_unknown_command() -> CaseResult:
    exe = find_driver_exe(DRIVER_NAME)
    if exe is None:
        return CaseResult("S05_unknown_command", "skip", f"{DRIVER_NAME} not found")
    proc = run_process([
        str(exe),
        "--profile=oneshot",
        "--mode=console",
        "--cmd=not_a_real_cmd",
    ])
    rows = parse_jsonl(proc.stdout)
    err_rows = [r for r in rows if r.get("status") == "error"]
    if not err_rows:
        return CaseResult("S05_unknown_command", "fail", f"no error row\nstdout={proc.stdout}")
    if err_rows[0].get("code") != 404:
        return CaseResult("S05_unknown_command", "fail", f"expected code=404, got {err_rows[0].get('code')}")
    return CaseResult("S05_unknown_command", "pass", "unknown command returns 404")


CASES = {
    "s01_meta_describe": case_s01_meta_describe,
    "s02_export_meta": case_s02_export_meta,
    "s03_status": case_s03_status,
    "s04_capture_validation": case_s04_capture_validation,
    "s05_unknown_command": case_s05_unknown_command,
}


def main() -> int:
    parser = argparse.ArgumentParser(description="M106 3D temperature scanner smoke tests")
    parser.add_argument("--case", default="all", choices=["all", *CASES.keys()])
    args = parser.parse_args()

    selected = CASES.values() if args.case == "all" else [CASES[args.case]]
    results = [case_fn() for case_fn in selected]

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
