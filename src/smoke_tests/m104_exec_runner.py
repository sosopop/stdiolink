#!/usr/bin/env python3
"""M104 exec_runner smoke tests.

Covers:
  S01 - spawn success with stdout output
  S02 - nonexistent program fails
  S03 - non-zero exit code with default success_exit_codes fails
"""
from __future__ import annotations

import argparse
import os
import platform
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent


@dataclass
class CaseResult:
    name: str
    status: str  # "pass" | "fail"
    detail: str


def _find_runtime_data_root() -> Path | None:
    env_dir = os.environ.get("STDIOLINK_DATA_ROOT")
    candidates = []
    if env_dir:
        candidates.append(Path(env_dir))
    candidates.append(PROJECT_ROOT / "build" / "runtime_release" / "data_root")
    for path in candidates:
        if (path / "services").exists():
            return path
    return None


def _exec_runner_service_dir() -> Path | None:
    data_root = _find_runtime_data_root()
    if data_root is None:
        return None
    service_dir = data_root / "services" / "exec_runner"
    return service_dir if service_dir.exists() else None


def _find_executable(name: str) -> Path | None:
    suffix = ".exe" if platform.system() == "Windows" else ""
    env_dir = os.environ.get("STDIOLINK_BIN_DIR")
    candidates = []
    if env_dir:
        candidates.append(Path(env_dir) / f"{name}{suffix}")
    candidates.append(PROJECT_ROOT / "build" / "runtime_release" / "bin" / f"{name}{suffix}")
    for p in candidates:
        if p.exists():
            return p
    return None


def _inject_dll_paths() -> dict[str, str]:
    env = os.environ.copy()
    candidate_dirs = [
        PROJECT_ROOT / "build" / "runtime_release" / "bin",
    ]
    extra = os.pathsep.join(str(d) for d in candidate_dirs if d.exists())
    if extra:
        env["PATH"] = extra + os.pathsep + env.get("PATH", "")
    return env


def _run(cmd: list[str], env: dict[str, str], timeout: int = 30) -> tuple[int, str, str]:
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout, env=env,
        )
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as e:
        return -1, (e.stdout or ""), (e.stderr or "") + "\n[TIMEOUT]"


def _fail_detail(cmd: list[str], rc: int, stdout: str, stderr: str) -> str:
    return (
        f"cmd: {cmd}\n"
        f"exit_code: {rc}\n"
        f"stdout: {stdout[:500]}\n"
        f"stderr: {stderr[:500]}"
    )


def run_s01() -> CaseResult:
    """S01: spawn success stdout."""
    svc_exe = _find_executable("stdiolink_service")
    stub_exe = _find_executable("test_process_async_stub")
    if svc_exe is None or stub_exe is None:
        return CaseResult("S01", "fail",
                          f"executables not found: service={svc_exe}, stub={stub_exe}")

    svc_dir = _exec_runner_service_dir()
    if svc_dir is None:
        return CaseResult("S01", "fail", "runtime exec_runner service dir not found")

    env = _inject_dll_paths()
    cmd = [
        str(svc_exe), str(svc_dir),
        f"--config.program={stub_exe}",
        "--config.args[0]=--mode=stdout",
        "--config.args[1]=--text=smoke_ok",
    ]
    rc, stdout, stderr = _run(cmd, env)
    if rc != 0:
        return CaseResult("S01", "fail", _fail_detail(cmd, rc, stdout, stderr))
    if "smoke_ok" not in stdout and "smoke_ok" not in stderr:
        return CaseResult("S01", "fail",
                          f"'smoke_ok' not found in output\n{_fail_detail(cmd, rc, stdout, stderr)}")
    return CaseResult("S01", "pass", "spawn success with stdout output verified")


def run_s02() -> CaseResult:
    """S02: nonexistent program fails."""
    svc_exe = _find_executable("stdiolink_service")
    if svc_exe is None:
        return CaseResult("S02", "fail", "stdiolink_service not found")

    svc_dir = _exec_runner_service_dir()
    if svc_dir is None:
        return CaseResult("S02", "fail", "runtime exec_runner service dir not found")
    env = _inject_dll_paths()
    cmd = [
        str(svc_exe), str(svc_dir),
        "--config.program=nonexistent_exec_runner_binary_xyz",
    ]
    rc, stdout, stderr = _run(cmd, env)
    if rc == 0:
        return CaseResult("S02", "fail",
                          f"expected non-zero exit code\n{_fail_detail(cmd, rc, stdout, stderr)}")
    return CaseResult("S02", "pass", f"nonexistent program correctly failed with rc={rc}")


def run_s03() -> CaseResult:
    """S03: non-zero exit code with default success_exit_codes fails."""
    svc_exe = _find_executable("stdiolink_service")
    stub_exe = _find_executable("test_process_async_stub")
    if svc_exe is None or stub_exe is None:
        return CaseResult("S03", "fail",
                          f"executables not found: service={svc_exe}, stub={stub_exe}")

    svc_dir = _exec_runner_service_dir()
    if svc_dir is None:
        return CaseResult("S03", "fail", "runtime exec_runner service dir not found")
    env = _inject_dll_paths()
    cmd = [
        str(svc_exe), str(svc_dir),
        f"--config.program={stub_exe}",
        "--config.args[0]=--mode=stdout",
        "--config.args[1]=--exit-code=9",
    ]
    rc, stdout, stderr = _run(cmd, env)
    if rc == 0:
        return CaseResult("S03", "fail",
                          f"expected non-zero exit code\n{_fail_detail(cmd, rc, stdout, stderr)}")
    return CaseResult("S03", "pass", f"exit code 9 correctly rejected with rc={rc}")


CASES: dict[str, Callable[[], CaseResult]] = {
    "S01": run_s01,
    "S02": run_s02,
    "S03": run_s03,
}


def main() -> int:
    parser = argparse.ArgumentParser(description="M104 exec_runner smoke tests.")
    parser.add_argument("--case", default="all", choices=["all", *CASES.keys()])
    args = parser.parse_args()

    selected = CASES.keys() if args.case == "all" else [args.case]
    results = [CASES[name]() for name in selected]

    failed = 0
    for result in results:
        print(f"[{result.status.upper()}] {result.name}: {result.detail}")
        if result.status != "pass":
            failed += 1

    print(f"\n[SUMMARY] total={len(results)} pass={len(results) - failed} fail={failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
