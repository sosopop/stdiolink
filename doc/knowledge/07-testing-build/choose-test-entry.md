# Choose Test Entry

## Purpose

按改动类型或排查目标，选择最省时的测试入口，避免上来就跑全量。

## Fast Routing

- 用户贴失败日志，先确认问题是否仍存在：`verify-reported-failure.md`
- `ctest` 和手动运行结果不一致：`ctest-vs-direct-run.md`
- 怀疑是运行目录、产物或环境问题：`triage-test-failure.md`
- 怀疑是半包、粘包、分段到达：`stream-transport-test-pattern.md`

## By Change Area

- `src/stdiolink/protocol/`、`src/stdiolink/driver/`、`src/drivers/`、`src/stdiolink/host/`：先跑相关 GTest，再按链路决定是否补 smoke。
- `src/stdiolink_service/`、`src/stdiolink_server/`：先跑对应 GTest；涉及 runtime、调度、HTTP 联调时再补 smoke。
- `src/webui/`：页面逻辑先 Vitest，跨页面流程再补 Playwright。

## Minimal Commands

- GTest：`build\runtime_release\bin\stdiolink_tests.exe --gtest_filter=SuiteName.TestName`
- Smoke：`python src/smoke_tests/run_smoke.py --plan mXX_name`
- WebUI：在 `src/webui/` 下先跑 `npx vitest run ...`，再决定是否补 `npx playwright test ...`

## Escalation Rule

- 先从最接近源码层的测试开始，只有单元层不够覆盖时再升级。
- 影响运行目录、跨进程编排或发布包行为时，再优先检查 runtime / CTest。

## Related

- `ctest-vs-direct-run.md`
- `stream-transport-test-pattern.md`
- `verify-reported-failure.md`
- `triage-test-failure.md`
- `test-matrix.md`
