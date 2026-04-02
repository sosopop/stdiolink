# Driver Lifecycle

## Purpose

明确一个 Driver 进程从启动到响应结束的链路，以及新增 Driver 需要改哪些地方。

## Runtime Chain

`main.cpp` -> `DriverCore` -> 解析 JSONL/Console 输入 -> 参数校验 -> `ICommandHandler::handle()` -> `IResponder` 输出 `event|done|error`

## Core Interfaces

- `ICommandHandler`：只处理命令执行。
- `IMetaCommandHandler`：在处理命令外，额外提供 `driverMeta()`。
- `IResponder`：抽象输出端，常见实现是 `StdioResponder`。

## Implementation Entry

- 核心运行时：`src/stdiolink/driver/driver_core.*`
- handler 接口：`src/stdiolink/driver/icommand_handler.h`
- meta handler：`src/stdiolink/driver/meta_command_handler.h`
- responder：`src/stdiolink/driver/{iresponder.h,stdio_responder.*}`
- console/help：`src/stdiolink/driver/help_generator.*`

## New Driver Path

- 在 `src/drivers/driver_xxx/` 创建 `main.cpp`、handler、`CMakeLists.txt`
- 输出名必须是 `stdio.drv.<name>`
- 注册到 runtime 组装，确保进入 `data_root/drivers/`
- 有参数/文档/DriverLab 需求时实现 `IMetaCommandHandler`

## Constraints

- Windows 下标准输入读取沿用 Qt 行读取。
- 用 Qt JSON/IO 类型，不要混入另一套序列化或管道实现。
- 明确是 `OneShot` 还是 `KeepAlive`；生命周期会影响 Host 和 Service 的关闭行为。
- `OneShot` 下如果每条命令都显式带连接参数，优先在文档和 meta 中写清默认值来源、是否复用最近一次执行结果、哪些命令是纯无状态。
- Console 模式对外只保证“`0` 表示成功、非 `0` 表示失败”；详细业务错误码应从 stdout JSON 的 `code` 字段读取，不应依赖进程退出码在各平台上精确保留 `400/404/1000+`。

## Mode Semantics

- `Console` 模式通常由 `--cmd=...` 触发；命令参数走 argv 解析，Driver 只会在收到 `done` / `error` 后退出事件循环。
- `Stdio` 模式通过 stdin/stdout 传 JSONL；在 `OneShot` profile 下，`DriverCore` 处理完第一条 stdin 请求后就会结束进程，不要求该命令一定输出 `done`。
- 这意味着“同一个 `run` 命令”在不同入口可能表现不同：命令行 `--cmd=run` 走 `Console`，适合 server 型长生命周期命令；WebSocket/Host/DriverLab oneshot 走 `Stdio`，处理完一条请求后会退出。
- 如果某个命令的设计目标是“启动服务后常驻，仅靠 `event` 报启动成功”，要在文档里明确它更适合 `KeepAlive` 或命令行 `Console` 场景，不要默认认为 DriverLab oneshot 能保持该进程常驻。

## Related

- `driver-meta.md`
- `../08-workflows/add-driver.md`
- `../07-testing-build/test-matrix.md`
