#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[2]
LOCALES_DIR = ROOT_DIR / "src" / "webui" / "src" / "locales"
BASELINE_FILE = LOCALES_DIR / "en.json"


def load_json(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


def typename(value: object) -> str:
    if isinstance(value, dict):
        return "object"
    if isinstance(value, list):
        return "array"
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "bool"
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return "number"
    if isinstance(value, str):
        return "string"
    return type(value).__name__


def compare_nodes(
    baseline: object,
    current: object,
    path: str,
    failures: list[str],
) -> None:
    baseline_type = typename(baseline)
    current_type = typename(current)
    label = path or "<root>"

    if baseline_type != current_type:
        failures.append(
            f"{label}: type mismatch expected={baseline_type} actual={current_type}"
        )
        return

    if isinstance(baseline, dict):
        baseline_keys = set(baseline.keys())
        current_keys = set(current.keys())
        for key in sorted(baseline_keys - current_keys):
            key_path = f"{path}.{key}" if path else key
            failures.append(f"{key_path}: missing key")
        for key in sorted(current_keys - baseline_keys):
            key_path = f"{path}.{key}" if path else key
            failures.append(f"{key_path}: unexpected key")
        for key in sorted(baseline_keys & current_keys):
            key_path = f"{path}.{key}" if path else key
            compare_nodes(baseline[key], current[key], key_path, failures)
        return

    if isinstance(baseline, list):
        if len(baseline) != len(current):
            failures.append(
                f"{label}: array length mismatch expected={len(baseline)} actual={len(current)}"
            )
            return
        for index, (baseline_item, current_item) in enumerate(zip(baseline, current)):
            item_path = f"{label}[{index}]"
            compare_nodes(baseline_item, current_item, item_path, failures)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="M102 WebUI i18n locale alignment smoke tests."
    )
    parser.add_argument("--case", default="all", choices=["all"])
    _ = parser.parse_args()

    failures: list[str] = []
    if not LOCALES_DIR.exists():
        print(f"[FAIL] locales directory missing: {LOCALES_DIR}")
        return 1
    if not BASELINE_FILE.exists():
        print(f"[FAIL] baseline locale missing: {BASELINE_FILE}")
        return 1

    locale_files = sorted(LOCALES_DIR.glob("*.json"))
    if not locale_files:
        print(f"[FAIL] no locale files found in {LOCALES_DIR}")
        return 1

    try:
        baseline = load_json(BASELINE_FILE)
    except json.JSONDecodeError as exc:
        print(f"[FAIL] invalid json in {BASELINE_FILE.name}: {exc}")
        return 1

    if not isinstance(baseline, dict):
        print(f"[FAIL] baseline locale root must be object: {BASELINE_FILE.name}")
        return 1

    checked = 0
    for locale_file in locale_files:
        if locale_file == BASELINE_FILE:
            continue
        checked += 1
        try:
            current = load_json(locale_file)
        except json.JSONDecodeError as exc:
            failures.append(f"{locale_file.name}: invalid json: {exc}")
            continue
        if not isinstance(current, dict):
            failures.append(f"{locale_file.name}: root must be object")
            continue

        locale_failures: list[str] = []
        compare_nodes(baseline, current, "", locale_failures)
        if locale_failures:
            for failure in locale_failures:
                failures.append(f"{locale_file.name}: {failure}")

    if failures:
        for failure in failures:
            print(f"[FAIL] {failure}")
        print(
            f"[SUMMARY] FAIL locales={len(locale_files)} checked={checked} failures={len(failures)}"
        )
        return 1

    print(f"[SUMMARY] PASS locales={len(locale_files)} checked={checked} failures=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
