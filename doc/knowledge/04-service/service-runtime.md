# Service Runtime

## Purpose

明确 JS Service 是如何在 QuickJS 中跑起来的，以及各内置模块的源码入口。

## Runtime Chain

`stdiolink_service/main.cpp` -> 解析命令行/配置 -> 初始化 QuickJS -> 注册内置模块 -> 加载 Service `index.js` -> 调用 Host 能力/其他绑定

## Main Modules

- `stdiolink`：Driver/Task/openDriver/waitAny/getConfig
- `stdiolink/constants`
- `stdiolink/path`
- `stdiolink/fs`
- `stdiolink/time`
- `stdiolink/http`
- `stdiolink/log`
- `stdiolink/process`
- Driver 查找扩展：`stdiolink/driver`

## Implementation Entry

- 入口：`src/stdiolink_service/main.cpp`
- 模块聚合：`src/stdiolink_service/bindings/js_stdiolink_module.*`
- Driver/Task：`src/stdiolink_service/bindings/js_{driver,task}*`
- 调度/等待：`src/stdiolink_service/bindings/js_{task_scheduler,wait_any_scheduler}*`
- 配置：`src/stdiolink_service/config/service_*`

## Constraints

- JS 层大多是 C++ Host 能力的包装；底层行为异常先回到 `src/stdiolink/host/` 查。
- 改内置模块导出名时，同时检查手册、示例 Service 和绑定测试。

## Tests

- `src/tests/test_js_integration.cpp`
- `src/tests/test_js_engine_scaffold.cpp`
- `src/tests/test_constants_binding.cpp`
- `src/tests/test_http_binding.cpp`

## Related

- `service-config-and-driver.md`
- `../03-host/driver-task-flow.md`
