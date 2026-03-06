#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parents[2]

FILES = {
    'console': ROOT_DIR / 'doc' / 'manual' / '07-console' / 'README.md',
    'driverlab': ROOT_DIR / 'doc' / 'manual' / '12-webui' / 'driverlab.md',
    'best_practices': ROOT_DIR / 'doc' / 'manual' / '08-best-practices.md',
    'troubleshooting': ROOT_DIR / 'doc' / 'manual' / '09-troubleshooting.md',
}


def main() -> int:
    parser = argparse.ArgumentParser(description='M100 docs and migration smoke tests.')
    parser.add_argument('--case', default='all', choices=['all'])
    _ = parser.parse_args()

    failures: list[str] = []
    for name, path in FILES.items():
        if not path.exists():
            failures.append(f'missing file: {name} -> {path}')
            continue
        text = path.read_text(encoding='utf-8')
        if name == 'console':
            for needle in ['servers[0].host', 'tags[]', '--units[0].id=1']:
                if needle not in text:
                    failures.append(f'console missing {needle}')
        elif name == 'driverlab':
            for needle in ['argv token', 'JSON']:
                if needle not in text:
                    failures.append(f'driverlab missing {needle}')
        elif name == 'best_practices':
            for needle in ['--units[0].id=1', '错误处理', 'config.schema.json']:
                if needle not in text:
                    failures.append(f'best_practices missing {needle}')
        elif name == 'troubleshooting':
            for needle in ['PowerShell 5.1', 'expected string', 'expected array', 'Driver 启动失败', 'DriverBusyError']:
                if needle not in text:
                    failures.append(f'troubleshooting missing {needle}')

    joined = '\n'.join(path.read_text(encoding='utf-8') for path in [FILES['console'], FILES['troubleshooting']] if path.exists())
    if '--units=[{"id":1}]' not in joined or '--units[0].id=1' not in joined:
        failures.append('migration mapping missing old/new units example')

    if failures:
        for failure in failures:
            print(f'[FAIL] {failure}')
        print(f'[SUMMARY] FAIL failures={len(failures)}')
        return 1

    print('[SUMMARY] PASS cases=3 failures=0')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
