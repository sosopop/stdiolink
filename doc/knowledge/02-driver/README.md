# Driver

本目录覆盖 C++ Driver 进程的生命周期、命令处理、响应输出和元数据。

## Files

- `driver-plc-crane.md`：PLC 升降装置驱动/仿真的 DI 状态语义、仿真约束和测试入口。
- `driver-3d-temp-scanner.md`：3D 系统温度扫描仪驱动的命令面、协议映射和输出格式约束。
- `driver-3d-laser-radar.md`：三维激光雷达 TCP OneShot 驱动的命令范围、LIDA 协议映射、长任务轮询与原始数据落盘约束。
- `driver-opcua.md`：OPC UA 客户端驱动的节点查询/全量快照命令、递归规则和测试入口。
- `driver-opcua-server.md`：OPC UA Server 驱动的建点/删点/写值命令、事件模型与 Service/Project 接入点。
- `driver-pqw-analog-output.md`：品全微模拟量输出模块驱动的命令面、寄存器映射和测试入口。
- `driver-lifecycle.md`：`DriverCore`、运行模式、处理链和新增 Driver 时的落点。
- `driver-meta.md`：`IMetaCommandHandler`、`MetaBuilder`、导出与消费方。
- 设备专用 Driver 如果底层仍是标准寄存器协议，优先复用已有 Modbus RTU/TCP 传输实现，再在 Handler 层做设备语义映射。

## Source Anchors

- `src/stdiolink/driver/`
- `src/drivers/`
- `doc/manual/05-driver/`
