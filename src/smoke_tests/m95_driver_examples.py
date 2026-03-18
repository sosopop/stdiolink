#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[2]
EXE_SUFFIX = ".exe" if os.name == "nt" else ""

DRIVERS: dict[str, int] = {
    "stdio.drv.plc_crane": 6,
    "stdio.drv.modbustcp": 10,
    "stdio.drv.modbusrtu": 10,
    "stdio.drv.modbusrtu_serial": 10,
    "stdio.drv.modbustcp_server": 17,
    "stdio.drv.modbusrtu_server": 17,
    "stdio.drv.modbusrtu_serial_server": 17,
    "stdio.drv.3dvision": 43,
    "stdio.drv.pqw_analog_output": 8,
}

CONSOLE_EXCEPTION_ALLOWLIST: set[tuple[str, str]] = set()
CONSOLE_COVERAGE_MIN = 0.8


@dataclass
class DriverMetaResult:
    driver: str
    status: str  # pass|skip|fail
    detail: str
    meta: dict | None = None


def candidate_bin_dirs() -> list[Path]:
    dirs: list[Path] = []
    env_bin = os.environ.get("STDIOLINK_BIN_DIR")
    if env_bin:
        dirs.append(Path(env_bin).resolve())
    dirs.append((ROOT_DIR / "build" / "runtime_release" / "bin").resolve())
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


def find_driver_exe(base_name: str) -> Path | None:
    file_name = f"{base_name}{EXE_SUFFIX}"
    for runtime_root in candidate_runtime_roots():
        path = runtime_root / "data_root" / "drivers" / base_name / file_name
        if path.exists():
            return path
    return None


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


def parse_jsonl_objects(text: str) -> list[dict]:
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


def fetch_driver_meta(driver: str, exe: Path) -> DriverMetaResult:
    args = [str(exe), "--mode=console", "--cmd=meta.describe"]
    proc = run_process(args, timeout_s=20.0)
    if proc.returncode != 0:
        return DriverMetaResult(
            driver,
            "fail",
            f"meta.describe exit={proc.returncode}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}",
        )
    rows = parse_jsonl_objects(proc.stdout)
    done_rows = [r for r in rows if r.get("status") == "done" and isinstance(r.get("data"), dict)]
    if not done_rows:
        return DriverMetaResult(
            driver,
            "fail",
            f"meta.describe missing done payload\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}",
        )
    return DriverMetaResult(driver, "pass", "ok", meta=done_rows[-1]["data"])


def validate_examples(driver: str, meta: dict) -> list[str]:
    failures: list[str] = []
    commands = meta.get("commands")
    if not isinstance(commands, list):
        return [f"{driver}: commands is not a list"]

    expected = DRIVERS[driver]
    if len(commands) != expected:
        failures.append(f"{driver}: command count mismatch expected={expected} actual={len(commands)}")

    console_total = 0
    total = 0
    for cmd in commands:
        if not isinstance(cmd, dict):
            failures.append(f"{driver}: command entry is not object")
            continue
        cmd_name = str(cmd.get("name", ""))
        total += 1
        examples = cmd.get("examples")
        if not isinstance(examples, list) or not examples:
            failures.append(f"{driver}/{cmd_name}: examples empty")
            continue

        has_stdio = False
        has_console = False
        for idx, ex in enumerate(examples):
            if not isinstance(ex, dict):
                failures.append(f"{driver}/{cmd_name}: examples[{idx}] not object")
                continue
            if not isinstance(ex.get("description"), str) or not ex["description"].strip():
                failures.append(f"{driver}/{cmd_name}: examples[{idx}] missing description")
            mode = ex.get("mode")
            if not isinstance(mode, str) or not mode:
                failures.append(f"{driver}/{cmd_name}: examples[{idx}] missing mode")
            if mode == "stdio":
                has_stdio = True
            if mode == "console":
                has_console = True
            if not isinstance(ex.get("params"), dict):
                failures.append(f"{driver}/{cmd_name}: examples[{idx}] params must be object")

        if not has_stdio:
            failures.append(f"{driver}/{cmd_name}: no stdio example")
        if has_console:
            console_total += 1
        elif (driver, cmd_name) not in CONSOLE_EXCEPTION_ALLOWLIST:
            failures.append(f"{driver}/{cmd_name}: no console example and not in allowlist")

    coverage = (console_total / total) if total else 0.0
    if coverage < CONSOLE_COVERAGE_MIN:
        failures.append(
            f"{driver}: console coverage {coverage:.2%} < {CONSOLE_COVERAGE_MIN:.0%}"
        )
    return failures


def validate_export_doc(driver: str, exe: Path) -> list[str]:
    failures: list[str] = []
    formats = ["markdown", "openapi", "html", "ts"]
    for fmt in formats:
        proc = run_process([str(exe), f"--export-doc={fmt}"], timeout_s=30.0)
        if proc.returncode != 0:
            failures.append(f"{driver}: --export-doc={fmt} failed exit={proc.returncode}")
            continue
        out = proc.stdout
        if fmt == "markdown" and "#### Examples" not in out:
            failures.append(f"{driver}: markdown missing Examples section")
        if fmt == "html" and "<h4>Examples</h4>" not in out:
            failures.append(f"{driver}: html missing Examples block")
        if fmt == "ts" and "@example <program> --cmd=" not in out:
            failures.append(f"{driver}: ts missing @example")
        if fmt == "openapi":
            try:
                api = json.loads(out)
            except json.JSONDecodeError:
                failures.append(f"{driver}: openapi output is not valid JSON")
                continue
            paths = api.get("paths", {})
            found = False
            if isinstance(paths, dict):
                for _, path_obj in paths.items():
                    if not isinstance(path_obj, dict):
                        continue
                    post = path_obj.get("post")
                    if isinstance(post, dict) and "x-stdiolink-examples" in post:
                        found = True
                        break
            if not found:
                failures.append(f"{driver}: openapi missing x-stdiolink-examples")
    return failures


def validate_command_help(driver: str, exe: Path, meta: dict) -> list[str]:
    failures: list[str] = []
    commands = meta.get("commands")
    if not isinstance(commands, list) or not commands:
        return [f"{driver}: no commands for help validation"]
    cmd_name = commands[0].get("name")
    if not isinstance(cmd_name, str) or not cmd_name:
        return [f"{driver}: first command has invalid name"]
    proc = run_process([str(exe), f"--cmd={cmd_name}", "--help"], timeout_s=15.0)
    if proc.returncode != 0:
        failures.append(f"{driver}: command help failed for {cmd_name}")
        return failures
    combined = (proc.stdout or "") + "\n" + (proc.stderr or "")
    if "Examples:" not in combined:
        failures.append(f"{driver}: command help missing Examples section for {cmd_name}")
    return failures


def run_plan() -> int:
    meta_results: list[DriverMetaResult] = []
    failures: list[str] = []
    skipped = 0

    for driver in DRIVERS.keys():
        exe = find_driver_exe(driver)
        if exe is None:
            skipped += 1
            print(f"[SKIP] {driver}: executable not found")
            continue
        meta_result = fetch_driver_meta(driver, exe)
        meta_results.append(meta_result)
        if meta_result.status != "pass":
            failures.append(meta_result.detail)
            print(f"[FAIL] {driver}: {meta_result.detail}")
            continue
        print(f"[PASS] {driver}: meta.describe")

        assert meta_result.meta is not None
        failures.extend(validate_examples(driver, meta_result.meta))
        failures.extend(validate_export_doc(driver, exe))
        failures.extend(validate_command_help(driver, exe, meta_result.meta))

    for item in failures:
        print(f"[FAIL] {item}")

    checked = len(DRIVERS) - skipped
    if checked == 0:
        print("[SUMMARY] all drivers skipped")
        return 0

    status = "PASS" if not failures else "FAIL"
    print(
        f"[SUMMARY] {status} drivers_checked={checked} skipped={skipped} failures={len(failures)}"
    )
    return 0 if not failures else 1


def main() -> int:
    parser = argparse.ArgumentParser(description="M95 driver examples smoke tests.")
    parser.add_argument("--case", default="all", choices=["all"])
    _ = parser.parse_args()
    return run_plan()


if __name__ == "__main__":
    raise SystemExit(main())
