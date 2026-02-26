# Modbus RTU 串口 Server 从站驱动 — 功能需求文档

## 1. 功能概述

`driver_modbusrtu_serial_server` 是基于 stdiolink 框架的 Modbus RTU 串口从站驱动，通过 RS-232/RS-485 串口直接作为 Modbus Slave 响应主站请求。

核心能力：
- **串口通信**：使用 `QSerialPort` 监听本地串口，接收 RTU 帧并响应。
- **RTU 帧格式**：`[Unit ID][FC][Data][CRC16]`，通过 CRC16 校验数据完整性。
- **多 Unit 支持**：可注册多个从站地址，每个 Unit 拥有独立的四类数据区。
- **完整功能码**：支持 8 种标准 Modbus 功能码（FC 0x01–0x06、0x0F、0x10）。
- **T3.5 帧间隔**：严格遵循 Modbus RTU 标准的 3.5 字符时间帧间隔检测。
- **KeepAlive 生命周期**：启动后持续运行（需在 `main.cpp` 中设置 `Profile::KeepAlive`，或通过 `--profile=keepalive` 启动）。

与其他从站驱动的关系：
- **共享**：命令集、数据区管理、Unit 管理逻辑与 TCP 版本相同。
- **差异**：传输层从 `QTcpServer` 改为 `QSerialPort`，单连接（串口独占）。
- **复用**：CRC16 算法、RTU 帧解析逻辑复用 `driver_modbusrtu/` 中的实现。

输出可执行文件：`stdio.drv.modbusrtu_serial_server`（Windows 下为 `.exe`）。

---

## 2. 使用场景

| 场景 | 说明 |
|------|------|
| DriverLab 调试 | 在 WebUI DriverLab 中启动串口从站服务，模拟 RS-485 从站设备 |
| Server 编排调度 | 通过 `stdiolink_server` 注册为 Service，由 ScheduleEngine 管理生命周期 |
| 设备模拟 | 模拟 PLC 等串口 Modbus 从站，用于主站驱动开发和集成测试 |
| 硬件在环测试 | PC 通过 USB-RS485 转换器作为虚拟从站，与真实主站设备联调 |

---

## 3. 详细功能说明

### 3.1 串口参数

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| port_name | string | 是 | — | 串口名称，如 `"COM3"`（Windows）或 `"/dev/ttyUSB0"`（Linux） |
| baud_rate | int | 否 | 9600 | 波特率：1200 / 2400 / 4800 / 9600 / 19200 / 38400 / 57600 / 115200 |
| data_bits | int | 否 | 8 | 数据位：7 / 8 |
| stop_bits | string | 否 | `"1"` | 停止位：`"1"` / `"1.5"` / `"2"` |
| parity | string | 否 | `"none"` | 校验位：`"none"` / `"even"` / `"odd"` |

### 3.2 `status` — 驱动状态检查

- **参数**：无
- **返回**：`{"status": "ready", "listening": true, "port_name": "COM3", "units": [1, 2]}`

### 3.3 `start_server` — 启动串口从站服务

打开串口，开始监听主站请求。

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| port_name | string | 是 | — | 串口名称 |
| baud_rate | int | 否 | 9600 | 波特率 |
| data_bits | int | 否 | 8 | 数据位 |
| stop_bits | string | 否 | `"1"` | 停止位 |
| parity | string | 否 | `"none"` | 校验位 |

- **返回**：`{"started": true, "port_name": "COM3"}`
- **说明**：如果服务已在运行，返回 error code 3

### 3.4 `stop_server` — 停止串口从站服务

关闭串口，停止监听。

- **参数**：无
- **返回**：`{"stopped": true}`

### 3.5 数据管理命令

以下命令与 `driver_modbustcp_server` 完全一致：

| 命令 | 说明 |
|------|------|
| `add_unit` | 添加从站 Unit，参数：`unit_id`、`data_area_size`（默认 10000） |
| `remove_unit` | 移除从站 Unit，参数：`unit_id` |
| `list_units` | 列出所有已注册 Unit |
| `set_coil` / `get_coil` | 设置/读取线圈值 |
| `set_discrete_input` / `get_discrete_input` | 设置/读取离散输入值 |
| `set_holding_register` / `get_holding_register` | 设置/读取保持寄存器值 |
| `set_input_register` / `get_input_register` | 设置/读取输入寄存器值 |
| `set_registers_batch` / `get_registers_batch` | 批量设置/读取寄存器（支持类型转换） |

各命令的参数定义、返回值格式与 `modbustcp_server_driver.md` 第 3 节完全相同，此处不再重复。

### 3.6 事件推送（KeepAlive 模式）

串口为单连接模式，无 `client_connected` / `client_disconnected` 事件。

| 事件名 | 触发条件 | 数据 |
|--------|----------|------|
| `data_written` | 主站写入数据（FC 0x05/0x06/0x0F/0x10） | `{"unit_id": 1, "function_code": 6, "address": 100, "quantity": 1}` |

---

## 4. 接口约定

### 4.1 JSONL 请求/响应格式

请求（stdin，每行一个 JSON）：
```json
{"cmd": "start_server", "data": {"port_name": "COM3", "baud_rate": 9600, "parity": "none"}}
{"cmd": "add_unit", "data": {"unit_id": 1}}
{"cmd": "set_holding_register", "data": {"unit_id": 1, "address": 0, "value": 100}}
```

成功响应（stdout）：
```json
{"status": "done", "code": 0, "data": {"started": true, "port_name": "COM3"}}
```

事件推送（stdout）：
```json
{"status": "event", "code": 0, "data": {"event": "data_written", "data": {"unit_id": 1, "function_code": 6, "address": 0, "quantity": 1}}}
```

错误响应（stdout）：
```json
{"status": "error", "code": 1, "data": {"message": "Failed to open serial port: COM3"}}
{"status": "error", "code": 3, "data": {"message": "Server already running"}}
```

### 4.2 串口参数规范

| 参数 | 类型 | 范围 | 默认值 |
|------|------|------|--------|
| port_name | string | 合法串口名称 | （必填） |
| baud_rate | int | 1200 / 2400 / 4800 / 9600 / 19200 / 38400 / 57600 / 115200 | 9600 |
| data_bits | int | 7 / 8 | 8 |
| stop_bits | string | `"1"` / `"1.5"` / `"2"` | `"1"` |
| parity | string | `"none"` / `"even"` / `"odd"` | `"none"` |

### 4.3 元数据导出

执行 `stdio.drv.modbusrtu_serial_server --export-meta` 输出完整 DriverMeta JSON。

---

## 5. 实现设计

### 5.1 目录结构

```
src/drivers/driver_modbusrtu_serial_server/
├── CMakeLists.txt
├── main.cpp
├── modbus_rtu_serial_server.h
└── modbus_rtu_serial_server.cpp
```

### 5.2 与现有代码的复用关系

| 模块 | 来源 | 复用方式 |
|------|------|----------|
| CRC16 算法 | `driver_modbusrtu/modbus_rtu_client.cpp` | 复用静态查找表 + `calculateCRC16` |
| 数据区管理 | `tmp/modbustcpserver.h/cpp` | 复用 `ModbusDataArea`、Unit 管理、数据访问接口 |
| 功能码处理 | `tmp/modbustcpserver.cpp` | 复用 8 个 `handle*` 方法的业务逻辑 |
| `modbus_types.h/cpp` | `driver_modbusrtu/` | 引用数据类型和字节序转换 |

### 5.3 与 TCP 版本的核心差异

| 方面 | RTU Over TCP Server | RTU Serial Server |
|------|---------------------|-------------------|
| 传输层 | `QTcpServer` + `QTcpSocket` | `QSerialPort` |
| 连接模式 | 多客户端并发 | 单连接（串口独占） |
| 帧边界检测 | CRC16 + 超时 | T3.5 静默间隔 + CRC16 |
| 连接事件 | `client_connected` / `client_disconnected` | 无 |
| CMake 依赖 | `Qt::Network` | `Qt::SerialPort` |

### 5.4 T3.5 帧间隔检测

作为从站，需通过 T3.5 静默间隔检测帧边界：

- 串口数据到达时启动 T3.5 定时器
- 定时器超时前收到新数据则重置定时器并追加到缓冲区
- 定时器超时后认为一帧完整，进行 CRC16 校验和处理
- 缓冲区超过 256 字节（Modbus RTU 最大 ADU 长度）时，丢弃缓冲区并重新同步
- 波特率 ≤ 19200：`T3.5 = 3.5 × (1 start + data_bits + parity_bit + stop_bits) / baud_rate × 1000 ms`
- 波特率 > 19200：固定 1.75 ms（Modbus 标准建议）

### 5.5 CMake 依赖

```cmake
find_package(Qt6 COMPONENTS Core SerialPort QUIET)
```

---

## 6. 边界条件与异常处理

### 6.1 串口异常

| 场景 | 错误码 | 处理 |
|------|--------|------|
| 串口不存在或被占用 | 1 | 返回 error，message 包含串口名称和系统错误描述 |
| 串口打开后意外断开（USB 拔出） | 1 | 返回 error，message 说明串口连接丢失 |
| 服务已在运行时再次 start | 3 | 返回 error，提示服务已在运行 |
| 服务未运行时调用 stop | 3 | 返回 error，提示服务未运行 |

### 6.2 参数校验

参数校验分两层：

- **元数据 Schema 校验**（框架自动执行）：必填字段缺失、类型不匹配、枚举值非法等，返回 error code **400**。
- **业务逻辑校验**（驱动代码执行）：运行时状态冲突、资源不存在等，返回 error code **3**。

| 场景 | 错误码 | 处理 |
|------|--------|------|
| 必填参数缺失、类型错误、枚举值非法 | 400 | 框架自动校验，返回 ValidationFailed |
| unit_id 已存在时 add_unit | 3 | 返回 error，提示已存在 |
| address 超出数据区范围 | 3 | 返回 error，提示地址越界 |

### 6.3 RTU 协议异常

| 场景 | 处理 |
|------|------|
| 主站请求 CRC16 校验失败 | 丢弃帧，不响应（RTU 标准行为） |
| 主站请求帧长度不足（< 4 字节） | 丢弃帧，不响应 |
| 不支持的功能码 | 返回异常响应（FC\|0x80 + 异常码 0x01） |
| 地址越界 | 返回异常响应（异常码 0x02） |
| Unit ID 不存在 | 不响应（RTU 标准：从站仅响应匹配自身地址的请求） |
| 广播地址（Unit ID = 0） | 仅处理写操作（FC 0x05/0x06/0x0F/0x10），不发送响应；读操作静默忽略 |
| 串口噪声 / 残帧 | T3.5 超时后 CRC 校验失败则丢弃缓冲区，自动恢复同步等待下一帧 |

### 6.4 错误码汇总

| 错误码 | 来源 | 含义 |
|--------|------|------|
| 0 | 驱动 | 成功 |
| 1 | 驱动 | 串口打开失败 |
| 3 | 驱动 | 业务逻辑校验失败 / 状态冲突 |
| 400 | 框架 | 元数据 Schema 校验失败 |
| 404 | 框架 | 未知命令 |

---

## 7. 验收标准

### 7.1 构建

- [ ] `build.bat Release` 成功编译，输出 `stdio.drv.modbusrtu_serial_server.exe`
- [ ] 无编译警告（/W3 级别）
- [ ] Qt SerialPort 模块正确链接

### 7.2 元数据

- [ ] `stdio.drv.modbusrtu_serial_server.exe --export-meta` 输出合法 JSON
- [ ] JSON 包含 16 个命令定义（status + 服务控制 + Unit 管理 + 数据访问 + 批量操作）
- [ ] 每个命令的参数 schema 与本文档一致
- [ ] 串口参数（port_name、baud_rate、parity 等）的枚举约束正确

### 7.3 DriverLab 交互

- [ ] DriverLab 可加载该 Driver 并展示命令列表
- [ ] 可通过 `start_server` 启动串口从站
- [ ] 使用外部 Modbus RTU 主站通过串口连接后，`data_written` 事件正常推送
- [ ] CRC16 校验正确，主站可正常读写数据
- [ ] T3.5 帧间隔检测正确，帧边界识别准确

### 7.4 异常场景

- [ ] 串口不存在或被占用时 `start_server` 返回 error code 1
- [ ] 主站发送 CRC 错误的帧时，从站不响应（静默丢弃）
- [ ] 操作不存在的 unit_id 返回 error code 3
- [ ] 传入非法 parity/stop_bits 值时，返回 error code 3
- [ ] 发送未知命令时返回 error code 404
