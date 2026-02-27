# Modbus RTU 串口驱动 — 功能需求文档

## 1. 功能概述

`driver_modbusrtu_serial` 是基于 stdiolink 框架的 Modbus RTU 串口驱动，通过 RS-232/RS-485 串口直接与 Modbus 从站设备通信。

与现有 `driver_modbusrtu`（RTU Over TCP）的关系：
- **复用**：`modbus_types.h/cpp`（数据类型枚举、字节序转换器）、CRC16 校验算法、RTU 帧构建与解析逻辑。
- **替换**：传输层从 `QTcpSocket`（TCP 网关）改为 `QSerialPort`（本地串口）。
- **新增**：串口参数配置（波特率、数据位、停止位、校验位）、T3.5 字符帧间隔定时。

输出可执行文件：`stdio.drv.modbusrtu_serial`（Windows 下为 `.exe`）。

---

## 2. 使用场景

| 场景 | 说明 |
|------|------|
| DriverLab 调试 | 在 WebUI DriverLab 中交互式执行命令，通过本地串口读写 Modbus 从站寄存器 |
| Server 编排调度 | 通过 `stdiolink_server` 将 Driver 注册为 Service，在 Project 中配置串口参数，由 ScheduleEngine 自动调度 |
| 自动化脚本 | 通过 stdin/stdout JSONL 协议直接与 Driver 进程通信，集成到自动化控制流程 |
| 无网关场景 | 工控现场无 TCP 网关时，PC 通过 USB-RS485 转换器直连 Modbus 从站 |

---

## 3. 详细功能说明

### 3.1 串口连接参数

所有需要访问从站的命令共享以下连接参数：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| port_name | string | 是 | — | 串口名称，如 `"COM3"`（Windows）或 `"/dev/ttyUSB0"`（Linux） |
| baud_rate | int | 否 | 9600 | 波特率：1200 / 2400 / 4800 / 9600 / 19200 / 38400 / 57600 / 115200 |
| data_bits | int | 否 | 8 | 数据位：7 / 8 |
| stop_bits | string | 否 | `"1"` | 停止位：`"1"` / `"1.5"` / `"2"` |
| parity | string | 否 | `"none"` | 校验位：`"none"` / `"even"` / `"odd"` |
| unit_id | int | 否 | 1 | 从站地址（1–247） |
| timeout | int | 否 | 3000 | 超时时间（ms），范围 100–30000 |

### 3.2 `status` — 驱动状态检查

基础健康检查命令，不访问串口。

- **参数**：无
- **返回**：`{"status": "ready"}`

### 3.3 `read_coils` — 读取线圈 (FC 0x01)

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| （串口连接参数） | — | — | — | 见 3.1 节 |
| address | int | 是 | — | 起始地址（0–65535） |
| count | int | 否 | 1 | 读取数量（1–2000） |

- **返回**：`{"values": [true, false, true]}`

### 3.4 `read_discrete_inputs` — 读取离散输入 (FC 0x02)

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| （串口连接参数） | — | — | — | 见 3.1 节 |
| address | int | 是 | — | 起始地址（0–65535） |
| count | int | 否 | 1 | 读取数量（1–2000） |

- **返回**：`{"values": [true, false]}`

### 3.5 `read_holding_registers` — 读取保持寄存器 (FC 0x03)

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| （串口连接参数） | — | — | — | 见 3.1 节 |
| address | int | 是 | — | 起始地址（0–65535） |
| count | int | 否 | 1 | 读取数量（1–125） |
| data_type | enum | 否 | `"uint16"` | 数据类型：int16 / uint16 / int32 / uint32 / float32 / int64 / uint64 / float64 |
| byte_order | enum | 否 | `"big_endian"` | 字节序：big_endian / little_endian / big_endian_byte_swap / little_endian_byte_swap |

- **返回**：`{"values": [100, 200], "raw": [100, 200]}`
- `values` 为按 data_type 和 byte_order 转换后的值，`raw` 为原始 uint16 寄存器值
- 当 `data_type` 为多字类型（如 int32 占 2 个寄存器、float64 占 4 个寄存器）时，`count` 指原始寄存器数量，必须是该类型所占寄存器数的整数倍，否则返回 error code 3
- 当 `data_type` 为 int64 / uint64 时，超过 JSON 安全整数范围（2^53）的值将以字符串形式返回

### 3.6 `read_input_registers` — 读取输入寄存器 (FC 0x04)

- **参数**：与 `read_holding_registers` 相同
- **返回**：与 `read_holding_registers` 相同

### 3.7 `write_coil` — 写单个线圈 (FC 0x05)

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| （串口连接参数） | — | — | — | 见 3.1 节 |
| address | int | 是 | — | 线圈地址（0–65535） |
| value | bool | 是 | — | 线圈值 |

- **返回**：`{"written": true}`

### 3.8 `write_coils` — 写多个线圈 (FC 0x0F)

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| （串口连接参数） | — | — | — | 见 3.1 节 |
| address | int | 是 | — | 起始地址（0–65535） |
| values | array | 是 | — | 线圈值数组（bool[]），如 `[true, false, true]`，长度 1–1968 |

- **返回**：`{"written": 3}`（写入数量）

### 3.9 `write_holding_register` — 写单个保持寄存器 (FC 0x06)

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| （串口连接参数） | — | — | — | 见 3.1 节 |
| address | int | 是 | — | 寄存器地址（0–65535） |
| value | int | 是 | — | 寄存器值（0–65535） |

- **返回**：`{"written": true}`

### 3.10 `write_holding_registers` — 写多个保持寄存器 (FC 0x10，带类型转换)

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| （串口连接参数） | — | — | — | 见 3.1 节 |
| address | int | 是 | — | 起始地址（0–65535） |
| value | double | 是 | — | 要写入的值 |
| data_type | enum | 否 | `"uint16"` | 数据类型（同 3.5 节） |
| byte_order | enum | 否 | `"big_endian"` | 字节序（同 3.5 节） |

- **返回**：`{"written": 2}`（写入寄存器数量，取决于 data_type）
- **说明**：此命令写入单个类型化值，按 data_type 自动展开为对应数量的寄存器（如 float32 → 2 个寄存器）。当 data_type 为 int64/uint64 时，value 受 JSON double 精度限制（2^53），超出范围的值应使用 `write_holding_registers_raw` 直接写入原始寄存器值。

### 3.11 `write_holding_registers_raw` — 写多个保持寄存器 (FC 0x10，原始值)

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| （串口连接参数） | — | — | — | 见 3.1 节 |
| address | int | 是 | — | 起始地址（0–65535） |
| values | array | 是 | — | uint16 寄存器值数组（每个元素 0–65535），如 `[100, 200]`，长度 1–123 |

- **返回**：`{"written": 2}`（写入寄存器数量）

---

## 4. 接口约定

### 4.1 JSONL 请求/响应格式

请求（stdin，每行一个 JSON）：
```json
{"cmd": "read_holding_registers", "data": {"port_name": "COM3", "baud_rate": 9600, "address": 0, "count": 10}}
{"cmd": "write_holding_register", "data": {"port_name": "/dev/ttyUSB0", "address": 100, "value": 1234}}
```

成功响应（stdout）：
```json
{"status": "done", "code": 0, "data": {"values": [100, 200], "raw": [100, 200]}}
```

错误响应（stdout）：
```json
{"status": "error", "code": 1, "data": {"message": "Failed to open serial port: COM3"}}
{"status": "error", "code": 2, "data": {"message": "CRC error"}}
{"status": "error", "code": 3, "data": {"message": "Server already running"}}
```

### 4.2 串口连接参数规范

| 参数 | 类型 | 范围 | 默认值 |
|------|------|------|--------|
| port_name | string | 合法串口名称 | （必填） |
| baud_rate | int | 1200 / 2400 / 4800 / 9600 / 19200 / 38400 / 57600 / 115200 | 9600 |
| data_bits | int | 7 / 8 | 8 |
| stop_bits | string | `"1"` / `"1.5"` / `"2"` | `"1"` |
| parity | string | `"none"` / `"even"` / `"odd"` | `"none"` |
| unit_id | int | 1–247 | 1 |
| timeout | int | 100–30000 ms | 3000 |

### 4.3 元数据导出

执行 `stdio.drv.modbusrtu_serial --export-meta` 输出完整 DriverMeta JSON，包含所有命令定义、参数 schema 及枚举约束，可被 DriverLab 和 SchemaEditor 直接消费。

---

## 5. 实现设计

### 5.1 目录结构

```
src/drivers/driver_modbusrtu_serial/
├── CMakeLists.txt
├── main.cpp
├── modbus_rtu_serial_client.h
└── modbus_rtu_serial_client.cpp
```

### 5.2 与现有代码的复用关系

| 模块 | 来源 | 复用方式 |
|------|------|----------|
| `modbus_types.h/cpp` | `driver_modbusrtu/` | 直接引用源文件（CMake `target_include_directories`） |
| CRC16 算法 | `modbus_rtu_client.cpp` | 提取为独立函数或在新客户端中复制（静态查找表 + `calculateCRC16`） |
| RTU 帧构建 (`buildRequest`) | `modbus_rtu_client.cpp` | 逻辑相同，在新客户端中实现 |
| 响应解析 (`parseReadBitsResponse` 等) | `modbus_rtu_client.cpp` | 逻辑相同，在新客户端中实现 |
| Handler 命令集 / Meta Builder | `driver_modbusrtu/main.cpp` | 参照实现，连接参数从 host/port 改为串口参数 |

### 5.3 `ModbusRtuSerialClient` 类设计

替换 `QTcpSocket` 为 `QSerialPort`，核心变更：

- **连接方法**：`bool open(const QString& portName, int baudRate, int dataBits, const QString& stopBits, const QString& parity)` 替代 `connectToServer(host, port)`
- **读写操作**：`QSerialPort::write()` / `QSerialPort::waitForReadyRead()` 替代 `QTcpSocket` 对应方法
- **T3.5 帧间隔**：发送请求前等待 3.5 个字符时间的静默期（按波特率计算），确保从站正确识别帧边界
- **响应接收**：发送请求后，通过 T3.5 静默间隔判定响应帧结束——收到数据后启动 T3.5 定时器，定时器超时前收到新数据则重置，超时后视为帧完整并进行 CRC16 校验
- **连接池 Key**：从 `{host, port}` 改为 `{portName, baudRate, dataBits, stopBits, parity}`（同一串口不同参数配置视为不同连接；同一串口不支持并发访问）

### 5.4 T3.5 帧间隔计算

Modbus RTU 标准要求帧间至少 3.5 个字符时间的静默：

```
T3.5 = 3.5 × (1 start + data_bits + parity_bit + stop_bits) / baud_rate × 1000 ms
```

- 波特率 ≤ 19200 时：严格按公式计算
- 波特率 > 19200 时：固定使用 1.75 ms（Modbus 标准建议）

### 5.5 CMake 依赖

需在 CMakeLists.txt 中添加 `Qt::SerialPort` 模块：

```cmake
find_package(Qt6 COMPONENTS Core Network SerialPort QUIET)
```

---

## 6. 边界条件与异常处理

### 6.1 串口异常

| 场景 | 错误码 | 处理 |
|------|--------|------|
| 串口不存在或被占用 | 1 | 返回 error，message 包含串口名称和系统错误描述 |
| 串口打开后意外断开（USB 拔出） | 1 | 返回 error，message 说明串口连接丢失 |
| 串口参数不合法（如波特率为 0） | 3 | 返回 error，提示合法值范围 |

### 6.2 参数校验

参数校验分两层：

- **元数据 Schema 校验**（框架自动执行）：必填字段缺失、类型不匹配、枚举值非法等，返回 error code **400**。
- **业务逻辑校验**（驱动代码执行）：返回 error code **3**。

| 场景 | 错误码 | 处理 |
|------|--------|------|
| 必填参数缺失、类型错误、枚举值非法 | 400 | 框架自动校验，返回 ValidationFailed |
| unit_id 超出 1–247 范围 | 3 | 返回 error，提示合法范围 |

### 6.3 Modbus 通讯异常

| 场景 | 错误码 | 处理 |
|------|--------|------|
| 从站返回异常码（0x01–0x0B） | 2 | 返回 error，message 包含异常码名称和十六进制值 |
| 响应超时（无应答） | 2 | 返回 error，message 说明超时 |
| CRC16 校验失败 | 2 | 返回 error，message 说明 CRC 校验错误 |
| 响应数据格式异常（长度不足） | 2 | 返回 error，message 说明解析失败 |

### 6.4 错误码汇总

| 错误码 | 来源 | 含义 |
|--------|------|------|
| 0 | 驱动 | 成功 |
| 1 | 驱动 | 串口打开/连接失败 |
| 2 | 驱动 | Modbus 通讯错误（超时、CRC 失败、异常码） |
| 3 | 驱动 | 业务逻辑校验失败 |
| 400 | 框架 | 元数据 Schema 校验失败 |
| 404 | 框架 | 未知命令 |

---

## 7. 验收标准

### 7.1 构建

- [ ] `build.bat` 成功编译，输出 `stdio.drv.modbusrtu_serial.exe`
- [ ] 无编译警告（/W3 级别）
- [ ] Qt SerialPort 模块正确链接

### 7.2 元数据

- [ ] `stdio.drv.modbusrtu_serial.exe --export-meta` 输出合法 JSON
- [ ] JSON 包含 10 个命令定义（status + 9 个 Modbus 操作命令）
- [ ] 每个命令的参数 schema 与本文档第 3 节一致
- [ ] 串口参数（port_name、baud_rate、parity 等）的枚举约束正确

### 7.3 DriverLab 交互

- [ ] DriverLab 可加载该 Driver 并展示命令列表
- [ ] 表单自动生成：枚举参数（baud_rate、parity、stop_bits、data_type、byte_order）显示为下拉框
- [ ] 必填参数（port_name、address）有标记
- [ ] 连接真实串口从站后，所有命令可正常执行并返回预期格式

### 7.4 异常场景

- [ ] 打开不存在的串口时，返回 error code 1
- [ ] 传入非法 parity/stop_bits 值时，返回 error code 3
- [ ] 缺少 port_name 时，返回 error code 3
- [ ] 从站无应答时，返回 error code 2（超时）
- [ ] 发送未知命令时，返回 error code 404
