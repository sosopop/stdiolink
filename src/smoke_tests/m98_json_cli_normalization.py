#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parents[2]
EXE_SUFFIX = '.exe' if os.name == 'nt' else ''


def candidate_bin_dirs() -> list[Path]:
    return [
        ROOT_DIR / 'build' / 'release',
        ROOT_DIR / 'build' / 'debug',
        ROOT_DIR / 'build' / 'runtime_release' / 'bin',
        ROOT_DIR / 'build' / 'runtime_debug' / 'bin',
    ]


def find_exe(base_name: str) -> Path | None:
    file_name = base_name + EXE_SUFFIX
    for directory in candidate_bin_dirs():
        candidate = directory / file_name
        if candidate.exists():
            return candidate
    return None


def run(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, check=False, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=20)


def parse_last_json(stdout_text: str) -> dict:
    lines = [line.strip() for line in stdout_text.splitlines() if line.strip()]
    if not lines:
        return {}
    return json.loads(lines[-1])


def case_console_numeric_string(exe: Path) -> list[str]:
    proc = run([str(exe), '--cmd=run', '--password=123456', '--mode_code=1', '--units[0].id=1', '--units[0].size=10000'])
    failures: list[str] = []
    if proc.returncode != 0:
        failures.append(f'console_success exit={proc.returncode} stderr={proc.stderr}')
        return failures
    payload = parse_last_json(proc.stdout)
    data = payload.get('data', {})
    if payload.get('status') != 'done':
        failures.append(f'console_success unexpected status: {payload}')
    if data.get('password') != '123456':
        failures.append(f'console_success password mismatch: {payload}')
    if data.get('mode_code') != '1':
        failures.append(f'console_success mode_code mismatch: {payload}')
    units = data.get('units', [])
    if not isinstance(units, list) or not units or units[0].get('id') != 1:
        failures.append(f'console_success units mismatch: {payload}')
    return failures


def case_int64_safe_range(exe: Path) -> list[str]:
    proc = run([str(exe), '--cmd=run', '--password=123456', '--mode_code=1', '--safe_counter=9007199254740993'])
    failures: list[str] = []
    payload = parse_last_json(proc.stdout) if proc.stdout.strip() else {}
    if payload.get('status') != 'error':
        failures.append(f'int64_range unexpected payload: {payload}\nstderr={proc.stderr}')
        return failures
    data = payload.get('data', {})
    if data.get('name') != 'CliParseFailed':
        failures.append(f'int64_range wrong error name: {payload}')
    if 'safe range' not in str(data.get('message', '')):
        failures.append(f'int64_range missing safe range text: {payload}')
    return failures


def case_special_key_path(exe: Path) -> list[str]:
    proc = run([str(exe), '--cmd=run', '--password=123456', '--mode_code=1', '--labels["app.kubernetes.io/name"]="demo"'])
    failures: list[str] = []
    if proc.returncode != 0:
        failures.append(f'special_key exit={proc.returncode} stderr={proc.stderr}')
        return failures
    payload = parse_last_json(proc.stdout)
    data = payload.get('data', {})
    labels = data.get('labels', {})
    if payload.get('status') != 'done':
        failures.append(f'special_key unexpected status: {payload}')
    if not isinstance(labels, dict) or labels.get('app.kubernetes.io/name') != 'demo':
        failures.append(f'special_key labels mismatch: {payload}')
    return failures


def case_conflict_returns_cli_parse_failed(exe: Path) -> list[str]:
    proc = run([str(exe), '--cmd=run', '--password=123456', '--mode_code=1', '--units=[{"id":1}]', '--units[0].size=2'])
    failures: list[str] = []
    payload = parse_last_json(proc.stdout) if proc.stdout.strip() else {}
    if payload.get('status') != 'error':
        failures.append(f'conflict unexpected payload: {payload}\nstderr={proc.stderr}')
        return failures
    data = payload.get('data', {})
    if data.get('name') != 'CliParseFailed':
        failures.append(f'conflict wrong error name: {payload}')
    if 'container literal vs child path' not in str(data.get('message', '')):
        failures.append(f'conflict missing path conflict text: {payload}')
    return failures


def case_roundtrip_via_gtest() -> list[str]:
    failures: list[str] = []
    tests_exe = find_exe('stdiolink_tests')
    if tests_exe is None:
        failures.append('roundtrip gtest executable not found')
        return failures
    proc = run([str(tests_exe), '--gtest_filter=JsonCliCodec.T14_RenderArgsRoundtripWithDefaultModes'])
    if proc.returncode != 0:
        failures.append(f'roundtrip gtest failed exit={proc.returncode}\nstdout={proc.stdout}\nstderr={proc.stderr}')
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description='M98 JSON/CLI normalization smoke tests.')
    parser.add_argument('--case', default='all', choices=['all'])
    _ = parser.parse_args()

    exe = find_exe('test_console_meta_driver')
    if exe is None:
        print('[FAIL] test_console_meta_driver executable not found')
        for directory in candidate_bin_dirs():
            print(f'  candidate: {directory / ("test_console_meta_driver" + EXE_SUFFIX)}')
        return 1

    failures = []
    failures.extend(case_console_numeric_string(exe))
    failures.extend(case_int64_safe_range(exe))
    failures.extend(case_special_key_path(exe))
    failures.extend(case_conflict_returns_cli_parse_failed(exe))
    failures.extend(case_roundtrip_via_gtest())

    if failures:
        for failure in failures:
            print(f'[FAIL] {failure}')
        print(f'[SUMMARY] FAIL failures={len(failures)}')
        return 1

    print('[SUMMARY] PASS cases=5 failures=0')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
