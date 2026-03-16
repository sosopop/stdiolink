# Service

本目录覆盖 `stdiolink_service` QuickJS 运行时、内置模块、配置 schema 和 Driver 编排方式。

## Files

- `service-runtime.md`：运行时结构、绑定模块、主入口、常见修改点。
- `service-config-and-driver.md`：Service 三件套、配置 schema、`resolveDriver/openDriver` 约束。

## Services

- `bin_scan_orchestrator`：料箱扫描编排，驱动 PLC + 3DVision
- `modbustcp_server_service`：Modbus TCP 从站服务
- `modbusrtu_server_service`：Modbus RTU 从站服务
- `modbusrtu_serial_server_service`：Modbus RTU 串口从站服务
- `plc_crane_sim`：PLC 行车模拟
- `exec_runner`：通用进程执行服务，通过配置启动任意外部进程并实时输出日志（M104）

## Source Anchors

- `src/stdiolink_service/`
- `src/data_root/services/`
- `doc/manual/10-js-service/`
