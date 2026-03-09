# Add Service Or Project

## Goal

新增一个 JS Service，或新增一个基于现有 Service 的 Project 配置。

## Service Steps

1. 在 `src/data_root/services/<service_id>/` 创建 `manifest.json`、`config.schema.json`、`index.js`
2. 在 `index.js` 中优先使用 `resolveDriver()` + `openDriver()`
3. 用 `getConfig()` 读取配置，用 `createLogger()` 打日志
4. oneshot 场景在结束前 `drv.$close()`
5. 需要被 Server 管理时，确认 `ServiceScanner` 能扫描到目录

## Project Steps

1. 在 `src/data_root/projects/<project>.json` 写 `serviceId`、`enabled`、`schedule`、`config`
2. 校验 `serviceId` 与 Service 目录一致
3. 按业务选择 `manual` / `fixed_rate` / `daemon`
4. 若是新增调度字段，联动 Server model/manager/API/UI

## Main Source Entry

- `src/stdiolink_service/`
- `src/data_root/services/`
- `src/data_root/projects/`
- `src/stdiolink_server/{scanner,manager,model}/`

## Tests

- JS 集成：`src/tests/test_js_integration.cpp`
- Driver 解析：`src/tests/test_driver_resolve.cpp`
- Smoke：`src/smoke_tests/`

## Related

- `../04-service/service-runtime.md`
- `../04-service/service-config-and-driver.md`
- `../05-server/server-lifecycle.md`
