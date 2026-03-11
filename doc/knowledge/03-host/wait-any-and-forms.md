# WaitAny And Forms

## Purpose

覆盖 Host 端两个高复用能力：多任务并发等待、基于元数据生成表单描述。

## waitAny

- 入口：`src/stdiolink/host/wait_any.*`
- 作用：在多个 `Task` 之间按事件/完成态推进调度。
- 高风险改动：Driver 提前退出、事件流丢失、某个 Task 永久不结束。
- Driver 早退时应返回 `msg.status === "error"`，不是 `null`。
- `null` 只应用在空任务组、超时或所有任务都已结束且没有新消息。

## Form Generation

- 入口：`src/stdiolink/host/form_generator.*`
- 依赖：`DriverMeta` / `FieldMeta`
- 作用：为 UI 或工具生成参数输入描述，不直接做业务执行。

## Related Modules

- meta 缓存：`src/stdiolink/host/meta_cache.*`
- meta 版本检查：`src/stdiolink/host/meta_version_checker.*`
- config 注入：`src/stdiolink/host/config_injector.*`

## Modify Entry

- 改 `FieldMeta`、默认值或嵌套类型时，同时检查表单生成。
- 改 `Task` 或事件语义时，同时检查 `wait_any.*` 和 JS `waitAny()` 绑定。

## Tests

- `src/tests/test_wait_any.cpp`
- `src/tests/test_example_auto_fill.cpp`
- `src/tests/test_config_inject.cpp`

## Related

- `../01-protocol/meta-validation.md`
- `../04-service/service-runtime.md`
