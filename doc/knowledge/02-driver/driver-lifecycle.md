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

## Related

- `driver-meta.md`
- `../08-workflows/add-driver.md`
- `../07-testing-build/test-matrix.md`
