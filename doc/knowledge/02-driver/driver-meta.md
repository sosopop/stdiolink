# Driver Meta

## Purpose

定义 Driver 的自描述信息，支撑校验、文档、表单和调试工具。

## Key Conclusions

- 优先把 Driver 当成“有元数据的命令集合”而不是只会接 JSON 的黑盒进程。
- 元数据越完整，Server 扫描、WebUI DriverLab、表单生成、文档导出越稳定。
- 命令别名如果要参与参数校验、CLI help、DriverLab 表单和默认值填充，必须进入 meta；只在 handler 里手工改名不够。

## Build Path

- 使用 `DriverMetaBuilder`
- 用 `CommandBuilder` 定义命令
- 用 `FieldBuilder` 定义参数、返回、事件

## Implementation Entry

- builder：`src/stdiolink/driver/meta_builder.*`
- 导出：`src/stdiolink/driver/meta_exporter.*`
- 示例填充：`src/stdiolink/driver/example_auto_fill.*`

## Consumers

- Host `meta_cache.*` / `form_generator.*`
- Service 配置 schema 与运行时帮助
- Server `driver_manager_scanner.*`
- WebUI DriverLab 和 Driver 详情页

## Modify Entry

- 改元数据字段时同步查：协议层类型、Driver 导出、Host 缓存、Server 扫描、前端使用点。
- 有 alias 时优先把 alias 也建成命令，再在 handler 内归一到主命令；否则 `DriverCore` 前置校验、`--help-command` 和 DriverLab 都看不到。

## Related

- `../01-protocol/meta-validation.md`
- `../05-server/server-lifecycle.md`
- `../06-webui/driverlab-and-events.md`
