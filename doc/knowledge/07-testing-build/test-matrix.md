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

## Minimum Expectation

- 改协议/Host/Driver：至少补 GTest
- 改 Service/Server 编排：补 GTest，必要时补 Smoke
- 改里程碑功能：补对应 smoke 脚本并注册到 `run_smoke.py` 和 `CMakeLists.txt`
- 改 WebUI：补 Vitest；跨页面流程补 Playwright

## High Value Test Files

- `src/tests/test_host_driver.cpp`
- `src/tests/test_wait_any.cpp`
- `src/tests/test_js_integration.cpp`
- `src/tests/test_api_router.cpp`
- `src/tests/test_driver_manager_scanner.cpp`

## Related

- `../08-workflows/add-driver.md`
- `../08-workflows/add-service-or-project.md`
