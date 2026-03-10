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

1. 在 `src/data_root/projects/<projectId>/` 创建 `config.json` 与 `param.json`
2. `config.json` 写 `id`、`serviceId`、`enabled`、`schedule`
3. `param.json` 写 Service 业务参数对象
4. 校验 `serviceId` 与 Service 目录一致
5. 按业务选择 `manual` / `fixed_rate` / `daemon`
6. 若是新增调度字段，联动 Server model/manager/API/UI

## File Boundary

- API 请求体里的 `config` 字段对应 Service 参数；磁盘 `config.json` 不应包含这个字段。
- 磁盘 `config.json` 只放 Project 元信息；Service 业务参数固定放 `param.json`。
- 直接运行 `stdiolink_service --config-file=...` 时，要保证路径对当前工作目录可达；不要假设它会相对 `--data-root` 查找。

## Main Source Entry

- `src/stdiolink_service/`
- `src/data_root/services/`
- `src/data_root/projects/`
- `src/stdiolink_server/{scanner,manager,model}/`

## Tests

- JS 集成：`src/tests/test_js_integration.cpp`
- Driver 解析：`src/tests/test_driver_resolve.cpp`
- Smoke：`src/smoke_tests/`
- 改 Project 存储或 Project 生命周期时，补：
  - `src/tests/test_project_manager.cpp`：双文件保存/回滚、磁盘格式误写
  - `src/tests/test_api_router.cpp`：删除运行中项目、Project 变更冲突
  - `src/tests/test_instance_manager.cpp`：项目级等待退出
  - `src/webui/src/pages/Projects/__tests__/ProjectConfig.test.tsx`：测试命令路径和 `cwd`

## Related

- `../04-service/service-runtime.md`
- `../04-service/service-config-and-driver.md`
- `../05-server/server-lifecycle.md`
