# Modbus RTU Over TCP Server 从站驱动 — 功能需求文档

## 1. 功能概述

`driver_modbusrtu_server` 是基于 stdiolink 框架的 Modbus RTU Over TCP 从站驱动，作为 Modbus RTU Slave 监听 TCP 端口，使用 RTU 帧格式（带 CRC16 校验）通过 TCP 通信。

核心能力：
- **RTU 帧格式**：使用 `[Unit ID][FC][Data][CRC16]` 帧格式，无 MBAP 头，通过 CRC16 校验数据完整性。
- **多 Unit 支持**：可注册多个从站地址，每个 Unit 拥有独立的四类数据区。
- **完整功能码**：支持 8 种标准 Modbus 功能码（FC 0x01–0x06、0x0F、0x10）。
- **事件推送**：主站写入数据时，通过 `event()` 机制实时通知上层应用。
- **KeepAlive 生命周期**：启动后持续运行（需在 `main.cpp` 中设置 `Profile::KeepAlive`，或通过 `--profile=keepalive` 启动）。

与 `driver_modbustcp_server`（Modbus TCP 从站）的关系：
- **共享**：命令集、数据区管理、Unit 管理逻辑完全相同。
- **差异**：协议层从 MBAP 头解析改为 RTU 帧 CRC16 校验，响应帧也带 CRC16。
- **复用**：CRC16 算法复用 `driver_modbusrtu/modbus_rtu_client.cpp` 中的实现。

输出可执行文件：`stdio.drv.modbusrtu_server`（Windows 下为 `.exe`）。

---

## 2. 使用场景

| 场景 | 说明 |
|------|------|
| DriverLab 调试 | 在 WebUI DriverLab 中启动 RTU 从站服务，模拟 RTU 网关设备 |
| Server 编排调度 | 通过 `stdiolink_server` 注册为 Service，由 ScheduleEngine 管理生命周期 |
| 设备模拟 | 模拟 RTU 网关从站，用于 RTU Over TCP 主站驱动的开发和测试 |
| 自动化测试 | 在 CI/CD 流程中启动虚拟 RTU 从站，配合主站驱动进行端到端测试 |

---

## 3. 详细功能说明

### 3.1 监听参数

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| listen_port | int | 否 | 502 | TCP 监听端口（1–65535） |

### 3.2 命令集

命令集与 `driver_modbustcp_server` 一致，包含 16 个命令：

| 命令 | 说明 |
|------|------|
| `status` | 驱动健康检查，返回服务状态、监听端口、已注册 Unit 列表 |
| `start_server` | 启动 TCP 监听，参数：`listen_port` |
| `stop_server` | 停止监听，断开所有主站连接 |
| `add_unit` | 添加从站 Unit，参数：`unit_id`、`data_area_size` |
| `remove_unit` | 移除从站 Unit，参数：`unit_id` |
| `list_units` | 列出所有已注册 Unit |
| `set_coil` | 设置线圈值，参数：`unit_id`、`address`、`value` |
| `get_coil` | 读取线圈值，参数：`unit_id`、`address` |
| `set_discrete_input` | 设置离散输入值 |
| `get_discrete_input` | 读取离散输入值 |
| `set_holding_register` | 设置保持寄存器值 |
| `get_holding_register` | 读取保持寄存器值 |
| `set_input_register` | 设置输入寄存器值 |
| `get_input_register` | 读取输入寄存器值 |
| `set_registers_batch` | 批量设置寄存器（支持类型转换） |
| `get_registers_batch` | 批量读取寄存器（支持类型转换） |

各命令的参数定义、返回值格式与 `modbustcp_server_driver.md` 第 3 节完全相同，此处不再重复。

### 3.3 事件推送（KeepAlive 模式）

与 `driver_modbustcp_server` 相同：

| 事件名 | 触发条件 | 数据 |
|--------|----------|------|
| `client_connected` | 主站建立 TCP 连接 | `{"address": "192.168.1.100", "port": 54321}` |
| `client_disconnected` | 主站断开 TCP 连接 | `{"address": "192.168.1.100", "port": 54321}` |
| `data_written` | 主站写入数据（FC 0x05/0x06/0x0F/0x10） | `{"unit_id": 1, "function_code": 6, "address": 100, "quantity": 1}` |

---

## 4. 接口约定

### 4.1 JSONL 请求/响应格式

请求（stdin，每行一个 JSON）：
```json
{"cmd": "start_server", "data": {"listen_port": 502}}
{"cmd": "add_unit", "data": {"unit_id": 1}}
{"cmd": "set_holding_register", "data": {"unit_id": 1, "address": 100, "value": 1234}}
```

成功响应（stdout）：
```json
{"status": "done", "code": 0, "data": {"started": true, "port": 502}}
```

事件推送（stdout）：
```json
{"status": "event", "code": 0, "data": {"event": "data_written", "data": {"unit_id": 1, "function_code": 6, "address": 100, "quantity": 1}}}
```

错误响应（stdout）：
```json
{"status": "error", "code": 1, "data": {"message": "Failed to listen on port 502: Address already in use"}}
{"status": "error", "code": 3, "data": {"message": "Unit 1 already exists"}}
```

各命令的参数定义、返回值格式与 `modbustcp_server_driver.md` 第 3 节一致。

### 4.2 RTU 帧格式（与主站通信）

请求帧（主站 → 从站）：
```
[Unit ID (1)] [FC (1)] [Data (N)] [CRC16 (2)]
```

响应帧（从站 → 主站）：
```
[Unit ID (1)] [FC (1)] [Data (N)] [CRC16 (2)]
```

异常响应帧：
```
[Unit ID (1)] [FC|0x80 (1)] [Exception Code (1)] [CRC16 (2)]
```

与 Modbus TCP 的区别：
- 无 MBAP 头（无 Transaction ID、Protocol ID、Length 字段）
- 每帧末尾附加 CRC16 校验（Modbus 标准多项式 0xA001）
- 帧边界检测策略：接收数据后启动超时定时器（建议 50ms），超时后对缓冲区进行 CRC16 校验；校验通过则视为完整帧，校验失败则丢弃缓冲区并等待下一帧。缓冲区最大长度限制为 256 字节（Modbus RTU 最大 ADU 长度）。

### 4.3 元数据导出

执行 `stdio.drv.modbusrtu_server --export-meta` 输出完整 DriverMeta JSON。

---

## 5. 实现设计

### 5.1 目录结构

```
src/drivers/driver_modbusrtu_server/
├── CMakeLists.txt
├── main.cpp
├── modbus_rtu_server.h
└── modbus_rtu_server.cpp
```

### 5.2 与现有代码的复用关系

| 模块 | 来源 | 复用方式 |
|------|------|----------|
| CRC16 算法 | `driver_modbusrtu/modbus_rtu_client.cpp` | 复用静态查找表 + `calculateCRC16` |
| 数据区管理 | `tmp/modbustcpserver.h/cpp` | 复用 `ModbusDataArea`、Unit 管理、数据访问接口 |
| 功能码处理 | `tmp/modbustcpserver.cpp` | 复用 8 个 `handle*` 方法的业务逻辑 |
| `modbus_types.h/cpp` | `driver_modbusrtu/` | 引用数据类型和字节序转换 |

### 5.3 与 TCP Server 的核心差异

| 方面 | Modbus TCP Server | RTU Over TCP Server |
|------|-------------------|---------------------|
| 帧头 | MBAP (7 bytes) | 无 |
| 校验 | 无（TCP 保证） | CRC16 (2 bytes) |
| 帧边界 | MBAP Length 字段 | CRC16 验证 + 超时检测 |
| 请求解析 | `parseHeader()` → `processRequest()` | `verifyCRC()` → `processRtuRequest()` |
| 响应构建 | `buildResponse()` 添加 MBAP 头 | `buildRtuResponse()` 添加 CRC16 |

---

## 6. 边界条件与异常处理

### 6.1 服务异常

| 场景 | 错误码 | 处理 |
|------|--------|------|
| 端口被占用，无法监听 | 1 | 返回 error，message 包含端口号和系统错误描述 |
| 服务已在运行时再次 start | 3 | 返回 error，提示服务已在运行 |
| 服务未运行时调用 stop | 3 | 返回 error，提示服务未运行 |

### 6.2 参数校验

参数校验分两层：

- **元数据 Schema 校验**（框架自动执行）：必填字段缺失、类型不匹配、枚举值非法等，返回 error code **400**。
- **业务逻辑校验**（驱动代码执行）：运行时状态冲突、资源不存在等，返回 error code **3**。

与 `driver_modbustcp_server` 相同（unit_id 重复添加、不存在时操作、地址越界等）。

### 6.3 RTU 协议异常

| 场景 | 处理 |
|------|------|
| 主站请求 CRC16 校验失败 | 丢弃帧，不响应（RTU 标准行为） |
| 主站请求帧长度不足 | 丢弃帧，不响应 |
| 不支持的功能码 | 返回异常响应（FC\|0x80 + 异常码 0x01） |
| 地址越界 | 返回异常响应（异常码 0x02） |
| Unit ID 不存在 | 返回异常响应（异常码 0x0B）。RTU Over TCP 视为网关语义，需向主站明确报告目标设备不可达；与串口 RTU 从站（静默不响应）行为不同。 |

### 6.4 错误码汇总

| 错误码 | 来源 | 含义 |
|--------|------|------|
| 0 | 驱动 | 成功 |
| 1 | 驱动 | 服务启动失败 |
| 3 | 驱动 | 业务逻辑校验失败 / 状态冲突 |
| 400 | 框架 | 元数据 Schema 校验失败 |
| 404 | 框架 | 未知命令 |

---

## 7. 验收标准

### 7.1 构建

- [ ] `build.bat` 成功编译，输出 `stdio.drv.modbusrtu_server.exe`
- [ ] 无编译警告（/W3 级别）

### 7.2 元数据

- [ ] `stdio.drv.modbusrtu_server.exe --export-meta` 输出合法 JSON
- [ ] JSON 包含 16 个命令定义
- [ ] 每个命令的参数 schema 与本文档一致

### 7.3 DriverLab 交互

- [ ] DriverLab 可加载该 Driver 并展示命令列表
- [ ] 可通过 `start_server` 启动从站
- [ ] 使用 RTU Over TCP 主站工具连接后，`data_written` 事件正常推送
- [ ] CRC16 校验正确，主站可正常读写数据

### 7.4 异常场景

- [ ] 端口被占用时 `start_server` 返回 error code 1
- [ ] 主站发送 CRC 错误的帧时，从站不响应（静默丢弃）
- [ ] 操作不存在的 unit_id 返回 error code 3
- [ ] 发送未知命令时返回 error code 404
