# 里程碑 50：Dashboard 状态与只读详情 API

> **前置条件**: 里程碑 49 已完成（CORS 中间件已就绪）
> **目标**: 实现 Server 状态总览、Instance 详情、Driver 详情三个只读 API，支撑 WebUI Dashboard 和详情页

---

## 1. 目标

- 实现 `GET /api/server/status` — Dashboard 首页系统状态总览
- 实现 `GET /api/instances/{id}` — Instance 详情（扩展字段）
- 实现 `GET /api/drivers/{id}` — Driver 详情（含完整 DriverMeta）
- 三个接口均为只读，低风险，无副作用
- 记录 Server 启动时间戳，用于计算 uptime

---

## 2. 背景与问题

WebUI Dashboard 需要展示系统状态汇总（Service/Project/Instance/Driver 计数、Server uptime、版本信息），但当前缺少对应接口。Instance 和 Driver 的列表 API 只返回摘要信息，详情页需要更丰富的字段（Instance 的工作目录、日志路径、命令行；Driver 的完整 DriverMeta）。

---

## 3. 技术要点

### 3.1 `GET /api/server/status`

响应体：

```json
{
  "status": "ok",
  "version": "0.1.0",
  "uptimeMs": 86400000,
  "startedAt": "2026-02-11T10:00:00Z",
  "host": "127.0.0.1",
  "port": 8080,
  "dataRoot": "/path/to/data_root",
  "serviceProgram": "/path/to/stdiolink_service",
  "counts": {
    "services": 5,
    "projects": {
      "total": 12,
      "valid": 10,
      "invalid": 2,
      "enabled": 8,
      "disabled": 4
    },
    "instances": {
      "total": 6,
      "running": 6
    },
    "drivers": 3
  },
  "system": {
    "platform": "linux",
    "cpuCores": 8,
    "totalMemoryBytes": 17179869184
  }
}
```

实现要点：

- `uptimeMs`：`ServerManager` 在 `initialize()` 时记录 `QDateTime::currentDateTimeUtc()`，请求时计算差值
- `counts`：遍历内存中的 services/projects/instances/drivers 统计
- `system.platform`：`QSysInfo::productType()` + `QSysInfo::currentCpuArchitecture()`
- `system.cpuCores`：`QThread::idealThreadCount()`
- `system.totalMemoryBytes`：使用平台 API 获取（Linux: `sysinfo()`，macOS: `sysctl(HW_MEMSIZE)`，Windows: `GlobalMemoryStatusEx()`）。如跨平台实现复杂度过高，首版可省略此字段，在 M56 ProcessMonitor 中统一处理
- 注意：不含 `serverCpuPercent`/`serverMemoryRssBytes`（自身资源采集归入 M56 ProcessMonitor）
- 注意：不含 `driverlabConnections`（归入 M55 DriverLab WebSocket）

### 3.2 `GET /api/instances/{id}`

在现有 `instanceToJson()` 基础上扩展：

```json
{
  "id": "inst_abc12345",
  "projectId": "silo-a",
  "serviceId": "data-collector",
  "pid": 12345,
  "startedAt": "2026-02-12T10:00:00Z",
  "status": "running",
  "workingDirectory": "/path/to/workspaces/silo-a",
  "logPath": "/path/to/logs/silo-a.log",
  "commandLine": ["stdiolink_service", "/path/to/services/data-collector", "--config-file=/tmp/xxx"]
}
```

扩展字段：

| 字段 | 来源 |
|------|------|
| `workingDirectory` | `QProcess::workingDirectory()` 或 Instance 创建参数 |
| `logPath` | Instance 的日志文件路径 |
| `commandLine` | `QProcess::program()` + `QProcess::arguments()` |

需在 `Instance` 模型中补充这些字段的存储。应在 `InstanceManager::startInstance()` 中立即将 `QProcess::workingDirectory()`、`program()` + `arguments()` 复制到 Instance 结构体，而非运行时动态查询 QProcess（QProcess 存活期结束后这些 API 不可用）。

### 3.3 `GET /api/drivers/{id}`

返回 Driver 完整信息，包含完整 DriverMeta：

```json
{
  "id": "driver_modbustcp",
  "program": "/path/to/driver_modbustcp",
  "metaHash": "abc123...",
  "meta": {
    "schemaVersion": "1.0",
    "info": { ... },
    "config": { ... },
    "commands": [ ... ],
    "types": { ... },
    "errors": [ ... ],
    "examples": [ ... ]
  }
}
```

实现：从 `DriverCatalog` 获取 `DriverConfig`，调用 `cfg.meta->toJson()` 序列化完整 Meta（需先判空）。

---

## 4. 实现方案

### 4.1 ServerManager 扩展

新增启动时间记录和 status 汇总方法：

```cpp
// server_manager.h 新增
struct ServerStatus {
    QString version;
    QDateTime startedAt;
    qint64 uptimeMs;
    QString host;
    int port;
    QString dataRoot;
    QString serviceProgram;

    int serviceCount;
    int projectTotal;
    int projectValid;
    int projectInvalid;
    int projectEnabled;
    int projectDisabled;
    int instanceTotal;
    int instanceRunning;
    int driverCount;

    QString platform;
    int cpuCores;
};

ServerStatus serverStatus() const;

// private:
QDateTime m_startedAt;
```

在 `initialize()` 成功后记录 `m_startedAt = QDateTime::currentDateTimeUtc()`。

### 4.2 Instance 模型扩展

```cpp
// model/instance.h 新增字段
struct Instance {
    // ... existing fields
    QString workingDirectory;
    QString logPath;
    QStringList commandLine;
};
```

在 `InstanceManager::startInstance()` 中填充这些字段。

### 4.3 ApiRouter 新增路由

```cpp
// api_router.h 新增
QHttpServerResponse handleServerStatus(const QHttpServerRequest& req);
QHttpServerResponse handleInstanceDetail(const QString& id,
                                         const QHttpServerRequest& req);
QHttpServerResponse handleDriverDetail(const QString& id,
                                       const QHttpServerRequest& req);
```

路由注册：

```cpp
server.route("/api/server/status", QHttpServerRequest::Method::Get,
             [this](const QHttpServerRequest& req) {
                 return handleServerStatus(req);
             });

server.route("/api/instances/<arg>", QHttpServerRequest::Method::Get,
             [this](const QString& id, const QHttpServerRequest& req) {
                 return handleInstanceDetail(id, req);
             });

server.route("/api/drivers/<arg>", QHttpServerRequest::Method::Get,
             [this](const QString& id, const QHttpServerRequest& req) {
                 return handleDriverDetail(id, req);
             });
```

注意：QHttpServer Router 按规则列表顺序遍历、首次命中即返回（见 `QHttpServerRouter::handleRequest` 实现）。`/api/instances/<arg>/terminate` 和 `/api/instances/<arg>/logs` 路径更长，且 HTTP 方法不同（POST / GET），只要在 `/api/instances/<arg>` GET 之前注册即可避免被吞掉。建议保持现有注册顺序：先注册子路由（terminate、logs），再注册 `GET /api/instances/<arg>`。

### 4.4 DriverMeta JSON 序列化

`DriverCatalog` 中的 `DriverConfig` 持有 `DriverMeta`。需确认 `DriverMeta::toJson()` 存在且返回完整 JSON（含 `info`/`config`/`commands`/`types`/`errors`/`examples`）。如缺失，需在本里程碑中补充。

---

## 5. 文件变更清单

### 5.1 修改文件

- `src/stdiolink_server/server_manager.h` — 新增 `ServerStatus` 结构体、`serverStatus()` 方法、`m_startedAt` 成员
- `src/stdiolink_server/server_manager.cpp` — 实现 `serverStatus()`，在 `initialize()` 中记录启动时间
- `src/stdiolink_server/model/instance.h` — 新增 `workingDirectory`/`logPath`/`commandLine` 字段
- `src/stdiolink_server/manager/instance_manager.cpp` — 在 `startInstance()` 中填充扩展字段
- `src/stdiolink_server/http/api_router.h` — 新增三个 handler 声明
- `src/stdiolink_server/http/api_router.cpp` — 实现三个 handler + 注册路由

### 5.2 测试文件

- 修改 `src/tests/test_api_router.cpp` — 新增三个 API 测试
- 修改 `src/tests/test_server_manager.cpp` — `serverStatus()` 测试

---

## 6. 测试与验收

### 6.1 单元测试场景

**test_api_router.cpp 新增**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | `GET /api/server/status` 返回 200 | 包含 `status`/`version`/`uptimeMs`/`startedAt` |
| 2 | status 中 counts 字段正确 | `services`/`projects`/`instances`/`drivers` 计数与实际一致 |
| 3 | status 中 system 字段 | `platform` 非空，`cpuCores > 0` |
| 4 | `GET /api/instances/{id}` 已存在 Instance | 返回 200 + 扩展字段 `workingDirectory`/`logPath`/`commandLine` |
| 5 | `GET /api/instances/{id}` 不存在 | 返回 404 |
| 6 | `GET /api/drivers/{id}` 已存在 Driver | 返回 200 + 完整 `meta` 对象 |
| 7 | `GET /api/drivers/{id}` 含 `meta.commands` | commands 数组非空，每个 command 含 `name`/`params` |
| 8 | `GET /api/drivers/{id}` 不存在 | 返回 404 |

**test_server_manager.cpp 新增**：

| # | 场景 | 验证点 |
|---|------|--------|
| 9 | `serverStatus()` 返回正确计数 | 初始化后 serviceCount/driverCount 与加载结果一致 |
| 10 | `serverStatus().uptimeMs` 递增 | 两次调用间 uptimeMs 有增长 |
| 11 | `serverStatus().startedAt` 非空 | initialize 后 startedAt 有效 |

### 6.2 验收标准

- 三个新 API 可用且响应字段完整
- Instance 详情包含扩展字段
- Driver 详情包含完整 DriverMeta
- Server status 的 counts 与内存数据一致
- 现有 API 行为无破坏
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：`GET /api/instances/<arg>` 与已有子路由的注册顺序问题
  - 控制：QHttpServer Router 按注册顺序遍历、首次命中返回。必须先注册更长的子路由（`/terminate`、`/logs`），再注册 `GET /api/instances/<arg>`，否则短路由会吞掉子路由请求。增加回归测试确认
- **风险 2**：`DriverMeta::toJson()` 缺失或不完整
  - 控制：检查现有 `DriverMeta` 序列化实现，必要时补充
- **风险 3**：system 信息获取的跨平台差异
  - 控制：首版仅使用 Qt 跨平台 API（`QSysInfo`、`QThread`），不涉及平台相关代码

---

## 8. 里程碑完成定义（DoD）

- `GET /api/server/status` 实现并返回完整状态
- `GET /api/instances/{id}` 实现并包含扩展字段
- `GET /api/drivers/{id}` 实现并包含完整 DriverMeta
- `ServerManager` 记录启动时间并提供状态汇总
- 对应单元测试完成并通过
- 本里程碑文档入库
