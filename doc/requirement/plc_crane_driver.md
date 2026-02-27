# PLC 升降装置 Modbus TCP Driver — 功能需求文档

## 1. 功能概述

`driver_plc_crane` 是基于 stdiolink 框架的专用 Modbus TCP 驱动，用于控制高炉 PLC 升降装置（气缸升降 + 球阀开关）。

与通用 `driver_modbustcp` 的关系：
- **复用**：直接引用 `driver_modbustcp` 的 `modbus_client.h/cpp` 和 `modbus_types.h/cpp`，不重复实现 Modbus TCP 协议栈。
- **封装**：将原始寄存器地址和功能码映射为语义化命令（`read_status`、`set_mode`、`cylinder_control` 等），屏蔽底层协议细节。
- **约束**：仅暴露该 PLC 设备所需的寄存器子集，参数值域受限于协议文档定义的合法枚举。

输出可执行文件：`stdio.drv.plc_crane`（Windows 下为 `.exe`）。

---

## 2. 使用场景

| 场景 | 说明 |
|------|------|
| DriverLab 调试 | 在 WebUI DriverLab 中交互式执行命令，实时查看气缸/阀门状态，手动控制升降与阀门 |
| Server 编排调度 | 通过 `stdiolink_server` 将 Driver 注册为 Service，在 Project 中配置 PLC 连接参数，由 ScheduleEngine 自动调度 |
| 自动化脚本 | 通过 stdin/stdout JSONL 协议直接与 Driver 进程通信，集成到自动化控制流程 |

---

## 3. 详细功能说明

### 3.1 `status` — 驱动状态检查

基础健康检查命令，不访问 PLC。

- **参数**：无
- **返回**：`{"status": "ready"}`

### 3.2 `read_status` — 读取设备状态

一次性读取气缸和球阀的全部到位信号。

- **Modbus 操作**：读离散量输入 (FC 0x02)，地址 9–10、13–14
- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| host | string | 是 | — | PLC IP 地址 |
| port | int | 否 | 502 | Modbus TCP 端口 |
| unit_id | int | 否 | 1 | 从站地址 |
| timeout | int | 否 | 3000 | 超时时间 (ms) |

- **返回**：

```json
{
  "cylinder_up": true,
  "cylinder_down": false,
  "valve_open": false,
  "valve_closed": true
}
```

- **寄存器映射**：

| 字段 | 离散输入地址 | 含义 |
|------|-------------|------|
| cylinder_up | 9 | 气缸上升到位 |
| cylinder_down | 10 | 气缸下降到位 |
| valve_open | 13 | 球阀打开到位 |
| valve_closed | 14 | 球阀关闭到位 |

### 3.3 `set_mode` — 设置手动/自动模式

- **Modbus 操作**：写单个寄存器 (FC 0x06)，地址 3
- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| host | string | 是 | — | PLC IP 地址 |
| port | int | 否 | 502 | Modbus TCP 端口 |
| unit_id | int | 否 | 1 | 从站地址 |
| timeout | int | 否 | 3000 | 超时时间 (ms) |
| mode | string | 是 | — | `"manual"` 或 `"auto"` |

- **寄存器映射**：`manual` → 写入 0，`auto` → 写入 1
- **返回**：`{"written": true, "mode": "manual"}`

### 3.4 `set_run` — 启动/停止

- **Modbus 操作**：写单个寄存器 (FC 0x06)，地址 2
- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| host | string | 是 | — | PLC IP 地址 |
| port | int | 否 | 502 | Modbus TCP 端口 |
| unit_id | int | 否 | 1 | 从站地址 |
| timeout | int | 否 | 3000 | 超时时间 (ms) |
| action | string | 是 | — | `"start"` 或 `"stop"` |

- **寄存器映射**：`start` → 写入 1，`stop` → 写入 0
- **返回**：`{"written": true, "action": "start"}`

### 3.5 `cylinder_control` — 气缸升降控制（手动模式）

- **Modbus 操作**：写单个寄存器 (FC 0x06)，地址 0
- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| host | string | 是 | — | PLC IP 地址 |
| port | int | 否 | 502 | Modbus TCP 端口 |
| unit_id | int | 否 | 1 | 从站地址 |
| timeout | int | 否 | 3000 | 超时时间 (ms) |
| action | string | 是 | — | `"up"` / `"down"` / `"stop"` |

- **寄存器映射**：`up` → 写入 1，`down` → 写入 2，`stop` → 写入 0
- **返回**：`{"written": true, "action": "up"}`

### 3.6 `valve_control` — 阀门开关控制（手动模式）

- **Modbus 操作**：写单个寄存器 (FC 0x06)，地址 1
- **参数**：

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| host | string | 是 | — | PLC IP 地址 |
| port | int | 否 | 502 | Modbus TCP 端口 |
| unit_id | int | 否 | 1 | 从站地址 |
| timeout | int | 否 | 3000 | 超时时间 (ms) |
| action | string | 是 | — | `"open"` / `"close"` / `"stop"` |

- **寄存器映射**：`open` → 写入 1，`close` → 写入 2，`stop` → 写入 0
- **返回**：`{"written": true, "action": "open"}`

---

## 4. 接口约定

### 4.1 JSONL 请求/响应格式

请求（stdin，每行一个 JSON）：
```json
{"cmd": "read_status", "data": {"host": "192.168.1.10", "port": 502}}
{"cmd": "cylinder_control", "data": {"host": "192.168.1.10", "action": "up"}}
```

成功响应（stdout）：
```json
{"done": {"code": 0, "data": {"cylinder_up": true, "cylinder_down": false, "valve_open": false, "valve_closed": true}}}
```

错误响应（stdout）：
```json
{"error": {"code": 1, "data": {"message": "Failed to connect to 192.168.1.10"}}}
{"error": {"code": 2, "data": {"message": "Modbus exception: Illegal Data Address (0x02)"}}}
{"error": {"code": 3, "data": {"message": "Invalid action: 'foo', expected: up, down, stop"}}}
```

### 4.2 连接参数规范

| 参数 | 类型 | 范围 | 默认值 |
|------|------|------|--------|
| host | string | 合法 IP/主机名 | （必填） |
| port | int | 1–65535 | 502 |
| unit_id | int | 1–247 | 1 |
| timeout | int | 100–30000 ms | 3000 |

### 4.3 元数据导出

执行 `stdio.drv.plc_crane --export-meta` 输出完整 DriverMeta JSON，包含所有命令定义、参数 schema 及枚举约束，可被 DriverLab 和 SchemaEditor 直接消费。

---

## 5. 边界条件与异常处理

### 5.1 连接异常

| 场景 | 错误码 | 处理 |
|------|--------|------|
| TCP 连接失败（主机不可达/端口未监听） | 1 | 返回 error，message 包含目标地址 |
| 连接超时 | 1 | 同上，由 ModbusClient 内部 timeout 控制 |

### 5.2 参数校验

| 场景 | 错误码 | 处理 |
|------|--------|------|
| `set_mode` 的 mode 不是 `manual`/`auto` | 3 | 返回 error，提示合法值 |
| `set_run` 的 action 不是 `start`/`stop` | 3 | 返回 error，提示合法值 |
| `cylinder_control` 的 action 不是 `up`/`down`/`stop` | 3 | 返回 error，提示合法值 |
| `valve_control` 的 action 不是 `open`/`close`/`stop` | 3 | 返回 error，提示合法值 |
| 缺少必填参数 host | 3 | 返回 error，提示缺少 host |

### 5.3 Modbus 通讯异常

| 场景 | 错误码 | 处理 |
|------|--------|------|
| PLC 返回异常码（0x01–0x0B） | 2 | 返回 error，message 包含异常码名称和十六进制值 |
| 响应超时（无应答） | 2 | 返回 error，message 说明超时 |
| 响应数据格式异常 | 2 | 返回 error，message 说明解析失败 |

### 5.4 错误码汇总

| 错误码 | 含义 |
|--------|------|
| 0 | 成功 |
| 1 | 连接失败 |
| 2 | Modbus 通讯错误 |
| 3 | 参数校验失败 |
| 404 | 未知命令 |

---

## 6. 验收标准

### 6.1 构建

- [ ] `build.bat` 成功编译，输出 `stdio.drv.plc_crane.exe`
- [ ] 无编译警告（/W3 级别）

### 6.2 元数据

- [ ] `stdio.drv.plc_crane.exe --export-meta` 输出合法 JSON
- [ ] JSON 包含 6 个命令定义（status、read_status、set_mode、set_run、cylinder_control、valve_control）
- [ ] 每个命令的参数 schema 与本文档第 3 节一致

### 6.3 DriverLab 交互

- [ ] DriverLab 可加载该 Driver 并展示命令列表
- [ ] 表单自动生成：枚举参数显示为下拉框，必填参数有标记
- [ ] 连接真实 PLC 后，所有命令可正常执行并返回预期格式

### 6.4 异常场景

- [ ] 连接不存在的 IP 时，返回 error code 1
- [ ] 传入非法 action/mode 值时，返回 error code 3
- [ ] 发送未知命令时，返回 error code 404
