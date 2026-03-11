# Driver And Task Flow

## Purpose

理解 Host 如何包装 Driver 子进程，以及单次请求如何映射成 `Task`。

## Runtime Chain

Host code -> `Driver::start()` -> `QProcess` 启动 Driver -> `request()` 发送命令 -> `Task` 接收 `event`/终态 -> 调用方等待或并发调度

## Implementation Entry

- Driver 包装：`src/stdiolink/host/driver.*`
- Task：`src/stdiolink/host/task.*`
- 状态定义：`src/stdiolink/host/task_state.h`
- Driver 目录索引：`src/stdiolink/host/driver_catalog.*`

## Key Constraints

- 必须正确处理 Driver 早退；否则 `waitNext` / `waitAnyNext` 会悬挂或丢错误。
- `request()` 遇到已退出 Driver，返回失败 `Task`；不会自动重启进程。
- `Task.tryNext()` / `waitNext()` 在 Driver 早退场景下应产出 terminal `error` message，而不是静默返回空。
- `Task` 不是简单 future；它需要保留中间 `event`。
- 改 `Driver` 生命周期时要检查 JS 绑定，因为 Service 底层复用 Host 能力。

## Tests

- `src/tests/test_host_driver.cpp`
- `src/tests/test_driver_task_binding.cpp`
- `src/tests/test_driver_resolve.cpp`

## Related

- `wait-any-and-forms.md`
- `../04-service/service-runtime.md`
- `../08-workflows/debug-change-entry.md`
