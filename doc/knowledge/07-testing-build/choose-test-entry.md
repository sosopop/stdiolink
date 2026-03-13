# Choose Test Entry

## Purpose

按改动类型或排查目标，选择最省时的测试入口，避免上来就跑全量。

## Choose By Goal

- 确认一条已报失败是否仍存在：先看 `verify-reported-failure.md`
- 验证刚改的核心行为：先跑最接近源码层的测试
- 验证跨进程或运行目录装配：补跑 smoke
- 验证前端页面逻辑：先 Vitest
- 验证前端跨页面流程：补 Playwright

## Choose By Change Area

- `src/stdiolink/protocol/`
  - 先跑相关 GTest
  - 如果协议变更会穿透 Driver/Host/Service，再补 smoke
- `src/stdiolink/driver/`、`src/drivers/`
  - 先跑 Driver/Host 相关 GTest
  - 如果依赖 runtime driver 装配，再补 smoke 或 standalone run
- `src/stdiolink/host/`
  - 先跑 `src/tests/` 中 Host/Task/waitAny 相关 GTest
  - 涉及跨进程编排时补 smoke
- `src/stdiolink_service/`
  - 先跑 JS 绑定、配置、service 集成相关 GTest
  - 涉及 driver/service/runtime data_root 时补 smoke
- `src/stdiolink_server/`
  - 先跑 Server/API/Manager 相关 GTest
  - 涉及实例启动、调度、HTTP 联调时补 smoke
- `src/webui/`
  - 页面逻辑先 Vitest
  - 页面流程和浏览器行为再补 Playwright

## Choose By Failure Source

- 用户贴 `FAILED` 摘要
  - 先查失败来源是 GTest、Smoke、Vitest 还是 Playwright
- `ctest` 失败但日志很短
  - 先用 `ctest --test-dir build -N -V` 看实际命令
- 单个 GTest case 失败
  - 直接跑 `stdiolink_tests.exe --gtest_filter=...`
- smoke 失败
  - 直接跑对应 `run_smoke.py --plan ...`
- 前端 CI 失败
  - 先缩小到单文件 Vitest 或单 spec Playwright

## Stream Transport Bugs

- 短包、半包、粘包、分段到达这类现象，先按流式接收问题处理，不要默认把一次读取当成一帧。
- 先补源码层 GTest 复现；TCP 用本地假服务端分段回包，串口/管道用测试桩分块喂数据。
- 至少保留两个对照：整帧一次到达通过、同一帧分多次到达也通过或稳定报超时。
- 修复点优先看“累计缓冲 + 按边界判完整”；只有涉及 runtime、跨进程或真机兼容性时再升级到 smoke。

## Minimal Command Templates

### C++ / GTest

```powershell
rg -n "keyword" src/tests src/stdiolink src/stdiolink_service src/stdiolink_server
build\runtime_release\bin\stdiolink_tests.exe --gtest_filter=SuiteName.TestName
ctest --test-dir build --output-on-failure -R "^stdiolink_tests$"
```

### Smoke

```powershell
python src/smoke_tests/run_smoke.py --plan mXX_name
ctest --test-dir build -L smoke --output-on-failure
```

### WebUI

```powershell
cd src/webui
npm run test
npx vitest run path\to\file.test.tsx
npx playwright test e2e\file.spec.ts
```

## Escalation Rule

- 单元层已经稳定复现或验证通过，再决定是否上升到 smoke
- 只有在改动影响运行目录、跨进程编排、发布包行为时，才优先检查 runtime 或 release 产物
- 前端不要默认跑 Playwright；先用 Vitest 缩小范围

## Related

- `verify-reported-failure.md`
- `triage-test-failure.md`
- `test-matrix.md`
