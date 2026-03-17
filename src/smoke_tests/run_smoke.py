#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent
PLAN_SCRIPTS = {
    "m102_webui_i18n_alignment": SCRIPT_DIR / "m102_webui_i18n_alignment.py",
    "m98_json_cli_normalization": SCRIPT_DIR / "m98_json_cli_normalization.py",
    "m99_cli_example_unification": SCRIPT_DIR / "m99_cli_example_unification.py",
    "m100_cli_docs_and_migration": SCRIPT_DIR / "m100_cli_docs_and_migration.py",
    "m101a_service_driver_timeout": SCRIPT_DIR / "m101a_service_driver_timeout.py",
    "m101_bin_scan_orchestrator": SCRIPT_DIR / "m101_bin_scan_orchestrator.py",
    "m94_server_run_oneshot": SCRIPT_DIR / "m94_server_run_oneshot_smoke.py",
    "m95_driver_examples": SCRIPT_DIR / "m95_driver_examples.py",
    "m96_3dvision_ws": SCRIPT_DIR / "m96_3dvision_ws.py",
    "m97_plc_crane_sim": SCRIPT_DIR / "m97_plc_crane_sim_smoke.py",
    "m103_3d_scan_robot": SCRIPT_DIR / "m103_3d_scan_robot.py",
    "m104_exec_runner": SCRIPT_DIR / "m104_exec_runner.py",
    "m105_pqw_analog_output": SCRIPT_DIR / "m105_pqw_analog_output.py",
}
LEGACY_MILESTONE_ALIASES = {
    "102": "m102_webui_i18n_alignment",
    "98": "m98_json_cli_normalization",
    "99": "m99_cli_example_unification",
    "100": "m100_cli_docs_and_migration",
    "101a": "m101a_service_driver_timeout",
    "101bin": "m101_bin_scan_orchestrator",
    "94": "m94_server_run_oneshot",
    "95": "m95_driver_examples",
    "96": "m96_3dvision_ws",
    "97": "m97_plc_crane_sim",
    "103": "m103_3d_scan_robot",
    "104": "m104_exec_runner",
    "105": "m105_pqw_analog_output",
}


def _inject_dll_paths() -> dict[str, str]:
    """将所有候选 bin 目录注入 PATH，确保子进程能找到 Qt/依赖 DLL。"""
    env = os.environ.copy()
    candidate_dirs = [
        PROJECT_ROOT / "build" / "runtime_debug" / "bin",
        PROJECT_ROOT / "build" / "runtime_release" / "bin",
        PROJECT_ROOT / "build" / "debug",
        PROJECT_ROOT / "build" / "release",
    ]
    extra = os.pathsep.join(str(d) for d in candidate_dirs if d.exists())
    if extra:
        env["PATH"] = extra + os.pathsep + env.get("PATH", "")
    return env


def resolve_plan_name(raw_plan: str) -> str:
    return LEGACY_MILESTONE_ALIASES.get(raw_plan, raw_plan)


def run_plan(plan_name: str, case: str | None, env: dict[str, str]) -> int:
    resolved = resolve_plan_name(plan_name)
    script = PLAN_SCRIPTS.get(resolved)
    if script is None:
        known = ", ".join(sorted(PLAN_SCRIPTS.keys()))
        print(f"[FAIL] Unknown plan: {plan_name}. Known plans: {known}")
        return 1

    cmd = [sys.executable, str(script)]
    if case:
        cmd.extend(["--case", case])

    print(f"[INFO] Running plan {resolved}: {' '.join(cmd)}")
    result = subprocess.run(cmd, check=False, env=env)
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Run smoke test plans.")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument(
        "--plan",
        help="Test plan name, e.g. m94_server_run_oneshot, or 'all'",
    )
    group.add_argument(
        "--milestone",
        help="Deprecated alias. Milestone number, e.g. 94, or 'all'",
    )
    parser.add_argument("--case", help="Run a single case in the selected test plan script")
    args = parser.parse_args()

    env = _inject_dll_paths()

    selected = args.plan
    if selected is None:
        selected = args.milestone
        print("[WARN] --milestone is deprecated; use --plan instead.")

    if selected == "all":
        exit_code = 0
        for plan_name in sorted(PLAN_SCRIPTS.keys()):
            rc = run_plan(plan_name, None, env)
            if rc != 0:
                exit_code = rc
        return exit_code

    return run_plan(selected, args.case, env)


if __name__ == "__main__":
    raise SystemExit(main())
