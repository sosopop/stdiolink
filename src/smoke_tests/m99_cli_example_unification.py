#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parents[2]
EXE_SUFFIX = '.exe' if os.name == 'nt' else ''


def candidate_bin_dirs() -> list[Path]:
    dirs: list[Path] = []
    env_bin = os.environ.get('STDIOLINK_BIN_DIR')
    if env_bin:
        dirs.append(Path(env_bin))
    dirs.extend([
        ROOT_DIR / 'build' / 'release',
        ROOT_DIR / 'build' / 'debug',
        ROOT_DIR / 'build' / 'runtime_release' / 'bin',
        ROOT_DIR / 'build' / 'runtime_debug' / 'bin',
    ])
    return dirs


def find_exe(base_name: str) -> Path | None:
    file_name = base_name + EXE_SUFFIX
    for directory in candidate_bin_dirs():
        candidate = directory / file_name
        if candidate.exists():
            return candidate
    return None


def run(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, check=False, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=20)


def assert_contains(text: str, needle: str, label: str, failures: list[str]) -> None:
    if needle not in text:
        failures.append(f'{label} missing: {needle}')


def assert_not_contains(text: str, needle: str, label: str, failures: list[str]) -> None:
    if needle in text:
        failures.append(f'{label} contains forbidden fragment: {needle}')


def extract_with_regex(text: str, pattern: str, label: str, failures: list[str]) -> str | None:
    match = re.search(pattern, text, re.MULTILINE)
    if match is None:
        failures.append(f'{label} missing extract pattern: {pattern}')
        return None
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser(description='M99 CLI example unification smoke tests.')
    parser.add_argument('--case', default='all', choices=['all'])
    _ = parser.parse_args()

    exe = find_exe('test_console_meta_driver')
    if exe is None:
        print('[FAIL] test_console_meta_driver executable not found')
        return 1

    failures: list[str] = []

    help_proc = run([str(exe), '--cmd=run', '--help'])
    if help_proc.returncode != 0:
        failures.append(f'command help failed exit={help_proc.returncode}')
    help_text = help_proc.stdout + '\n' + help_proc.stderr
    assert_contains(help_text, '--password="123456"', 'help', failures)
    assert_contains(help_text, '--units[0].id=1', 'help', failures)
    assert_not_contains(help_text, '--units="{', 'help', failures)

    md_proc = run([str(exe), '--export-doc=markdown'])
    if md_proc.returncode != 0:
        failures.append(f'markdown export failed exit={md_proc.returncode}')
    assert_contains(md_proc.stdout, '--password="123456"', 'markdown', failures)
    assert_contains(md_proc.stdout, '--units[0].id=1', 'markdown', failures)

    html_proc = run([str(exe), '--export-doc=html'])
    if html_proc.returncode != 0:
        failures.append(f'html export failed exit={html_proc.returncode}')
    assert_contains(html_proc.stdout, '--password=&quot;123456&quot;', 'html', failures)
    assert_contains(html_proc.stdout, '--units[0].id=1', 'html', failures)

    ts_proc = run([str(exe), '--export-doc=ts'])
    if ts_proc.returncode != 0:
        failures.append(f'ts export failed exit={ts_proc.returncode}')
    assert_contains(ts_proc.stdout, '@example <program> --cmd=run --mode_code="1" --password="123456" --units[0].id=1 --units[0].size=10000', 'ts', failures)

    expected_cli = '<program> --cmd=run --mode_code="1" --password="123456" --units[0].id=1 --units[0].size=10000'
    help_cli = extract_with_regex(help_text, r'cli\s*:\s*(<program> --cmd=run .+)', 'help', failures)
    md_cli = extract_with_regex(md_proc.stdout, r'- CLI: `(<program> --cmd=run .+)`', 'markdown', failures)
    html_cli = extract_with_regex(html_proc.stdout, r'CLI: <code>([^<]+)</code>', 'html', failures)
    ts_cli = extract_with_regex(ts_proc.stdout, r'@example (<program> --cmd=run .+)', 'ts', failures)
    if help_cli is not None and md_cli is not None and help_cli != md_cli:
        failures.append(f'help/markdown cli mismatch: help={help_cli!r} markdown={md_cli!r}')
    if help_cli is not None and help_cli != expected_cli:
        failures.append(f'help cli mismatch expected={expected_cli!r} actual={help_cli!r}')
    if md_cli is not None and md_cli != expected_cli:
        failures.append(f'markdown cli mismatch expected={expected_cli!r} actual={md_cli!r}')
    if html_cli is not None and html.unescape(html_cli) != expected_cli:
        failures.append(f'html cli mismatch expected={expected_cli!r} actual={html.unescape(html_cli)!r}')
    if ts_cli is not None and ts_cli != expected_cli:
        failures.append(f'ts cli mismatch expected={expected_cli!r} actual={ts_cli!r}')

    if failures:
        for failure in failures:
            print(f'[FAIL] {failure}')
        print(f'[SUMMARY] FAIL failures={len(failures)}')
        return 1

    print('[SUMMARY] PASS cases=4 failures=0')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
