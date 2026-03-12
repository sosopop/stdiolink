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
- 调整具体 Service 的时间/重试类约束时，`config.schema.json` 的 min/max 必须和实现真实可承受范围一致；约束过严会直接卡住 GTest/Smoke 里的快速配置。
- 相关实现：`src/stdiolink_service/config/service_config_schema.*`
- `stdiolink_service --config.*` 已复用共享 `JsonCliCodec` 路径语法；支持 `foo.bar`、`foo[0].bar`、`foo[]`、`foo["quoted.key"]`，与 driver CLI 和 WebUI expanded command 保持一致。
- `stdiolink_service --config.*` 现在也会按 `config.schema.json` 先做字段级类型解析，再交给 `JsonCliCodec` 聚合；`string/enum` 字段会像 driver 一样保留 `123456`、`1` 这类数字样式字符串。
- 路径命中的叶子 override 会按 schema 严格解析；完整容器 literal 仍要求合法 JSON 字面量，不会递归重写内部字段类型。
- `--config-file` 只按传入字符串和当前工作目录 `QFile::open()`；不会相对 `--data-root` 自动解析。
- 写 CLI 示例时，如果 `--config-file` 用相对路径，必须同时明确命令执行时的 `cwd`。

## Driver Use

- 推荐：`resolveDriver("stdio.drv.xxx")` 后再 `openDriver(path)`
- `resolveDriver()` 默认按 `data_root/drivers/*/` 查找
- `openDriver(path, args?, options?)` 统一按 keepalive 方式启动 Driver；即使 `args` 中带了 `--profile=...`，也会被覆盖为 `--profile=keepalive`。
- 如果需要保留底层生命周期控制（例如自己决定 `queryMeta()` 时机、自己消费 `Task`、自己传 `--profile=oneshot|keepalive`），改用 `new Driver()`。
- 命令名包含 `.` 时用 `proxy["cmd.name"]()`
- `bin_scan_orchestrator` 现在会在 3dvision `login` 后显式执行 `ws.connect` 和 `ws.subscribe("vessel.notify")`；两者都是强依赖。
- `bin_scan_orchestrator` 在扫描阶段会消费 driver `event`，一旦收到 `scanner.error` 就立即失败，不再只等 `vessellog.last` 轮询超时。

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
