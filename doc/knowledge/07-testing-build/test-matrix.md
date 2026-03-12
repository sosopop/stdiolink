# Test Matrix

## Purpose

说明每类测试负责什么，以及新增功能至少要补哪一层。

## Layers

- GTest：`src/tests/`，覆盖协议、Host/Driver、Server、JS 绑定
- Smoke：`src/smoke_tests/`，覆盖端到端功能链路
- Vitest：`src/webui/src/__tests__/`，覆盖前端逻辑
- Playwright：`src/webui/e2e/`，覆盖前端端到端

## Commands

- 全量 CTest：`ctest --test-dir build --output-on-failure`
- Smoke label：`ctest --test-dir build -L smoke --output-on-failure`
- Smoke plan：`python src/smoke_tests/run_smoke.py --plan all`
- WebUI：在 `src/webui/` 下运行 `npm run test` / `npx playwright test`

## Decision Table

- 用户贴 `[ FAILED ] Suite.Test`
  - 先读 `verify-reported-failure.md`
  - 先确认 `ctest --test-dir build -N -V`
  - 再直接跑 `stdiolink_tests.exe --gtest_filter=Suite.Test`
- 不知道先跑哪层测试
  - 先读 `choose-test-entry.md`
  - 核心 C++ 行为优先 GTest
  - 跨进程链路再补 smoke
- CTest 和手动运行结果不一致
  - 先读 `test-artifact-and-runtime-layout.md`
  - 比较产物路径、工作目录、环境变量
- 失败像环境问题或偶发竞争
  - 先读 `triage-test-failure.md`
  - 先排查端口、残留进程、运行目录、超时

## CTest And GTest

- `ctest` 注册的是测试命令，不一定是单个 GTest case
- 当前仓库中，`stdiolink_tests` 常作为一个整体测试程序被 CTest 注册
- `BinScanOrchestratorServiceTest` 已拆到独立的 `bin_scan_orchestrator_service_tests`，不再包含在 `stdiolink_tests` 里
- 要复现单个 GTest case，优先直接运行 `stdiolink_tests.exe --gtest_filter=...`
- 在判断失败是否仍存在前，先用 `ctest --test-dir build -N -V` 确认实际注册命令和工作目录

## Minimum Expectation

- 改协议/Host/Driver：至少补 GTest
- 改 Service/Server 编排：补 GTest，必要时补 Smoke
- 改需要端到端覆盖的功能：补对应 smoke 脚本，并同时注册到 `run_smoke.py` 和 `CMakeLists.txt`
- 改 WebUI：补 Vitest；跨页面流程补 Playwright

## Smoke Registration Rule

- Smoke 脚本文件：`src/smoke_tests/mXX_*.py`
- 统一入口注册：`src/smoke_tests/run_smoke.py`
- CTest 注册：`src/smoke_tests/CMakeLists.txt`
- 缺任何一处都会导致脚本无法通过统一入口或 CTest 被发现

## High Value Test Files

- `src/tests/test_host_driver.cpp`
- `src/tests/test_wait_any.cpp`
- `src/tests/test_js_integration.cpp`
- `src/tests/test_api_router.cpp`
- `src/tests/test_driver_manager_scanner.cpp`

## Related

- `choose-test-entry.md`
- `verify-reported-failure.md`
- `triage-test-failure.md`
- `test-artifact-and-runtime-layout.md`
- `../08-workflows/add-driver.md`
- `../08-workflows/add-service-or-project.md`
