# Modbus TCP Server 从站驱动 — 功能需求文档

## 1. 功能概述

`driver_modbustcp_server` 是基于 stdiolink 框架的 Modbus TCP 从站驱动，作为 Modbus TCP Slave 监听指定端口，接收主站请求并响应。

核心能力：
- **多 Unit 支持**：可注册多个从站地址（Unit ID），每个 Unit 拥有独立的四类数据区（线圈、离散输入、保持寄存器、输入寄存器）。
- **完整功能码**：支持 8 种标准 Modbus 功能码（FC 0x01–0x06、0x0F、0x10）。
- **事件推送**：主站写入数据时，通过 `event()` 机制实时通知上层应用。
- **KeepAlive 生命周期**：启动后持续运行，通过命令控制服务启停和数据读写。

与参考实现 `tmp/modbustcpserver.h/cpp` 的关系：
- **封装**：将 `ModbusTcpServer` 类的功能映射为 stdiolink JSONL 命令接口。
- **协议**：使用标准 Modbus TCP 协议（MBAP 头 + PDU），由 `QTcpServer` 处理 TCP 连接。

输出可执行文件：`stdio.drv.modbustcp_server`（Windows 下为 `.exe`）。

---

## 2. 使用场景

| 场景 | 说明 |
|------|------|
| DriverLab 调试 | 在 WebUI DriverLab 中启动从站服务，手动设置寄存器值，观察主站读写行为 |
| Server 编排调度 | 通过 `stdiolink_server` 注册为 Service，在 Project 中配置监听参数，由 ScheduleEngine 管理生命周期 |
| 设备模拟 | 模拟 PLC 等 Modbus 从站设备，用于主站驱动开发和集成测试 |
| 自动化测试 | 在 CI/CD 流程中启动虚拟从站，配合主站驱动进行端到端自动化测试 |

---

## 3. 详细功能说明

### 3.1 监听参数

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| listen_port | int | 否 | 502 | TCP 监听端口（1–65535） |

### 3.2 `status` — 驱动状态检查

基础健康检查命令，不操作服务。

- **参数**：无
- **返回**（服务运行中）：`{"status": "ready", "listening": true, "port": 502, "units": [1, 2]}`
- **返回**（服务未启动）：`{"status": "ready", "listening": false, "units": []}`

### 3.3 `start_server` — 启动从站服务

启动 TCP 监听，开始接受主站连接。

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| listen_port | int | 否 | 502 | TCP 监听端口 |

- **返回**：`{"started": true, "port": 502}`
- **说明**：如果服务已在运行，返回 error code 3

### 3.4 `stop_server` — 停止从站服务

停止 TCP 监听，断开所有主站连接。

- **参数**：无
- **返回**：`{"stopped": true}`
- **说明**：如果服务未运行，返回 error code 3

### 3.5 `add_unit` — 添加从站 Unit

注册一个新的从站地址，创建独立数据区。

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| unit_id | int | 是 | — | 从站地址（1–247） |
| data_area_size | int | 否 | 10000 | 每类数据区的容量（地址数量），范围 1–65536 |

- **返回**：`{"added": true, "unit_id": 1, "data_area_size": 10000}`
- **说明**：如果 unit_id 已存在，返回 error code 3。启动时不预置任何 Unit，需通过 `add_unit` 显式添加。

### 3.6 `remove_unit` — 移除从站 Unit

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| unit_id | int | 是 | — | 从站地址 |

- **返回**：`{"removed": true, "unit_id": 1}`

### 3.7 `list_units` — 列出所有 Unit

- **参数**：无
- **返回**：`{"units": [1, 2, 3]}`

### 3.8 `set_coil` — 设置线圈值

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| unit_id | int | 是 | — | 从站地址 |
| address | int | 是 | — | 线圈地址（0–65535） |
| value | bool | 是 | — | 线圈值 |

- **返回**：`{"written": true}`

### 3.9 `get_coil` — 读取线圈值

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| unit_id | int | 是 | — | 从站地址 |
| address | int | 是 | — | 线圈地址（0–65535） |

- **返回**：`{"value": true}`

### 3.10 `set_discrete_input` — 设置离散输入值

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| unit_id | int | 是 | — | 从站地址 |
| address | int | 是 | — | 离散输入地址（0–65535） |
| value | bool | 是 | — | 输入值 |

- **返回**：`{"written": true}`

### 3.11 `get_discrete_input` — 读取离散输入值

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| unit_id | int | 是 | — | 从站地址 |
| address | int | 是 | — | 离散输入地址（0–65535） |

- **返回**：`{"value": false}`

### 3.12 `set_holding_register` — 设置保持寄存器值

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| unit_id | int | 是 | — | 从站地址 |
| address | int | 是 | — | 寄存器地址（0–65535） |
| value | int | 是 | — | 寄存器值（0–65535） |

- **返回**：`{"written": true}`

### 3.13 `get_holding_register` — 读取保持寄存器值

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| unit_id | int | 是 | — | 从站地址 |
| address | int | 是 | — | 寄存器地址（0–65535） |

- **返回**：`{"value": 1234}`

### 3.14 `set_input_register` — 设置输入寄存器值

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| unit_id | int | 是 | — | 从站地址 |
| address | int | 是 | — | 寄存器地址（0–65535） |
| value | int | 是 | — | 寄存器值（0–65535） |

- **返回**：`{"written": true}`

### 3.15 `get_input_register` — 读取输入寄存器值

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| unit_id | int | 是 | — | 从站地址 |
| address | int | 是 | — | 寄存器地址（0–65535） |

- **返回**：`{"value": 5678}`

### 3.16 `set_registers_batch` — 批量设置寄存器

支持一次设置多个连续寄存器，可指定数据类型进行自动转换。

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| unit_id | int | 是 | — | 从站地址 |
| area | enum | 是 | — | 数据区：`"holding"` / `"input"` |
| address | int | 是 | — | 起始地址（0–65535） |
| values | array | 是 | — | 值数组 |
| data_type | enum | 否 | `"uint16"` | 数据类型：int16 / uint16 / int32 / uint32 / float32 |
| byte_order | enum | 否 | `"big_endian"` | 字节序：big_endian / little_endian / big_endian_byte_swap / little_endian_byte_swap |

- **返回**：`{"written": 4}`（写入寄存器数量）

### 3.17 `get_registers_batch` — 批量读取寄存器

- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| unit_id | int | 是 | — | 从站地址 |
| area | enum | 是 | — | 数据区：`"holding"` / `"input"` |
| address | int | 是 | — | 起始地址（0–65535） |
| count | int | 是 | — | 读取数量（1–125） |
| data_type | enum | 否 | `"uint16"` | 数据类型：int16 / uint16 / int32 / uint32 / float32 |
| byte_order | enum | 否 | `"big_endian"` | 字节序：big_endian / little_endian / big_endian_byte_swap / little_endian_byte_swap |

- **返回**：`{"values": [100, 200, 300], "raw": [100, 200, 300]}`

### 3.18 事件推送（KeepAlive 模式）

服务运行期间，通过 `event()` 机制向 stdout 推送以下事件：

| 事件名 | 触发条件 | 数据 |
|--------|----------|------|
| `client_connected` | 主站建立 TCP 连接 | `{"address": "192.168.1.100", "port": 54321}` |
| `client_disconnected` | 主站断开 TCP 连接 | `{"address": "192.168.1.100", "port": 54321}` |
| `data_written` | 主站通过 Modbus 协议写入数据（FC 0x05/0x06/0x0F/0x10），本地 `set_*` 命令不触发 | `{"unit_id": 1, "function_code": 6, "address": 100, "quantity": 1}` |

---

## 4. 接口约定

### 4.1 JSONL 请求/响应格式

请求（stdin，每行一个 JSON）：
```json
{"cmd": "start_server", "data": {"listen_port": 502}}
{"cmd": "add_unit", "data": {"unit_id": 1, "data_area_size": 10000}}
{"cmd": "set_holding_register", "data": {"unit_id": 1, "address": 100, "value": 1234}}
```

成功响应（stdout）：
```json
{"status": "done", "code": 0, "data": {"started": true, "port": 502}}
```

事件推送（stdout）：
```json
{"status": "event", "code": 0, "data": {"event": "client_connected", "data": {"address": "192.168.1.100", "port": 54321}}}
{"status": "event", "code": 0, "data": {"event": "data_written", "data": {"unit_id": 1, "function_code": 6, "address": 100, "quantity": 1}}}
```

错误响应（stdout）：
```json
{"status": "error", "code": 1, "data": {"message": "Failed to listen on port 502: Address already in use"}}
{"status": "error", "code": 3, "data": {"message": "Unit 1 already exists"}}
```

### 4.2 监听参数规范

| 参数 | 类型 | 范围 | 默认值 |
|------|------|------|--------|
| listen_port | int | 1–65535 | 502 |

### 4.3 Modbus 功能码支持

| 功能码 | 名称 | 操作 |
|--------|------|------|
| 0x01 | Read Coils | 读线圈 |
| 0x02 | Read Discrete Inputs | 读离散输入 |
| 0x03 | Read Holding Registers | 读保持寄存器 |
| 0x04 | Read Input Registers | 读输入寄存器 |
| 0x05 | Write Single Coil | 写单个线圈 |
| 0x06 | Write Single Register | 写单个保持寄存器 |
| 0x0F | Write Multiple Coils | 写多个线圈 |
| 0x10 | Write Multiple Registers | 写多个保持寄存器 |

### 4.4 元数据导出

执行 `stdio.drv.modbustcp_server --export-meta` 输出完整 DriverMeta JSON，可被 DriverLab 和 SchemaEditor 直接消费。

---

## 5. 实现设计

### 5.1 目录结构

```
src/drivers/driver_modbustcp_server/
├── CMakeLists.txt
├── main.cpp
├── modbus_tcp_server.h
└── modbus_tcp_server.cpp
```

### 5.2 与参考实现的复用关系

| 模块 | 来源 | 复用方式 |
|------|------|----------|
| `ModbusTcpServer` 类 | `tmp/modbustcpserver.h/cpp` | 移植并适配，移除 `applog.h` 依赖，改用 Qt 标准日志 |
| `ModbusDataArea` 结构 | `tmp/modbustcpserver.h` | 直接复用，四类数据区 + 可配置大小 |
| MBAP 头解析 / 粘包处理 | `tmp/modbustcpserver.cpp` | 直接复用 `processBuffer()` 逻辑 |
| 功能码处理函数 | `tmp/modbustcpserver.cpp` | 直接复用 8 个 `handle*` 方法 |
| `modbus_types.h/cpp` | `driver_modbusrtu/` | 引用数据类型和字节序转换（用于批量操作） |

### 5.3 KeepAlive 模式

驱动使用 `DriverCore::Profile::KeepAlive`（需在 `main.cpp` 中显式设置，或通过命令行 `--profile=keepalive` 启动），启动后持续运行：
- `start_server` 命令启动 `QTcpServer` 监听
- 主站连接/断开、数据写入通过 Qt 信号触发 `event()` 推送
- `stop_server` 命令停止监听并断开所有连接
- 进程退出时自动清理

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

| 场景 | 错误码 | 处理 |
|------|--------|------|
| 必填参数缺失、类型错误、枚举值非法 | 400 | 框架自动校验，返回 ValidationFailed |
| unit_id 已存在时 add_unit | 3 | 返回 error，提示已存在 |
| unit_id 不存在时操作数据 | 3 | 返回 error，提示 unit 不存在 |
| address 超出数据区范围 | 3 | 返回 error，提示地址越界 |

### 6.3 Modbus 协议异常

从站自动处理主站请求中的协议异常，返回标准 Modbus 异常响应：

| 场景 | 异常码 | 说明 |
|------|--------|------|
| 不支持的功能码 | 0x01 (Illegal Function) | 自动返回异常响应 |
| 地址越界 | 0x02 (Illegal Data Address) | 自动返回异常响应 |
| 数据值非法 | 0x03 (Illegal Data Value) | 自动返回异常响应 |
| Unit ID 不存在 | 0x0B (Gateway Target Device Failed) | 自动返回异常响应 |

### 6.4 错误码汇总

| 错误码 | 来源 | 含义 |
|--------|------|------|
| 0 | 驱动 | 成功 |
| 1 | 驱动 | 服务启动失败（端口占用等） |
| 3 | 驱动 | 业务逻辑校验失败 / 状态冲突 |
| 400 | 框架 | 元数据 Schema 校验失败（必填缺失、类型错误、枚举非法） |
| 404 | 框架 | 未知命令 |

---

## 7. 验收标准

### 7.1 构建

- [ ] `build.bat` 成功编译，输出 `stdio.drv.modbustcp_server.exe`
- [ ] 无编译警告（/W3 级别）

### 7.2 元数据

- [ ] `stdio.drv.modbustcp_server.exe --export-meta` 输出合法 JSON
- [ ] JSON 包含 16 个命令定义（status + 服务控制 + Unit 管理 + 数据访问 + 批量操作）
- [ ] 每个命令的参数 schema 与本文档第 3 节一致

### 7.3 DriverLab 交互

- [ ] DriverLab 可加载该 Driver 并展示命令列表
- [ ] 可通过 `start_server` 启动从站，`add_unit` 添加 Unit
- [ ] 使用外部 Modbus 主站工具连接后，`data_written` 事件正常推送
- [ ] 通过 `set_holding_register` / `get_holding_register` 可读写数据

### 7.4 异常场景

- [ ] 端口被占用时 `start_server` 返回 error code 1
- [ ] 重复 `start_server` 返回 error code 3
- [ ] 操作不存在的 unit_id 返回 error code 3
- [ ] 地址越界时返回 error code 3
- [ ] 发送未知命令时返回 error code 404
