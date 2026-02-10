# stdiolink 服务管理器 — 简化需求文档

> 版本: 0.4.0  
> 日期: 2026-02-10  
> 设计原则: **极简、直观、可靠**

---

## 1. 核心概念

### 1.1 概念模型

```
Service (服务模板)
  │
  ├─ manifest.json         ← 服务元信息
  ├─ index.js              ← 服务逻辑代码
  └─ config.schema.json    ← 配置约束规范
      │
      │ 实例化 (基于 schema 创建配置)
      ▼
Project (服务实例配置)
  │
  ├─ serviceId             ← 指向 Service
  ├─ config {...}          ← 符合 Service schema 的配置
  └─ schedule {...}        ← 调度规则
      │
      │ 运行时 (按调度规则启动)
      ▼
Instance (运行时进程)
  └─ stdiolink_service 子进程
```

### 1.2 核心关系

```
Service (1) ──────► Project (N) ──────► Instance (M)
   ↑                    ↓
   │              validates config
   │              based on
   └────── config.schema.json
```

**关键理解**:
- **Service** = 服务模板，定义了"能做什么"和"需要什么配置"
- **Project** = Service 的实例化配置，定义了"用什么参数运行"
- **Instance** = Project 的运行时实例，是实际执行的进程
- **Manager** = 根据 Project 配置来管理 Service 的执行

---

## 2. 术语定义

| 术语 | 定义 | 示例 |
|------|------|------|
| **Driver** | Driver 可执行文件（建议按目录管理，并通过 `driver.meta.json` 描述） | `drivers/modbus/driver_modbustcp` |
| **Service** | 服务模板目录，包含代码和 Schema | `services/data-collector/` |
| **Project** | Service 的实例化配置文件，必须符合 Service 的 Schema | `projects/silo-a.json` |
| **Instance** | Project 的运行时进程实例（内存对象） | `inst_abc123` |
| **Schema** | 配置约束规范，定义 Project 配置的结构和验证规则 | `config.schema.json` |
| **Schedule** | 调度规则，定义 Project 如何运行 | `manual/fixed_rate/daemon` |

---

## 3. 目录结构

```
<data_root>/
├── config.json                # 管理器配置 (可选)
│
├── drivers/                   # Driver 目录（建议复用 DriverScanner + DriverCatalog）
│   ├── modbus/
│   │   ├── driver.meta.json   # ✅ 自动识别 Driver 元信息
│   │   └── driver_modbustcp   # ✅ 可执行文件（Windows 下可为 .exe）
│   ├── broken-driver.failed/  # ❌ 导出 meta 失败后标记，扫描时跳过
│   └── vision/
│       ├── driver.meta.json
│       └── driver_3dvision
│
├── services/                  # Service 模板目录
│   │
│   ├── data-collector/        # Service: 数据采集
│   │   ├── manifest.json      # 服务元信息
│   │   ├── index.js           # 服务逻辑
│   │   └── config.schema.json # 配置 Schema (定义规范)
│   │
│   └── device-monitor/        # Service: 设备监控
│       ├── manifest.json
│       ├── index.js
│       └── config.schema.json
│
├── projects/                  # Project 配置文件 (Service 实例化配置)
│   │
│   ├── silo-a.json            # Project: 料仓A (基于 data-collector)
│   │                          # config 必须符合 data-collector 的 schema
│   │
│   ├── silo-b.json            # Project: 料仓B (基于 data-collector)
│   │                          # 同一 Service，不同配置
│   │
│   └── monitor-1.json         # Project: 监控1 (基于 device-monitor)
│                              # config 必须符合 device-monitor 的 schema
│
├── workspaces/                # 各 Project 的工作目录
│   ├── silo-a/                # Project silo-a 的工作目录
│   │   ├── output/
│   │   └── temp/
│   ├── silo-b/                # Project silo-b 的工作目录
│   └── monitor-1/             # Project monitor-1 的工作目录
│
├── logs/                      # 日志目录（v1: Project 级聚合日志）
│   ├── manager.log            # 管理器日志
│   ├── silo-a.log             # Project silo-a 的运行日志
│   ├── silo-b.log             # Project silo-b 的运行日志
│   └── monitor-1.log          # Project monitor-1 的运行日志
│
└── shared/                    # 全局共享目录
    └── data/
```

---

## 4. 配置文件格式

### 4.1 Service 定义 (services/data-collector/)

#### manifest.json
```json
{
  "manifestVersion": "1",
  "id": "data-collector",
  "name": "数据采集服务",
  "version": "1.0.0",
  "description": "定时采集 Modbus 设备数据并存储"
}
```

#### config.schema.json (定义 Project 配置规范)
```json
{
  "device": {
    "type": "object",
    "description": "设备连接参数",
    "fields": {
      "host": {
        "type": "string",
        "description": "设备 IP 地址",
        "required": true
      },
      "driverProgram": {
        "type": "string",
        "description": "Driver 可执行文件路径（相对 data_root 或绝对路径）",
        "required": true
      },
      "port": {
        "type": "int",
        "description": "端口号",
        "default": 502,
        "constraints": { "min": 1, "max": 65535 }
      },
      "slaveId": {
        "type": "int",
        "description": "从站地址",
        "default": 1,
        "constraints": { "min": 0, "max": 247 }
      }
    }
  },
  "polling": {
    "type": "object",
    "description": "轮询参数",
    "fields": {
      "intervalMs": {
        "type": "int",
        "description": "采集间隔（毫秒）",
        "default": 1000,
        "constraints": { "min": 100 }
      },
      "registers": {
        "type": "array",
        "description": "寄存器地址列表",
        "items": {
          "type": "int",
          "constraints": { "min": 0, "max": 65535 }
        }
      }
    }
  },
  "output": {
    "type": "object",
    "description": "输出设置",
    "fields": {
      "format": {
        "type": "enum",
        "description": "输出格式",
        "constraints": { "enumValues": ["json", "csv"] },
        "default": "json"
      },
      "filePath": {
        "type": "string",
        "description": "输出文件路径（相对于 workspace）",
        "default": "output/data.json"
      }
    }
  }
}
```

#### Schema 格式说明

- `config.schema.json` 的输入格式采用“顶层字段对象”形式（与 `ServiceConfigSchema::fromJsonFile` 对齐）
- `stdiolink_service --dump-config-schema` 输出的是标准化表示（`{"fields":[...]}`），用于展示/调试，不作为源 schema 文件格式回写
- Manager 侧 API 返回 schema 时，建议保留源格式，避免前后格式不一致

#### index.js
```javascript
import { getConfig, openDriver } from 'stdiolink';

const config = getConfig(); // 获取 Project 配置（已验证符合 schema）

// 根据配置打开 Driver
const driver = await openDriver(config.device.driverProgram, [
  '--host=' + config.device.host,
  '--port=' + String(config.device.port),
  '--slave=' + String(config.device.slaveId)
]);

// 按配置的寄存器地址列表采集数据
for (const addr of config.polling.registers) {
  const result = await driver.readRegisters({ start: addr, count: 1 });
  console.log(`Register ${addr}: ${JSON.stringify(result.values)}`);
}

await driver.$close();
```

### 4.2 Project 配置 (projects/*.json)

#### projects/silo-a.json (基于 data-collector Service)
```json
{
  "name": "料仓A数据采集",
  "serviceId": "data-collector",
  "enabled": true,
  
  "schedule": {
    "type": "fixed_rate",
    "intervalMs": 5000,
    "maxConcurrent": 1
  },
  
  "config": {
    "device": {
      "host": "192.168.1.100",
      "driverProgram": "drivers/modbus/driver_modbustcp",
      "port": 502,
      "slaveId": 1
    },
    "polling": {
      "intervalMs": 1000,
      "registers": [0, 1, 2, 3]
    },
    "output": {
      "format": "json",
      "filePath": "output/silo-a.json"
    }
  }
}
```

**说明**:
- `serviceId: "data-collector"` 指向 Service 模板
- `config` 对象必须符合 `data-collector/config.schema.json` 的约束
- Manager 在加载时会验证配置是否符合 Schema
- 验证失败的 Project 会被标记为 invalid，不会启动
- `enabled` 仅控制自动调度（manual 模式下仍可通过 API 手动启动）

#### projects/silo-b.json (同一 Service，不同配置)
```json
{
  "name": "料仓B数据采集",
  "serviceId": "data-collector",
  "enabled": true,
  
  "schedule": {
    "type": "daemon",
    "restartDelayMs": 3000
  },
  
  "config": {
    "device": {
      "host": "192.168.1.101",
      "driverProgram": "drivers/modbus/driver_modbustcp",
      "port": 502,
      "slaveId": 2
    },
    "polling": {
      "intervalMs": 2000,
      "registers": [0, 1]
    },
    "output": {
      "format": "csv",
      "filePath": "output/silo-b.csv"
    }
  }
}
```

---

## 5. Manager 核心职责

### 5.1 启动流程

```
1. 扫描 drivers/ 目录
   → 复用 `stdiolink::DriverScanner::scanDirectory()`（`src/stdiolink/host/driver_catalog.*`）
   → 按 `driver.meta.json` 发现 Driver
   → 跳过目录名以 `.failed` 结尾的 Driver 目录
   
2. 扫描 services/ 目录
   → 加载所有 Service 模板
   → 解析 manifest.json 和 config.schema.json
   
3. 扫描 projects/ 目录
   → 加载所有 Project 配置文件
   
4. 验证 Project 配置
   → 检查 serviceId 对应的 Service 是否存在
   → 验证 config 是否符合 Service 的 schema
   → 标记无效的 Project
   
5. 应用调度规则
   → 对 enabled=true 且 valid 的 Project 启动调度
   → manual: 不启动，等待手动触发
   → fixed_rate: 启动定时器
   → daemon: 立即启动 Instance
   
6. 启动 HTTP API 服务
```

### 5.2 Driver 扫描与 meta 自修复策略

**扫描规则**:
- 仅扫描 `drivers/` 下非 `.failed` 后缀目录
- 若目录内存在 `driver.meta.json`，按现有流程加载
- 若目录内不存在 `driver.meta.json`，尝试执行 Driver 导出：
  - `<driver_program> --export-meta=<driver_dir>/driver.meta.json`

**导出失败处理**:
- 导出失败（进程启动失败、超时、退出码非 0、输出文件缺失/非法）时：
  - 将目录重命名为 `<dir>.failed`
  - 记录失败原因到 manager 日志
- 后续自动扫描和手动扫描默认均跳过 `.failed` 目录

**手动重扫**:
- 管理器支持手动触发 Driver 重扫，用于更新 `driver.meta.json`
- 手动重扫默认行为：
  - 对已有 `driver.meta.json` 的目录尝试重新导出覆盖（刷新元数据）
  - 对缺失 `driver.meta.json` 的目录继续执行导出流程
  - 仍跳过 `.failed` 目录（需要先人工改名恢复）

**实现边界**:
- `ServiceScanner` / `DriverScanner` 是编排层，底层文件校验与解析复用：
  - Service: `ServiceDirectory` + `ServiceManifest` + `ServiceConfigSchema`
  - Driver: `stdiolink::DriverScanner` + `stdiolink::DriverCatalog`

### 5.3 配置验证

**验证流程**:
```
加载 Project 配置时:
  1. 读取 projects/xxx.json
  2. 提取 serviceId
  3. 查找对应的 Service
  4. 加载 Service 的 config.schema.json
  5. 使用 `stdiolink_service::ServiceConfigValidator` 合并默认值并验证 config
  6. 验证通过 → Project 标记为 valid
  7. 验证失败 → Project 标记为 invalid，记录错误原因
```

**验证示例**:
```cpp
// 伪代码
Project project = loadProjectFile("projects/silo-a.json");
Service service = getService(project.serviceId);

QJsonObject mergedConfig;
ValidationResult result = ServiceConfigValidator::mergeAndValidate(
    service.configSchema,
    QJsonObject{},          // fileConfig: Project 场景可为空
    project.config,         // 直接视作 typed config
    UnknownFieldPolicy::Reject,
    mergedConfig
);

if (result.valid) {
    project.status = "valid";
    scheduleProject(project);
} else {
    project.status = "invalid";
    project.error = result.toString();
    qWarning() << "Project" << project.id << "invalid:" << result.toString();
}
```

### 5.4 Instance 生命周期

```
启动 Instance:
  1. 读取 Project 配置
  2. 使用 ServiceConfigValidator 预校验（用于 Manager 侧状态与错误提示）
  3. 解析 `stdiolink_service` 路径（优先级）:
     1) `config.json.serviceProgram`
     2) 与 `stdiolink_server` 同目录下的 `stdiolink_service` 可执行文件
     3) PATH 中的 `stdiolink_service`
  4. 生成临时配置文件（建议 QTemporaryFile，路径由平台决定）
     - 写入内容为 Project 原始 config（非 mergedConfig）
     - 由 `stdiolink_service` 在启动时做最终 merge+validate
  5. 启动子进程:
     <stdiolink_service_path> <service_dir> --config-file=<temp_config>
  6. 重定向日志到 logs/{projectId}.log
  7. 记录 Instance 到内存

监控 Instance:
  1. 监听 QProcess::finished 信号
  2. 记录退出码
  3. 根据调度类型决定是否重启:
     - daemon + exitCode!=0 → 延迟后重启
     - 其他 → 不重启
  4. 从内存移除 Instance 对象
  5. 清理临时配置文件
```

### 5.5 Manager 关闭流程

```
收到 SIGTERM/SIGINT:
  1. 设置 shuttingDown 标记（停止接受新的 start/reload/scan 请求）
  2. 停止所有调度器（fixed_rate 定时器、daemon 自动重启）
  3. 向所有 running Instance 发送 terminate()
  4. 等待实例退出（graceTimeoutMs，默认 5000）
  5. 超时未退出的实例执行 kill()
  6. 清理临时配置文件并退出
```

---

## 6. HTTP API

### 6.1 Service API (只读)

```
# 列出所有 Service
GET /api/services
Response:
{
  "services": [
    {
      "id": "data-collector",
      "name": "数据采集服务",
      "version": "1.0.0",
      "serviceDir": "services/data-collector",
      "hasSchema": true,
      "projectCount": 2
    }
  ]
}

# 获取 Service 详情和 Schema
GET /api/services/{id}
Response:
{
  "id": "data-collector",
  "name": "数据采集服务",
  "version": "1.0.0",
  "serviceDir": "services/data-collector",
  "manifest": { ... },
  "configSchema": { ... },  ← 返回源格式 Schema（与 config.schema.json 一致）
  "projects": ["silo-a", "silo-b"]
}
```

### 6.2 Project API

```
# 列出所有 Project
GET /api/projects
Response:
{
  "projects": [
    {
      "id": "silo-a",
      "name": "料仓A数据采集",
      "serviceId": "data-collector",
      "enabled": true,
      "valid": true,              ← 配置是否符合 Schema
      "schedule": { "type": "fixed_rate", "intervalMs": 5000 },
      "instanceCount": 1,
      "status": "running"
    },
    {
      "id": "silo-c",
      "name": "料仓C数据采集",
      "serviceId": "data-collector",
      "enabled": true,
      "valid": false,             ← 配置验证失败
      "error": "Required field 'device.host' missing",
      "instanceCount": 0,
      "status": "invalid"
    }
  ]
}

# 获取 Project 详情
GET /api/projects/{id}
Response:
{
  "id": "silo-a",
  "name": "料仓A数据采集",
  "serviceId": "data-collector",
  "enabled": true,
  "valid": true,
  "schedule": { ... },
  "config": { ... },              ← Project 配置
  "configSchema": { ... },        ← 对应的 Service Schema
  "instances": [
    {
      "id": "inst_abc123",
      "pid": 12345,
      "startedAt": "2026-02-10T14:30:00Z",
      "status": "running"
    }
  ]
}

# 创建 Project
POST /api/projects
Request:
{
  "id": "silo-c",                 ← Project ID (文件名)
  "name": "料仓C数据采集",
  "serviceId": "data-collector",  ← 必须是已存在的 Service
  "enabled": false,
  "schedule": { "type": "manual" },
  "config": {                     ← 必须符合 Service 的 Schema
    "device": {
      "host": "192.168.1.102",
      "driverProgram": "drivers/modbus/driver_modbustcp",
      "port": 502
    },
    "polling": { "registers": [0, 1, 2] }
  }
}
Response: 
  201 Created (配置验证通过)
  400 Bad Request (配置不符合 Schema)

约束:
- `id` 必须匹配正则 `^[A-Za-z0-9_-]+$`
- 禁止包含 `/`、`\\`、`..`、空白字符
- 实际文件名固定为 `projects/{id}.json`

# 更新 Project
PUT /api/projects/{id}
Request: (同创建)
Response:
  200 OK (验证通过并保存)
  400 Bad Request (配置不符合 Schema)
  409 Conflict (请求体 `id` 存在且与路径 `{id}` 不一致)

# 验证 Project 配置 (不保存)
POST /api/projects/{id}/validate
Request:
{
  "config": { ... }
}
Response:
{
  "valid": true
}
或
{
  "valid": false,
  "error": {
    "field": "device.host",
    "message": "required field 'host' is missing"
  }
}

# 删除 Project
DELETE /api/projects/{id}
Response: 204 No Content

# 启动 Project (手动触发)
POST /api/projects/{id}/start
Response:
{
  "instanceId": "inst_xyz789",
  "pid": 12346
}

并发行为:
- `manual`: 若已有运行实例，返回 `409 Conflict`（避免重复触发）
- `fixed_rate`: 视为“立即触发一次”；若达到 `maxConcurrent`，返回 `409 Conflict`
- `daemon`: 若已运行，返回 `200 OK` 且 `no-op`

# 停止 Project (停止所有实例)
POST /api/projects/{id}/stop
Response: 200 OK

# 重新加载 Project 配置
POST /api/projects/{id}/reload
Response: 200 OK
```

### 6.3 Instance API (只读)

```
# 列出所有运行中的 Instance
GET /api/instances
Response:
{
  "instances": [
    {
      "id": "inst_abc123",
      "projectId": "silo-a",
      "serviceId": "data-collector",
      "pid": 12345,
      "startedAt": "2026-02-10T14:30:00Z",
      "status": "running"
    }
  ]
}

# 按 Project 筛选
GET /api/instances?projectId=silo-a

# 终止 Instance
POST /api/instances/{id}/terminate
Response: 200 OK

# 获取 Instance 日志
GET /api/instances/{id}/logs?lines=100

说明:
- v1 日志为 Project 级聚合日志，`{id}` 会先映射到 `projectId` 再读取 `logs/{projectId}.log`
- 该接口不保证“仅该实例”日志切片（多实例并发时会混写）
```

### 6.4 Driver API

```
# 列出已发现 Driver
GET /api/drivers

# 手动触发 Driver 重扫并刷新 meta
POST /api/drivers/scan
Request:
{
  "refreshMeta": true       // 可选，默认 true
}
Response:
{
  "scanned": 12,
  "updated": 9,
  "newlyFailed": 1,
  "skippedFailed": 2
}
```

### 6.5 安全边界

- v1 默认监听 `127.0.0.1`，未内置鉴权（认证/授权暂不纳入本期）
- 若使用 `--host=0.0.0.0` 或非 loopback 地址，必须在外层反向代理/网关提供鉴权与访问控制
- 破坏性接口（start/stop/delete/terminate/scan）建议仅在受信网络暴露

---

## 7. Schedule 调度策略

### 7.1 Manual (手动)

```json
{
  "schedule": {
    "type": "manual"
  }
}
```

**行为**:
- 不自动启动 Instance
- 仅通过 `POST /api/projects/{id}/start` 手动触发
- 适用场景: 一次性任务、测试、手动控制

### 7.2 Fixed Rate (固定频率)

```json
{
  "schedule": {
    "type": "fixed_rate",
    "intervalMs": 5000,
    "maxConcurrent": 1
  }
}
```

**参数**:
- `intervalMs`: 执行间隔（毫秒）
- `maxConcurrent`: 最大并发 Instance 数（默认 1）

**行为**:
```
每隔 intervalMs:
  if (当前运行的 Instance 数 < maxConcurrent):
    创建新 Instance
  else:
    跳过本次触发
```

**示例**: intervalMs=5000, maxConcurrent=1
```
T=0s:  启动 Instance A
T=5s:  Instance A 仍在运行 → 跳过
T=10s: Instance A 已结束 → 启动 Instance B
T=15s: Instance B 仍在运行 → 跳过
T=20s: Instance B 已结束 → 启动 Instance C
```

**适用场景**: 定时数据采集、周期性检查

### 7.3 Daemon (守护进程)

```json
{
  "schedule": {
    "type": "daemon",
    "restartDelayMs": 3000,
    "maxConsecutiveFailures": 5
  }
}
```

**参数**:
- `restartDelayMs`: 异常退出后重启延迟（毫秒，默认 3000）
- `maxConsecutiveFailures`: 连续异常退出阈值（默认 5）

**行为**:
```
启动时:
  立即启动 Instance A
  
Instance 退出时:
  if (exitCode == 0):
    记录日志，不重启 (正常退出)
    consecutiveFailures = 0
  else:
    consecutiveFailures += 1
    if (consecutiveFailures >= maxConsecutiveFailures):
      标记 Project 为 crash_loop，停止自动重启，等待人工处理
      return
    等待 restartDelayMs
    启动新 Instance B
```

**适用场景**: 持续监控、长连接服务、实时数据流

---

## 8. 配置验证机制

### 8.1 验证时机

1. **Project 加载时**: Manager 启动时扫描 projects/ 目录
2. **Project 创建时**: `POST /api/projects`
3. **Project 更新时**: `PUT /api/projects/{id}`
4. **手动验证时**: `POST /api/projects/{id}/validate`

### 8.2 验证流程

```cpp
// 伪代码示例
ValidationResult validateProject(const Project& project) {
    // 1. 检查 Service 是否存在
    Service service = serviceScanner.getService(project.serviceId);
    if (!service.valid) {
        return ValidationResult::fail("serviceId", "service not found: " + project.serviceId);
    }
    
    // 2. 通过 ServiceConfigValidator 合并默认值并验证
    QJsonObject merged;
    ValidationResult result = ServiceConfigValidator::mergeAndValidate(
        service.configSchema,
        QJsonObject{},          // fileConfig
        project.config,
        UnknownFieldPolicy::Reject,
        merged
    );
    
    if (!result.valid) {
        return result; // 返回详细错误信息
    }
    
    // 3. 验证 schedule 参数
    result = validateSchedule(project.schedule);
    
    return result;
}
```

### 8.3 错误处理

**验证失败的 Project**:
- 标记为 `valid: false`
- 记录错误原因到 `error` 字段
- 不会被调度执行
- API 返回详细错误信息

**示例错误响应**:
```json
{
  "valid": false,
  "error": {
    "field": "polling.intervalMs",
    "message": "value 50 < min 100"
  }
}
```

---

## 9. 完整示例

### 9.1 场景: 多个料仓数据采集

**目录结构**:
```
data/
├── services/
│   └── data-collector/
│       ├── manifest.json
│       ├── index.js
│       └── config.schema.json
│
├── projects/
│   ├── silo-a.json          ← 料仓A (固定频率)
│   ├── silo-b.json          ← 料仓B (守护进程)
│   └── silo-c.json          ← 料仓C (手动)
│
└── workspaces/
    ├── silo-a/
    ├── silo-b/
    └── silo-c/
```

**projects/silo-a.json** (固定频率采集)
```json
{
  "name": "料仓A数据采集",
  "serviceId": "data-collector",
  "enabled": true,
  "schedule": {
    "type": "fixed_rate",
    "intervalMs": 5000
  },
  "config": {
    "device": { "host": "192.168.1.100", "driverProgram": "drivers/modbus/driver_modbustcp", "port": 502, "slaveId": 1 },
    "polling": { "intervalMs": 1000, "registers": [0, 1, 2, 3] },
    "output": { "format": "json", "filePath": "output/data.json" }
  }
}
```

**projects/silo-b.json** (守护进程持续采集)
```json
{
  "name": "料仓B数据采集",
  "serviceId": "data-collector",
  "enabled": true,
  "schedule": {
    "type": "daemon",
    "restartDelayMs": 3000
  },
  "config": {
    "device": { "host": "192.168.1.101", "driverProgram": "drivers/modbus/driver_modbustcp", "port": 502, "slaveId": 2 },
    "polling": { "intervalMs": 2000, "registers": [0, 1] },
    "output": { "format": "csv", "filePath": "output/data.csv" }
  }
}
```

**projects/silo-c.json** (手动触发)
```json
{
  "name": "料仓C数据采集",
  "serviceId": "data-collector",
  "enabled": false,
  "schedule": {
    "type": "manual"
  },
  "config": {
    "device": { "host": "192.168.1.102", "driverProgram": "drivers/modbus/driver_modbustcp", "port": 502, "slaveId": 3 },
    "polling": { "intervalMs": 1000, "registers": [0] },
    "output": { "format": "json", "filePath": "output/data.json" }
  }
}
```

### 9.2 运行效果

```
启动 Manager:
  ✓ 发现 Service: data-collector
  ✓ 加载 Project: silo-a (valid)
  ✓ 加载 Project: silo-b (valid)
  ✓ 加载 Project: silo-c (valid, disabled)
  ✓ 启动调度: silo-a (fixed_rate, 5s 间隔)
  ✓ 启动调度: silo-b (daemon, 持续运行)
  
运行日志:
  [14:30:00] [silo-a] Instance inst_001 started (PID: 12345)
  [14:30:00] [silo-b] Instance inst_002 started (PID: 12346)
  [14:30:05] [silo-a] Instance inst_001 exited (code: 0)
  [14:30:05] [silo-a] Instance inst_003 started (PID: 12347)
  [14:30:10] [silo-a] Instance inst_003 exited (code: 0)
  [14:30:10] [silo-a] Instance inst_004 started (PID: 12348)
  [14:35:00] [silo-b] Instance inst_002 exited (code: 1, error)
  [14:35:03] [silo-b] Instance inst_005 started (PID: 12349) [restart]
  
手动触发 silo-c:
  curl -X POST http://localhost:8080/api/projects/silo-c/start
  [14:40:00] [silo-c] Instance inst_006 started (PID: 12350)
  [14:40:03] [silo-c] Instance inst_006 exited (code: 0)
```

---

## 10. 命令行参数

```bash
stdiolink_server [选项]

选项:
  --data-root=<path>       数据根目录 (默认: .)
  --port=<port>            HTTP 端口 (默认: 8080)
  --host=<addr>            监听地址 (默认: 127.0.0.1)
  --log-level=<level>      日志级别: debug|info|warn|error (默认: info)
  --help                   显示帮助
  --version                显示版本

示例:
  # 默认配置
  stdiolink_server

  # 指定数据目录
  stdiolink_server --data-root=/var/stdiolink
```

### 10.1 config.json（可选）

`<data_root>/config.json` 用于提供默认配置，建议格式：

```json
{
  "host": "127.0.0.1",
  "port": 8080,
  "logLevel": "info",
  "serviceProgram": ""
}
```

字段说明:
- `host` / `port` / `logLevel`: 与命令行同名参数含义一致
- `serviceProgram`: `stdiolink_service` 可执行文件绝对路径（可选）

优先级:
- CLI 参数 > `config.json` > 内置默认值

---

## 11. 核心类设计

```cpp
// 主管理器
class ServerManager {
    DriverManagerScanner driverScanner;
    DriverCatalog driverCatalog;
    ServiceScanner serviceScanner;
    ProjectManager projectManager;
    InstanceManager instanceManager;
    ScheduleEngine scheduleEngine;

    void initialize();
    void validateAllProjects();
    void startScheduling();
    void rescanDrivers(bool refreshMeta = true);
};

// HTTP API 路由（基于 Qt6::HttpServer）
// main.cpp 中创建 QHttpServer 实例，由 ApiRouter 注册路由
class ApiRouter : public QObject {
    ServerManager* manager;
    void registerRoutes(QHttpServer& server);
    // 路由处理方法：handleServiceList, handleProjectStart, ...
};
// WebSocket 预留：后续可通过 QWebSocketServer 扩展实时事件推送

// Service 扫描器
// 组合复用 ServiceDirectory/ServiceManifest/ServiceConfigSchema，不重复实现底层校验
class ServiceScanner {
    struct ServiceInfo {
        QString id;
        QString name;
        QString serviceDir;
        stdiolink_service::ServiceConfigSchema configSchema; // 解析的 Schema
        bool valid;
    };
    
    QMap<QString, ServiceInfo> scan(const QString& dir);
    ServiceInfo getService(const QString& id);
};

// Driver 扫描器（Manager 编排层）
// 组合复用 stdiolink::DriverScanner + stdiolink::DriverCatalog，补充 .failed 跳过与导出失败隔离策略
class DriverManagerScanner {
public:
    struct ScanStat {
        int scanned = 0;
        int updated = 0;
        int newlyFailed = 0;
        int skippedFailed = 0;
    };

    ScanStat scan(const QString& dir,
                  bool refreshMeta = true);
};

// Driver 目录快照（Manager 持有）
class DriverCatalog {
    QMap<QString, DriverConfig> drivers;
};

// Project 管理器
class ProjectManager {
    struct Project {
        QString id;
        QString name;
        QString serviceId;
        bool enabled;
        Schedule schedule;
        QJsonObject config;
        bool valid;          // 配置是否验证通过
        QString error;       // 验证错误信息
    };
    
    QMap<QString, Project> loadProjects(const QString& dir);
    ValidationResult validateProject(const Project& project);
    void saveProject(const QString& id, const Project& project);
};

// Instance 管理器
class InstanceManager {
    struct Instance {
        QString id;
        QString projectId;
        QProcess* process;
        QDateTime startedAt;
        int pid;
        QString status;
    };
    
    QString createInstance(const QString& projectId, const QJsonObject& config);
    void terminateInstance(const QString& instanceId);
    QList<Instance*> getInstances(const QString& projectId = "");
};

// 配置验证器 (复用 stdiolink_service::ServiceConfigValidator)
ValidationResult validateProjectConfig(
    const QJsonObject& config,
    const stdiolink_service::ServiceConfigSchema& schema
);
```

---

## 12. 总结

### 核心理念

```
Service (模板) → 定义能力和约束
    ↓
Project (配置) → 实例化 Service，提供参数
    ↓
Instance (进程) → 实际执行
    ↓
Manager → 根据 Project 管理 Service 的执行
```

### 关键特性

- ✅ **基于 Schema 的配置验证**: 确保 Project 配置正确性
- ✅ **一个 Service，多个 Project**: 不同配置复用同一服务逻辑
- ✅ **约定优于配置**: Driver/Service 自动发现
- ✅ **配置即文件**: Project 配置文件即数据库
- ✅ **三种调度模式**: 覆盖手动、定时、常驻场景
- ✅ **内存 Instance 管理**: 运行时状态，轻量高效

### 设计优势

1. **清晰的层次**: Service → Project → Instance 职责分明
2. **强类型约束**: Schema 验证确保配置正确
3. **灵活复用**: 一个 Service 可创建多个不同配置的 Project
4. **简单可靠**: 文件系统存储，无数据库依赖
5. **易于调试**: 配置文件可直接编辑，日志清晰可追溯
