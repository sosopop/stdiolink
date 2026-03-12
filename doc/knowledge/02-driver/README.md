# Driver

本目录覆盖 C++ Driver 进程的生命周期、命令处理、响应输出和元数据。

## Files

- `driver-lifecycle.md`：`DriverCore`、运行模式、处理链和新增 Driver 时的落点。
- `driver-meta.md`：`IMetaCommandHandler`、`MetaBuilder`、导出与消费方。
- 设备专用 Driver 如果底层仍是标准寄存器协议，优先复用已有 Modbus RTU/TCP 传输实现，再在 Handler 层做设备语义映射。

## Source Anchors

- `src/stdiolink/driver/`
- `src/drivers/`
- `doc/manual/05-driver/`
