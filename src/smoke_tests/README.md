# Smoke Tests

This directory contains plan-oriented Python smoke tests.

## Goals

- Provide fast regression checks for critical paths in each test plan.
- Keep scripts runnable both from command line and from CTest.
- Avoid replacing existing unit/integration tests.

## Layout

- `run_smoke.py`: shared entrypoint.
- `mXX_*.py`: plan scripts (milestone or non-milestone).

## Usage

From repository root:

```bash
python src/smoke_tests/run_smoke.py --plan m94_server_run_oneshot
```

Run a single case:

```bash
python src/smoke_tests/run_smoke.py --plan m94_server_run_oneshot --case tcp_run_oneshot
```

With CTest:

```bash
ctest -R smoke_m94_server_run_oneshot --output-on-failure
```

## Add tests for a new plan

1. Add a smoke script in this directory.
2. Register the script in `run_smoke.py` under `PLAN_SCRIPTS`.
3. Register a CTest case in `src/smoke_tests/CMakeLists.txt`.
