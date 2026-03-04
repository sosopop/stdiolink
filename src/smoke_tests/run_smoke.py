#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
PLAN_SCRIPTS = {
    "m94_server_run_oneshot": SCRIPT_DIR / "m94_server_run_oneshot_smoke.py",
    "m95_driver_examples": SCRIPT_DIR / "m95_driver_examples.py",
}
LEGACY_MILESTONE_ALIASES = {
    "94": "m94_server_run_oneshot",
    "95": "m95_driver_examples",
}


def resolve_plan_name(raw_plan: str) -> str:
    return LEGACY_MILESTONE_ALIASES.get(raw_plan, raw_plan)


def run_plan(plan_name: str, case: str | None) -> int:
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
    result = subprocess.run(cmd, check=False)
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

    selected = args.plan
    if selected is None:
        selected = args.milestone
        print("[WARN] --milestone is deprecated; use --plan instead.")

    if selected == "all":
        exit_code = 0
        for plan_name in sorted(PLAN_SCRIPTS.keys()):
            rc = run_plan(plan_name, None)
            if rc != 0:
                exit_code = rc
        return exit_code

    return run_plan(selected, args.case)


if __name__ == "__main__":
    raise SystemExit(main())
