# 里程碑 85：Modbus Server 从站驱动 Service 模板与 Project 配置

> **前置条件**: M79 (ModbusTCP Server)、M80 (ModbusRTU Server)、M81 (ModbusRTU Serial Server)、M84 (event_mode 支持)
> **目标**: 为三个 Modbus 从站驱动创建 Service 模板（manifest.json + config.schema.json + index.js）和 Project 配置，使其可通过 WebUI / REST API 进行管理与编排

## 1. 目标

- 新增 `modbustcp_server_service` Service 模板（3 文件），封装 `stdio.drv.modbustcp_server` 驱动的启动、Unit 注册与状态查询
- 新增 `modbusrtu_server_service` Service 模板（3 文件），封装 `stdio.drv.modbusrtu_server` 驱动的启动、Unit 注册与状态查询
- 新增 `modbusrtu_serial_server_service` Service 模板（3 文件），封装 `stdio.drv.modbusrtu_serial_server` 驱动的启动、Unit 注册与状态查询
- 新增 3 个 Project 配置文件（manual 调度，enabled=false，合理默认值）
- 所有 Service 的 index.js 遵循 keepalive 模式：打开驱动 → start_server → 循环 add_unit → status → 不调用 `$close()`
- Service Scanner 自动发现新 Service，REST API `/api/services` 可列出，WebUI 可展示

## 2. 背景与问题

- M79–M81 已实现三个 Modbus 从站驱动的 C++ 可执行文件，但它们只能通过 stdin/stdout 手动交互或 DriverLab 调试
- 缺少 Service 模板意味着无法通过 ServerManager 的 Project/Instance 机制进行生命周期管理（启动、停止、调度）
- 工业场景中 Modbus 从站通常需要长期运行，通过 Service + Project 机制可实现 daemon 调度、配置持久化、WebUI 可视化管理

**范围**:
- 三个 Service 目录各含 manifest.json、config.schema.json、index.js
- 三个 Project JSON 文件（manual 调度，enabled=false）
- config.schema.json 暴露 start_server 和 add_unit 的关键参数
- index.js 使用 stdiolink JS 绑定（openDriver、getConfig、createLogger、findDriverPath）

**非目标**:
- 不修改 C++ 驱动代码（M79–M84 已完成）
- 不修改 stdiolink 核心库、stdiolink_service 或 stdiolink_server
- 不新增 JS 绑定 API
- 不实现数据访问命令（set_coil/get_register 等）的 Service 封装——属于 DriverLab 交互式调试范畴
- 不实现 daemon/fixed_rate 调度的 Project（仅 manual）
- 不包含 modbusrtu_serial（主站）和 plc_crane（PLC 升降装置）驱动

## 3. 技术要点

### 3.1 Service 三文件结构约定

每个 Service 目录包含三个文件，遵循现有 `driver_pipeline_service` 等的约定：

| 文件 | 用途 | 关键字段 |
|------|------|----------|
| `manifest.json` | 服务身份声明 | `manifestVersion`, `id`, `name`, `version`, `description` |
| `config.schema.json` | 配置字段定义 | 每个字段含 `type`, `default`, `description`, 可选 `constraints`/`enum` |
| `index.js` | ES Module 入口脚本 | 使用 stdiolink 绑定，读取 config，驱动编排逻辑 |

manifest.json 示例（TCP）：
```json
{
  "manifestVersion": "1",
  "id": "modbustcp_server_service",
  "name": "Modbus TCP Server Service",
  "version": "1.0.0",
  "description": "Modbus TCP 从站服务，启动 TCP 监听并注册从站 Unit"
}
```

### 3.2 config.schema.json 字段设计

三个驱动的 `start_server` 参数不同，config schema 需分别设计。共享字段通过 `unit_ids` 字符串（逗号分隔）和 `data_area_size` 统一 add_unit 配置。

**TCP / RTU over TCP 共享字段：**

| 字段 | type | default | constraints | 说明 |
|------|------|---------|-------------|------|
| `listen_port` | int | 502 / 5020 | min:1, max:65535 | 监听端口 |
| `unit_ids` | string | "1" | — | 从站地址列表，逗号分隔（如 "1,2,3"） |
| `data_area_size` | int | 10000 | min:1, max:65536 | 每个 Unit 的数据区大小 |
| `event_mode` | string | "write" | enum: write,all,read,none | 事件推送模式（M84） |

**RTU 串口从站额外字段：**

| 字段 | type | default | constraints | 说明 |
|------|------|---------|-------------|------|
| `port_name` | string | "COM1" | — | 串口名称（如 COM1、/dev/ttyUSB0） |
| `baud_rate` | int | 9600 | enum: 1200–115200 | 波特率 |
| `data_bits` | int | 8 | enum: 5,6,7,8 | 数据位 |
| `stop_bits` | string | "1" | enum: 1,1.5,2 | 停止位 |
| `parity` | string | "none" | enum: none,even,odd | 校验位 |

设计决策：`unit_ids` 使用逗号分隔字符串而非 JSON 数组，因为 config.schema.json 的 type 系统不支持 array of int，且字符串在 WebUI SchemaEditor 中编辑更直观。

### 3.3 index.js keepalive 编排模式

三个 index.js 共享相同的编排流程，仅 `start_server` 参数构造不同：

```
┌─────────────────────────────────────────┐
│  getConfig() → 读取 Project 配置         │
│  findDriverPath() → 探测驱动可执行文件    │
│  openDriver() → 启动驱动子进程           │
│         ↓                                │
│  drv.start_server({...}) → 启动服务      │
│         ↓                                │
│  for each unitId in unit_ids:            │
│    drv.add_unit({unit_id, data_area_size})│
│         ↓                                │
│  drv.status() → 日志输出当前状态          │
│         ↓                                │
│  【不调用 $close()，进程保持运行】         │
└─────────────────────────────────────────┘
```

关键代码模式（TCP 为例）：
```javascript
import { openDriver } from "stdiolink";
import { getConfig } from "stdiolink";
import { createLogger } from "stdiolink/log";
import { findDriverPath } from "../../shared/lib/driver_utils.js";

const cfg = getConfig();
const listenPort = Number(cfg.listen_port ?? 502);
const unitIds = String(cfg.unit_ids ?? "1");
const dataAreaSize = Number(cfg.data_area_size ?? 10000);
const eventMode = String(cfg.event_mode ?? "write");

const logger = createLogger({ service: "modbustcp_server" });

function parseUnitIds(str) {
    return str.split(",")
        .map(s => Number(s.trim()))
        .filter(n => Number.isInteger(n) && n > 0 && n <= 247);
}

(async () => {
    const driverPath = findDriverPath("stdio.drv.modbustcp_server");
    if (!driverPath) throw new Error("stdio.drv.modbustcp_server not found");

    const drv = await openDriver(driverPath);
    const startResult = await drv.start_server({
        listen_port: listenPort, event_mode: eventMode
    });
    logger.info("server started", startResult);

    const ids = parseUnitIds(unitIds);
    for (const id of ids) {
        await drv.add_unit({ unit_id: id, data_area_size: dataAreaSize });
    }

    const status = await drv.status();
    logger.info("server ready", status);
    // keepalive: do NOT call drv.$close()
})();
```

### 3.4 Project 配置约定与 unit_ids 解析容错

Project JSON 遵循现有 `manual_driver_pipeline.json` 格式：

```json
{
  "name": "Manual Modbus TCP Server",
  "serviceId": "modbustcp_server_service",
  "enabled": false,
  "schedule": { "type": "manual" },
  "config": { "listen_port": 502, "unit_ids": "1", "data_area_size": 10000, "event_mode": "write" }
}
```

`unit_ids` 解析容错：
```javascript
// "1, 2, 3" → [1, 2, 3]
// "1,,2" → [1, 2]（跳过空段）
// "abc" → []（非数字跳过）
const ids = unitIds.split(",").map(s => Number(s.trim()))
    .filter(n => Number.isInteger(n) && n > 0 && n <= 247);
```

范围限制 1–247 与驱动 C++ 端的 `range(1, 247)` 约束一致。三个 Project 默认端口差异化：TCP=502、RTU over TCP=5020、Serial 使用 port_name。所有 `enabled: false` 防止部署后自动启动。

## 4. 实现步骤

### 4.1 modbustcp_server_service — manifest.json

- 新增 `src/demo/server_manager_demo/data_root/services/modbustcp_server_service/manifest.json`：
  ```json
  {
    "manifestVersion": "1",
    "id": "modbustcp_server_service",
    "name": "Modbus TCP Server Service",
    "version": "1.0.0",
    "description": "Modbus TCP 从站服务，启动 TCP 监听并注册从站 Unit"
  }
  ```
- 理由：遵循现有 manifest 格式，id 与目录名一致，ServiceScanner 可自动发现

### 4.2 modbustcp_server_service — config.schema.json

- 新增 `src/demo/server_manager_demo/data_root/services/modbustcp_server_service/config.schema.json`：
  ```json
  {
    "listen_port": {
      "type": "int",
      "default": 502,
      "description": "Modbus TCP 监听端口",
      "constraints": { "min": 1, "max": 65535 }
    },
    "unit_ids": {
      "type": "string",
      "default": "1",
      "description": "从站地址列表，逗号分隔（如 1,2,3），范围 1-247"
    },
    "data_area_size": {
      "type": "int",
      "default": 10000,
      "description": "每个 Unit 的数据区大小（寄存器/线圈数量）",
      "constraints": { "min": 1, "max": 65536 }
    },
    "event_mode": {
      "type": "string",
      "default": "write",
      "description": "事件推送模式：write=仅写事件, all=读写事件, read=仅读事件, none=无事件",
      "enum": ["write", "all", "read", "none"]
    }
  }
  ```
- 理由：字段与驱动 `start_server` + `add_unit` 参数对齐；`unit_ids` 用 string 类型因 schema 不支持 array of int

### 4.3 modbustcp_server_service — index.js

- 新增 `src/demo/server_manager_demo/data_root/services/modbustcp_server_service/index.js`：
  ```javascript
  /**
   * modbustcp_server_service — Modbus TCP 从站服务
   *
   * 启动 Modbus TCP Server 驱动，配置监听端口并注册从站 Unit。
   * keepalive 模式：驱动进程持续运行，不调用 $close()。
   */
  import { openDriver } from "stdiolink";
  import { getConfig } from "stdiolink";
  import { createLogger } from "stdiolink/log";
  import { findDriverPath } from "../../shared/lib/driver_utils.js";

  const cfg = getConfig();
  const listenPort = Number(cfg.listen_port ?? 502);
  const unitIds = String(cfg.unit_ids ?? "1");
  const dataAreaSize = Number(cfg.data_area_size ?? 10000);
  const eventMode = String(cfg.event_mode ?? "write");

  const logger = createLogger({ service: "modbustcp_server" });

  function parseUnitIds(str) {
      return str.split(",")
          .map(s => Number(s.trim()))
          .filter(n => Number.isInteger(n) && n > 0 && n <= 247);
  }

  (async () => {
      logger.info("starting", { listenPort, unitIds, dataAreaSize, eventMode });

      const driverPath = findDriverPath("stdio.drv.modbustcp_server");
      if (!driverPath) throw new Error("stdio.drv.modbustcp_server not found");

      const drv = await openDriver(driverPath);
      logger.info("driver opened");

      const startResult = await drv.start_server({
          listen_port: listenPort,
          event_mode: eventMode
      });
      logger.info("server started", startResult);

      const ids = parseUnitIds(unitIds);
      if (ids.length === 0) {
          logger.warn("no valid unit_ids configured");
      }
      for (const id of ids) {
          const r = await drv.add_unit({ unit_id: id, data_area_size: dataAreaSize });
          logger.info(`unit ${id} added`, r);
      }

      const status = await drv.status();
      logger.info("server ready", status);
  })();
  ```
- 理由：遵循 `driver_pipeline_service/index.js` 的模式（import 顺序、getConfig、createLogger、IIFE async）；keepalive 不调用 `$close()`

### 4.4 modbusrtu_server_service — 三文件

- 新增 `src/demo/server_manager_demo/data_root/services/modbusrtu_server_service/manifest.json`：
  ```json
  {
    "manifestVersion": "1",
    "id": "modbusrtu_server_service",
    "name": "Modbus RTU Server Service",
    "version": "1.0.0",
    "description": "Modbus RTU over TCP 从站服务，启动 TCP 监听以 RTU 帧格式响应主站请求"
  }
  ```

- 新增 `src/demo/server_manager_demo/data_root/services/modbusrtu_server_service/config.schema.json`：
  - 与 TCP 版结构相同，`listen_port` 默认值改为 `5020` 以避免端口冲突：
  ```json
  {
    "listen_port": {
      "type": "int",
      "default": 5020,
      "description": "Modbus RTU over TCP 监听端口",
      "constraints": { "min": 1, "max": 65535 }
    },
    "unit_ids": { "type": "string", "default": "1", "description": "从站地址列表，逗号分隔，范围 1-247" },
    "data_area_size": { "type": "int", "default": 10000, "description": "每个 Unit 的数据区大小", "constraints": { "min": 1, "max": 65536 } },
    "event_mode": { "type": "string", "default": "write", "description": "事件推送模式", "enum": ["write", "all", "read", "none"] }
  }
  ```

- 新增 `src/demo/server_manager_demo/data_root/services/modbusrtu_server_service/index.js`：
  - 与 TCP 版结构相同，差异点：驱动名 `stdio.drv.modbusrtu_server`，默认端口 `5020`，logger service 名 `modbusrtu_server`
  - 理由：三个从站 Service 保持一致的编排模式，降低维护成本

### 4.5 modbusrtu_serial_server_service — 三文件

- 新增 `src/demo/server_manager_demo/data_root/services/modbusrtu_serial_server_service/manifest.json`：
  ```json
  {
    "manifestVersion": "1",
    "id": "modbusrtu_serial_server_service",
    "name": "Modbus RTU Serial Server Service",
    "version": "1.0.0",
    "description": "Modbus RTU 串口从站服务，打开串口以 RTU 帧格式响应主站请求"
  }
  ```

- 新增 `src/demo/server_manager_demo/data_root/services/modbusrtu_serial_server_service/config.schema.json`：
  - 包含串口特有参数（port_name、baud_rate、data_bits、stop_bits、parity）+ 共享参数（unit_ids、data_area_size、event_mode）：
  ```json
  {
    "port_name": { "type": "string", "default": "COM1", "description": "串口名称（如 COM1、/dev/ttyUSB0）" },
    "baud_rate": { "type": "int", "default": 9600, "description": "波特率", "enum": [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200] },
    "data_bits": { "type": "int", "default": 8, "description": "数据位", "enum": [5, 6, 7, 8] },
    "stop_bits": { "type": "string", "default": "1", "description": "停止位", "enum": ["1", "1.5", "2"] },
    "parity": { "type": "string", "default": "none", "description": "校验位", "enum": ["none", "even", "odd"] },
    "unit_ids": { "type": "string", "default": "1", "description": "从站地址列表，逗号分隔，范围 1-247" },
    "data_area_size": { "type": "int", "default": 10000, "description": "每个 Unit 的数据区大小", "constraints": { "min": 1, "max": 65536 } },
    "event_mode": { "type": "string", "default": "write", "description": "事件推送模式", "enum": ["write", "all", "read", "none"] }
  }
  ```

- 新增 `src/demo/server_manager_demo/data_root/services/modbusrtu_serial_server_service/index.js`：
  - 与 TCP 版编排流程相同，`start_server` 参数增加串口配置（port_name、baud_rate、data_bits、stop_bits、parity）
  - 驱动名 `stdio.drv.modbusrtu_serial_server`，logger service 名 `modbusrtu_serial_server`
  - 理由：串口参数直接透传给驱动 `start_server` 命令，与 C++ Handler 的参数名一致

### 4.6 Project 配置文件

- 新增 `src/demo/server_manager_demo/data_root/projects/manual_modbustcp_server.json`：
  ```json
  {
    "name": "Manual Modbus TCP Server",
    "serviceId": "modbustcp_server_service",
    "enabled": false,
    "schedule": { "type": "manual" },
    "config": {
      "listen_port": 502, "unit_ids": "1",
      "data_area_size": 10000, "event_mode": "write"
    }
  }
  ```

- 新增 `src/demo/server_manager_demo/data_root/projects/manual_modbusrtu_server.json`：
  ```json
  {
    "name": "Manual Modbus RTU Server",
    "serviceId": "modbusrtu_server_service",
    "enabled": false,
    "schedule": { "type": "manual" },
    "config": {
      "listen_port": 5020, "unit_ids": "1",
      "data_area_size": 10000, "event_mode": "write"
    }
  }
  ```

- 新增 `src/demo/server_manager_demo/data_root/projects/manual_modbusrtu_serial_server.json`：
  ```json
  {
    "name": "Manual Modbus RTU Serial Server",
    "serviceId": "modbusrtu_serial_server_service",
    "enabled": false,
    "schedule": { "type": "manual" },
    "config": {
      "port_name": "COM1", "baud_rate": 9600,
      "data_bits": 8, "stop_bits": "1", "parity": "none",
      "unit_ids": "1", "data_area_size": 10000, "event_mode": "write"
    }
  }
  ```
- 理由：`enabled: false` 防止自动启动占用端口/串口；config 值与 schema default 一致

## 5. 文件变更清单

### 5.1 新增文件
- `src/demo/server_manager_demo/data_root/services/modbustcp_server_service/manifest.json` — TCP 从站 Service 身份声明
- `src/demo/server_manager_demo/data_root/services/modbustcp_server_service/config.schema.json` — TCP 从站配置字段定义
- `src/demo/server_manager_demo/data_root/services/modbustcp_server_service/index.js` — TCP 从站编排脚本
- `src/demo/server_manager_demo/data_root/services/modbusrtu_server_service/manifest.json` — RTU over TCP 从站 Service 身份声明
- `src/demo/server_manager_demo/data_root/services/modbusrtu_server_service/config.schema.json` — RTU over TCP 从站配置字段定义
- `src/demo/server_manager_demo/data_root/services/modbusrtu_server_service/index.js` — RTU over TCP 从站编排脚本
- `src/demo/server_manager_demo/data_root/services/modbusrtu_serial_server_service/manifest.json` — RTU 串口从站 Service 身份声明
- `src/demo/server_manager_demo/data_root/services/modbusrtu_serial_server_service/config.schema.json` — RTU 串口从站配置字段定义
- `src/demo/server_manager_demo/data_root/services/modbusrtu_serial_server_service/index.js` — RTU 串口从站编排脚本
- `src/demo/server_manager_demo/data_root/projects/manual_modbustcp_server.json` — TCP 从站 Project 配置
- `src/demo/server_manager_demo/data_root/projects/manual_modbusrtu_server.json` — RTU over TCP 从站 Project 配置
- `src/demo/server_manager_demo/data_root/projects/manual_modbusrtu_serial_server.json` — RTU 串口从站 Project 配置

### 5.2 修改文件
- 无

### 5.3 测试文件
- 无新增测试文件（验证通过集成测试完成，见 §6）

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: manifest.json / config.schema.json / index.js 的静态正确性
- 用例分层: JSON 格式校验、必填字段完整性、字段类型与约束合规、JS 语法可加载
- 断言要点: JSON.parse 不抛异常；manifest 含 `manifestVersion`/`id`/`name`/`version`/`description`；schema 字段含 `type`/`default`/`description`；index.js 无语法错误
- 桩替身策略: 无需 mock（纯静态文件校验）
- 测试文件: 通过 shell 脚本或手动验证

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| manifest.json 格式 | 三个 manifest 均为合法 JSON 且含全部必填字段 | T01 |
| manifest.json id 一致性 | id 字段与目录名一致 | T02 |
| config.schema.json 格式 | 三个 schema 均为合法 JSON | T03 |
| config.schema.json 字段完整性 | 每个字段含 type + default + description | T04 |
| config.schema.json enum 合法性 | enum 字段值与驱动 C++ 端白名单一致 | T05 |
| index.js 语法 | 三个 index.js 可被 Node.js/QuickJS 解析无语法错误 | T06 |
| Project JSON 格式 | 三个 Project 均为合法 JSON 且含 name/serviceId/enabled/schedule/config | T07 |
| Project serviceId 引用 | serviceId 指向存在的 Service 目录 | T08 |
| Project config 字段匹配 | config 中的字段名与对应 schema 定义一致 | T09 |

#### 用例详情

**T01 — manifest.json 格式校验**
- 前置条件: 三个 Service 目录已创建
- 输入: 对每个 `manifest.json` 执行 `JSON.parse()`
- 预期: 解析成功，返回对象含 `manifestVersion`、`id`、`name`、`version`、`description` 五个字段
- 断言: `typeof obj.manifestVersion === "string"`; `typeof obj.id === "string"`; `typeof obj.name === "string"`

**T02 — manifest.json id 与目录名一致**
- 前置条件: T01 通过
- 输入: 读取每个 manifest 的 `id` 字段，与其所在目录名比较
- 预期: `modbustcp_server_service/manifest.json` 的 id 为 `"modbustcp_server_service"`，其余类推
- 断言: `obj.id === directoryName`

**T03 — config.schema.json 格式校验**
- 前置条件: 三个 Service 目录已创建
- 输入: 对每个 `config.schema.json` 执行 `JSON.parse()`
- 预期: 解析成功，返回对象，每个顶层 key 为字段名
- 断言: `typeof obj === "object"` && `Object.keys(obj).length > 0`

**T04 — config.schema.json 字段完整性**
- 前置条件: T03 通过
- 输入: 遍历每个 schema 的每个字段定义
- 预期: 每个字段含 `type`（string）、`default`（非 undefined）、`description`（string）
- 断言: `typeof field.type === "string"`; `field.default !== undefined`; `typeof field.description === "string"`

**T05 — config.schema.json enum 合法性**
- 前置条件: T04 通过
- 输入: 检查含 `enum` 的字段
- 预期: `event_mode` 的 enum 为 `["write", "all", "read", "none"]`；串口 `parity` 为 `["none", "even", "odd"]`；`stop_bits` 为 `["1", "1.5", "2"]`
- 断言: `JSON.stringify(field.enum) === JSON.stringify(expected)`

**T06 — index.js 语法校验**
- 前置条件: 三个 Service 目录已创建
- 输入: 对每个 `index.js` 使用 `node --check` 或 QuickJS 语法检查
- 预期: 无语法错误，退出码 0
- 断言: 进程退出码为 0，stderr 无 SyntaxError

**T07 — Project JSON 格式校验**
- 前置条件: 三个 Project 文件已创建
- 输入: 对每个 Project JSON 执行 `JSON.parse()`
- 预期: 解析成功，含 `name`（string）、`serviceId`（string）、`enabled`（bool）、`schedule`（object）、`config`（object）
- 断言: `typeof obj.name === "string"`; `typeof obj.serviceId === "string"`; `obj.enabled === false`; `obj.schedule.type === "manual"`

**T08 — Project serviceId 引用有效性**
- 前置条件: T07 通过，Service 目录已创建
- 输入: 检查每个 Project 的 `serviceId` 是否对应 `services/` 下的目录名
- 预期: `modbustcp_server_service`、`modbusrtu_server_service`、`modbusrtu_serial_server_service` 目录均存在
- 断言: `fs.existsSync(path.join(servicesDir, obj.serviceId))`

**T09 — Project config 字段与 schema 一致**
- 前置条件: T07、T03 通过
- 输入: 对每个 Project 的 `config` 字段名集合与对应 schema 的顶层 key 集合比较
- 预期: Project config 的所有字段名均在 schema 中定义
- 断言: `configKeys.every(k => schemaKeys.includes(k))`

#### 测试代码

```bash
#!/bin/bash
# validate_modbus_services.sh — M85 静态校验脚本
set -e
DATA_ROOT="src/demo/server_manager_demo/data_root"

SERVICES=("modbustcp_server_service" "modbusrtu_server_service" "modbusrtu_serial_server_service")
PROJECTS=("manual_modbustcp_server" "manual_modbusrtu_server" "manual_modbusrtu_serial_server")

# T01/T03/T07: JSON 格式校验
for svc in "${SERVICES[@]}"; do
    python3 -c "import json; json.load(open('$DATA_ROOT/services/$svc/manifest.json'))"
    python3 -c "import json; json.load(open('$DATA_ROOT/services/$svc/config.schema.json'))"
    echo "OK: $svc JSON valid"
done
for prj in "${PROJECTS[@]}"; do
    python3 -c "import json; json.load(open('$DATA_ROOT/projects/$prj.json'))"
    echo "OK: $prj.json valid"
done

# T02: manifest id 与目录名一致
for svc in "${SERVICES[@]}"; do
    ID=$(python3 -c "import json; print(json.load(open('$DATA_ROOT/services/$svc/manifest.json'))['id'])")
    [ "$ID" = "$svc" ] && echo "OK: $svc id matches" || { echo "FAIL: $svc id=$ID"; exit 1; }
done

# T08: Project serviceId 引用有效
for prj in "${PROJECTS[@]}"; do
    SID=$(python3 -c "import json; print(json.load(open('$DATA_ROOT/projects/$prj.json'))['serviceId'])")
    [ -d "$DATA_ROOT/services/$SID" ] && echo "OK: $prj -> $SID exists" || { echo "FAIL: $SID not found"; exit 1; }
done

echo "All static validations passed."
```

### 6.2 集成测试

#### 6.2.1 Service Scanner 发现验证

- 启动 `stdiolink_server --data-root src/demo/server_manager_demo/data_root`
- `GET /api/services` 返回列表中包含 `modbustcp_server_service`、`modbusrtu_server_service`、`modbusrtu_serial_server_service`
- 每个 Service 的 `name`、`version`、`description` 与 manifest 一致

#### 6.2.2 Project 加载验证

- `GET /api/projects` 返回列表中包含三个新 Project
- 每个 Project 的 `enabled` 为 `false`，`schedule.type` 为 `"manual"`
- `config` 字段值与 Project JSON 一致

#### 6.2.3 TCP Server 端到端启动

- 通过 `POST /api/projects/{id}/run` 手动触发 `manual_modbustcp_server` Project
- Instance 状态变为 `running`（keepalive 驱动持续运行）
- 使用 Modbus TCP 客户端工具连接 `127.0.0.1:502`，执行 FC 0x03 读取 Holding Register
- 验证读取成功（返回默认值 0）
- 通过 `POST /api/instances/{id}/stop` 停止 Instance

### 6.3 验收标准

- [ ] 三个 Service 目录各含 manifest.json、config.schema.json、index.js（T01–T06）
- [ ] 三个 manifest.json 的 id 与目录名一致（T02）
- [ ] config.schema.json 字段类型、默认值、enum 与驱动 C++ 端参数一致（T04、T05）
- [ ] 三个 Project JSON 格式正确，serviceId 引用有效（T07、T08）
- [ ] Project config 字段名与 schema 定义一致（T09）
- [ ] `stdiolink_server` 启动后 `GET /api/services` 列出三个新 Service（§6.2.1）
- [ ] `GET /api/projects` 列出三个新 Project，enabled=false（§6.2.2）
- [ ] 手动触发 TCP Server Project，驱动正常启动并可接受 Modbus 连接（§6.2.3）

## 7. 风险与控制

- 风险: `findDriverPath()` 在非标准部署路径下找不到驱动可执行文件，导致 Service 启动失败
  - 控制: `findDriverPath()` 已覆盖 5 种候选路径（`./`、`./bin/`、`../bin/`、`../../bin/`、`./build/bin/`），覆盖 release 包和开发构建两种布局
  - 控制: index.js 中找不到驱动时抛出明确异常（含驱动名），便于排查
  - 测试覆盖: §6.2.3 端到端启动验证

- 风险: `unit_ids` 字符串解析容错不足，用户输入非法值（如 "abc"、"0"、"300"）导致静默跳过或驱动报错
  - 控制: `parseUnitIds()` 过滤非整数、≤0、>247 的值；空结果时 logger.warn 提示
  - 控制: 驱动 C++ 端 `add_unit` 命令有独立的 range(1,247) 校验，双重保护
  - 测试覆盖: T04（schema 定义）、§6.2.3（端到端）

- 风险: TCP 默认端口 502 需要管理员权限（Linux 下 <1024 为特权端口）
  - 控制: config.schema.json 允许用户自定义端口（min:1, max:65535）；Project 默认值可在部署时修改
  - 控制: RTU over TCP 默认 5020 避免特权端口问题

- 风险: 串口 Service 的 `port_name` 默认值 "COM1" 在 Linux 上无效
  - 控制: 用户必须在 Project config 中修改为实际串口名（如 `/dev/ttyUSB0`）；`enabled: false` 防止默认配置自动启动
  - 测试覆盖: T07（Project enabled=false 验证）

## 8. 里程碑完成定义（DoD）

- [ ] 三个 Service 目录各含 manifest.json、config.schema.json、index.js，格式正确
- [ ] 三个 Project JSON 文件格式正确，serviceId 引用有效，enabled=false
- [ ] manifest.json 的 id 与目录名一致
- [ ] config.schema.json 字段与驱动 C++ 端 start_server / add_unit 参数对齐
- [ ] index.js 遵循 keepalive 编排模式，使用 findDriverPath + openDriver + proxy 语法
- [ ] ServiceScanner 自动发现三个新 Service（GET /api/services 验证）
- [ ] ProjectManager 加载三个新 Project（GET /api/projects 验证）
- [ ] 手动触发 TCP Server Project 可正常启动驱动并接受 Modbus 连接
- [ ] 发布脚本无需修改（publish_release 已自动复制 services/ 和 projects/）