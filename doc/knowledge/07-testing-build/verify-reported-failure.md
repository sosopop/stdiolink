# Verify Reported Failure

## Purpose

把“用户贴了一段失败日志”快速转成可执行的确认流程，判断问题是否当前仍存在。

## Workflow

1. 从日志里先提取测试标识：GTest case、CTest test、Smoke plan 或前端文件名。
2. 用 `ctest --test-dir build -N -V` 确认实际运行的测试命令和产物路径。
3. 用最短命令复现同一条测试，不要一上来跑全量。
4. 记录结论：当前可复现、当前不可复现、只在特定目录复现，或更像环境问题。

## Minimal Commands

```powershell
rg -n "SuiteName|TestName" src/tests
ctest --test-dir build -N -V
build\runtime_release\bin\stdiolink_tests.exe --gtest_filter=SuiteName.TestName
python src/smoke_tests/run_smoke.py --plan mXX_name
cd src/webui
npx vitest run path\to\file.test.tsx
npx playwright test e2e\file.spec.ts --grep "case name"
```

## Decision Rules

- 日志写的是 GTest case 时，优先直接跑 `--gtest_filter=...`，不要停留在 `ctest` 摘要。
- `ctest` 只能告诉你“运行了哪个命令”，不能替代 case 级复现。
- 单 case 通过但整包失败时，优先怀疑运行目录、测试间污染、端口占用或残留进程。

## Related

- `ctest-vs-direct-run.md`
- `test-artifact-and-runtime-layout.md`
- `triage-test-failure.md`
- `test-matrix.md`
