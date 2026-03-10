# Service Config And Driver Use

## Purpose

约束 Service 目录结构、配置 schema 和 Driver 使用方式，避免运行时定位错误。

## Service Template

- 必需文件：`manifest.json`、`config.schema.json`、`index.js`
- Service 事实源目录：`src/data_root/services/<service_id>/`
- Server 扫描依赖三件套完整存在

## Config

- `config.schema.json` 使用项目自定义 schema，底层复用 `FieldMeta`，不是标准 JSON Schema。
- 运行时通过 `getConfig()` 读取。
- 相关实现：`src/stdiolink_service/config/service_config_schema.*`
- `--config-file` 只按传入字符串和当前工作目录 `QFile::open()`；不会相对 `--data-root` 自动解析。
- 写 CLI 示例时，如果 `--config-file` 用相对路径，必须同时明确命令执行时的 `cwd`。

## Driver Use

- 推荐：`resolveDriver("stdio.drv.xxx")` 后再 `openDriver(path)`
- `resolveDriver()` 默认按 `data_root/drivers/*/` 查找
- oneshot Service 需要显式 `drv.$close()`
- 命令名包含 `.` 时用 `proxy["cmd.name"]()`

## Modify Entry

- 改 Driver 查找逻辑：`src/stdiolink_service/bindings/js_driver_resolve*`
- 改 Proxy 调用：`src/stdiolink_service/bindings/js_driver.*`
- 改配置帮助/schema 输出：`src/stdiolink_service/config/service_config_help.*`

## Tests And Examples

- 示例服务：`src/data_root/services/modbustcp_server_service/`
- 测试：`src/tests/test_driver_resolve.cpp`
- 集成：`src/tests/test_bin_scan_orchestrator_service.cpp`

## Related

- `../00-overview/runtime-layout.md`
- `../08-workflows/add-service-or-project.md`
