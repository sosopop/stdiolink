# Verify Reported Failure

## Purpose

把“用户贴了一段失败日志”快速转成可执行的确认流程，判断问题是否当前仍存在。

## When To Use

- 用户只给出 `FAILED` 摘要，没有说明如何复现
- CI 或手工日志里出现单个测试失败，需要确认当前工作区是否仍可复现
- 需要区分“当前真实回归”和“历史失败/环境失败”

## Workflow

1. 从日志中提取测试标识
   - GTest case：`SuiteName.TestName`
   - CTest test：`stdiolink_tests`、`smoke_xxx`
   - Vitest/Playwright：文件名、用例名、失败步骤
2. 定位源码或测试入口
   - C++/GTest：`rg -n "SuiteName|TestName" src/tests`
   - Smoke：`rg -n "m[0-9a-z_]+" src/smoke_tests`
   - WebUI：`rg -n "describe\\(|it\\(|test\\(" src/webui`
3. 确认实际运行的是哪个产物
   - 先看 `ctest --test-dir build -N -V`
   - 判断测试程序来自 `build/runtime_debug/bin`、`build/runtime_release/bin` 还是其他目录
4. 用最短命令复现同一条测试
   - GTest 优先直接跑测试程序并加 `--gtest_filter=...`
   - 需要回测偶发失败时，可用 `tools/replay_gtest_failures.ps1` 对目标 case 和相邻 case 做重复回放，只在失败时落日志
   - Smoke 优先跑对应 `run_smoke.py --plan ...`
   - WebUI 优先跑对应测试文件或用例
5. 记录复现结论
   - 当前可复现
   - 当前不可复现，但历史日志存在
   - 仅在特定构建/运行目录下可复现
   - 仅在环境异常时出现

## Minimal Commands

### GTest

```powershell
rg -n "SuiteName|TestName" src/tests
ctest --test-dir build -N -V
build\runtime_release\bin\stdiolink_tests.exe --gtest_filter=SuiteName.TestName
build\runtime_debug\bin\stdiolink_tests.exe --gtest_filter=SuiteName.TestName
tools/replay_gtest_failures.ps1 --target SuiteName.TestName --source-file src/tests/test_xxx.cpp --adjacent 1 --repeat 100
```

### Smoke

```powershell
ctest --test-dir build -N -V
python src/smoke_tests/run_smoke.py --plan m101_bin_scan_orchestrator
ctest --test-dir build -L smoke --output-on-failure
```

### WebUI

```powershell
cd src/webui
npx vitest run src/pages/Projects/__tests__/ProjectDetail.test.tsx
npx playwright test e2e/projects.spec.ts --grep "project detail"
```

## Decision Rules

- 日志写的是 GTest case，不要只停留在 `ctest` 摘要；直接跑 `stdiolink_tests.exe --gtest_filter=...`
- `ctest` 注册的是测试程序时，`ctest` 只能告诉你“哪个二进制被运行”，不能替代 case 级复现
- debug 产物和 release 产物都存在时，先确认失败日志对应哪套产物
- 如果单 case 通过，但整包失败，优先怀疑测试间污染、端口占用、残留进程或共享运行目录

## Common Misreads

- 把 CTest 的 test name 误认为 GTest case 名称
- 直接复现 debug 产物，但 CI/本地失败其实来自 release 产物
- 只看一条失败摘要，忽略它前面的资源拷贝、端口绑定、PATH 污染错误
- 复现时没有使用与原日志一致的工作目录

## Outputs To Capture

- 使用的命令
- 实际运行的测试程序路径
- 退出码
- 核心 stdout/stderr 摘要
- 是否在 debug/release 都复现

## Related

- `test-artifact-and-runtime-layout.md`
- `triage-test-failure.md`
- `test-matrix.md`
