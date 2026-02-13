# 里程碑 49：CORS 中间件

> **前置条件**: 里程碑 48 已完成
> **目标**: 为 QHttpServer 实现 CORS 跨域支持，作为 WebUI 所有 API 调用的前置基础设施

---

## 1. 目标

- 为所有 REST API 响应注入标准 CORS 头（`Access-Control-Allow-Origin` 等）
- 注册 `OPTIONS` 方法通配路由处理浏览器预检请求，返回 204 + CORS 头
- `ServerConfig` 新增 `corsOrigin` 字段，支持配置文件指定允许的源（默认 `"*"`）
- 不引入第三方依赖，基于 QHttpServer 现有机制实现

---

## 2. 背景与问题

WebUI 作为 SPA 与 API 服务器必然跨域（即使同机器也是不同端口）。当前 `http_helpers.h` 和整个 server 代码中没有任何 CORS 处理，浏览器端 `fetch()` 跨域请求会被拒绝。CORS 是所有后续 WebUI API 对接的硬性前置条件。

---

## 3. 技术要点

### 3.1 CORS 响应头

所有 HTTP 响应需附加以下头：

| 头 | 值 |
|----|----|
| `Access-Control-Allow-Origin` | `corsOrigin` 配置值（默认 `"*"`） |
| `Access-Control-Allow-Methods` | `GET, POST, PUT, PATCH, DELETE, OPTIONS` |
| `Access-Control-Allow-Headers` | `Content-Type, Accept, Authorization, Origin` |
| `Access-Control-Max-Age` | `86400` |

### 3.2 OPTIONS 预检处理

浏览器对跨域非简单请求（如 `POST` + `Content-Type: application/json`）会先发 `OPTIONS` 预检请求。需注册通配路由：

```
OPTIONS /api/* → 204 No Content + CORS 头
```

QHttpServer 不支持真正的通配路由（`/api/*`），需按路径段数分别注册 `<arg>` 占位符。M49–M57 最深 API 路径为 5 段（如 `/api/services/{id}/files/content`），因此注册 1-5 段即可覆盖。此外，已有的 `setMissingHandler()` 可作为兜底：对未匹配的 OPTIONS 请求返回 204 + CORS 头，避免遗漏新增路径。

### 3.3 实现方式

QHttpServer 从 Qt 6.8 起支持 after-request 处理器（`QHttpServer::addAfterRequestHandler()`），可在响应发出前统一注入 CORS 头，无需在每个 handler 中手动添加。

> **实现前须验证**：`addAfterRequestHandler` 的回调签名需以 Qt 6.10.0 实际头文件为准。文档示例中使用 `(const QHttpServerRequest&, QHttpServerResponse&)` 签名，如实际 API 不同（如返回 `QHttpServerResponse` 而非引用修改），需相应调整。建议先编写最小 demo 验证。

**方案**：

1. 使用 `QHttpServer::addAfterRequestHandler()` 全局处理器注入 CORS 头到所有响应
2. 注册 `OPTIONS /api/<arg>` 系列路由返回空 204 响应（CORS 头由 after-request 处理器统一注入）
3. `CorsMiddleware` 工具类封装上述逻辑

### 3.4 ServerConfig 扩展

```cpp
struct ServerConfig {
    // ... existing fields
    QString corsOrigin = "*";  // 新增
};
```

配置文件 `server.json` 中可选配置：

```json
{
  "corsOrigin": "http://localhost:3000"
}
```

---

## 4. 实现方案

### 4.1 CorsMiddleware 工具类

```cpp
// src/stdiolink_server/http/cors_middleware.h
#pragma once

#include <QHttpServer>
#include <QString>

namespace stdiolink_server {

class CorsMiddleware {
public:
    explicit CorsMiddleware(const QString& allowedOrigin = "*");

    /// 在 QHttpServer 上注册 CORS 支持：after-request 处理器 + OPTIONS 路由
    void install(QHttpServer& server);

    /// 获取当前允许的 Origin
    QString allowedOrigin() const { return m_allowedOrigin; }

private:
    QString m_allowedOrigin;
};

} // namespace stdiolink_server
```

### 4.2 实现

```cpp
// src/stdiolink_server/http/cors_middleware.cpp
#include "cors_middleware.h"
#include <QHttpHeaders>

namespace stdiolink_server {

CorsMiddleware::CorsMiddleware(const QString& allowedOrigin)
    : m_allowedOrigin(allowedOrigin) {}

void CorsMiddleware::install(QHttpServer& server) {
    // 1. addAfterRequestHandler：为所有响应注入 CORS 头
    server.addAfterRequestHandler(&server,
        [origin = m_allowedOrigin](const QHttpServerRequest&, QHttpServerResponse& resp) {
            QHttpHeaders headers = resp.headers();
            headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin,
                                    origin.toUtf8());
            headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::AccessControlAllowMethods,
                                    "GET, POST, PUT, PATCH, DELETE, OPTIONS");
            headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::AccessControlAllowHeaders,
                                    "Content-Type, Accept, Authorization, Origin");
            headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::AccessControlMaxAge,
                                    "86400");
            resp.setHeaders(std::move(headers));
    });

    // 2. OPTIONS 路由处理预检请求（按当前 API 路径层级覆盖）
    server.route("/api/<arg>", QHttpServerRequest::Method::Options,
                 [](const QString&) {
                     return QHttpServerResponse(
                         QHttpServerResponse::StatusCode::NoContent);
                 });

    // 嵌套路径的 OPTIONS（如 /api/services/{id}/files）
    server.route("/api/<arg>/<arg>", QHttpServerRequest::Method::Options,
                 [](const QString&, const QString&) {
                     return QHttpServerResponse(
                         QHttpServerResponse::StatusCode::NoContent);
                 });

    server.route("/api/<arg>/<arg>/<arg>", QHttpServerRequest::Method::Options,
                 [](const QString&, const QString&, const QString&) {
                     return QHttpServerResponse(
                         QHttpServerResponse::StatusCode::NoContent);
                 });

    server.route("/api/<arg>/<arg>/<arg>/<arg>",
                 QHttpServerRequest::Method::Options,
                 [](const QString&, const QString&, const QString&,
                    const QString&) {
                     return QHttpServerResponse(
                         QHttpServerResponse::StatusCode::NoContent);
                 });

    // 5 段路径（如 /api/services/{id}/files/content 的子路径）
    server.route("/api/<arg>/<arg>/<arg>/<arg>/<arg>",
                 QHttpServerRequest::Method::Options,
                 [](const QString&, const QString&, const QString&,
                    const QString&, const QString&) {
                     return QHttpServerResponse(
                         QHttpServerResponse::StatusCode::NoContent);
                 });
}

} // namespace stdiolink_server
```

### 4.3 集成到 ServerManager

在 `main.cpp` 或 `ApiRouter::registerRoutes()` 中，先于路由注册调用：

```cpp
CorsMiddleware cors(config.corsOrigin);
cors.install(server);
// 然后注册业务路由
router.registerRoutes(server);
```

注意：建议先安装 after-request 处理器，再注册业务路由，便于统一维护。

### 4.4 ServerConfig 配置解析

在 `server_config.cpp` 的 `loadFromFile()` 中增加：

```cpp
config.corsOrigin = obj.value("corsOrigin").toString("*");
```

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/stdiolink_server/http/cors_middleware.h`
- `src/stdiolink_server/http/cors_middleware.cpp`

### 5.2 修改文件

- `src/stdiolink_server/config/server_config.h` — 新增 `corsOrigin` 字段
- `src/stdiolink_server/config/server_config.cpp` — 解析 `corsOrigin` 配置
- `src/stdiolink_server/http/api_router.cpp` — 在 `registerRoutes` 中安装 CORS 中间件（或在 `main.cpp` 中独立安装）
- `src/stdiolink_server/CMakeLists.txt` — 新增源文件

### 5.3 测试文件

- 新增 `src/tests/test_cors_middleware.cpp`
- 修改 `src/tests/test_server_config.cpp` — `corsOrigin` 解析测试

---

## 6. 测试与验收

### 6.1 单元测试场景

**test_cors_middleware.cpp**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | GET 请求响应包含 CORS 头 | `Access-Control-Allow-Origin` 值与配置一致 |
| 2 | POST 请求响应包含 CORS 头 | 所有 CORS 头完整 |
| 3 | OPTIONS 预检返回 204 | 状态码 204 + CORS 头 |
| 4 | OPTIONS 嵌套路径 `/api/services/xxx/files` | 正确匹配多段路径 |
| 5 | 自定义 corsOrigin 生效 | `Access-Control-Allow-Origin` 返回配置的 origin |
| 6 | 默认 corsOrigin 为 `*` | 未配置时默认 `*` |
| 7 | 404 响应也包含 CORS 头 | after-request 处理器对所有状态码生效 |

**test_server_config.cpp** 新增：

| # | 场景 | 验证点 |
|---|------|--------|
| 8 | 配置文件含 corsOrigin | 正确解析为指定值 |
| 9 | 配置文件无 corsOrigin | 默认值为 `"*"` |

### 6.2 验收标准

- CORS 中间件安装后，所有 API 响应包含标准 CORS 头
- 浏览器端跨域 `fetch()` 请求不再报 CORS 错误
- OPTIONS 预检请求返回 204
- `corsOrigin` 可通过配置文件自定义
- 现有 API 行为无破坏（兼容 M48）
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：误用 Qt API 名称/签名（`afterRequest`、`addHeader`）导致编译失败
  - 控制：统一使用 `addAfterRequestHandler()` + `QHttpHeaders`（`replaceOrAppend` + `setHeaders`）
- **风险 2**：OPTIONS 路由覆盖不全或与已有路由冲突
  - 控制：OPTIONS 与 GET/POST/PUT/DELETE 按方法分发互不冲突；已预注册到 5 段深度覆盖 M49–M57 所有 API 路径。新增超过 5 段的 API 路径时需同步补充 OPTIONS 规则
- **风险 4**：M49–M57 累计 25+ 条路由，注册顺序越来越脆弱
  - 控制：在 `api_router.cpp` 中按模块分段注册（services / projects / instances / drivers / events），每段加注释标注顺序依赖关系。静态路由（如 `/runtime`、`/scan`）必须先于同级动态路由（`/<arg>`）注册
- **风险 3**：after-request 处理器对 WebSocket 升级请求的影响
  - 控制：WebSocket 升级走独立握手流程；CORS 处理仅针对常规 HTTP 响应

---

## 8. 里程碑完成定义（DoD）

- `CorsMiddleware` 类实现并集成到 server 启动流程
- `ServerConfig` 支持 `corsOrigin` 配置
- OPTIONS 预检路由注册并返回 204
- 所有 REST API 响应包含 CORS 头
- 对应单元测试完成并通过
- 本里程碑文档入库


---

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


---

# 里程碑 51：Project 列表增强与运维操作 API

> **前置条件**: 里程碑 49 已完成（CORS 中间件已就绪）
> **目标**: 增强 Project 列表查询（过滤 + 分页），新增启停开关、Project 级日志、批量运行态查询

---

## 1. 目标

- 增强 `GET /api/projects` — 支持 `serviceId`/`status`/`enabled` 过滤 + `page`/`pageSize` 分页
- 新增 `PATCH /api/projects/{id}/enabled` — 切换 Project 启用状态
- 新增 `GET /api/projects/{id}/logs` — Project 级日志（不依赖 Instance 存活）
- 新增 `GET /api/projects/runtime` — 批量运行态查询（避免 N+1 请求）
- 响应格式从纯数组变为 `{ projects: [...], total, page, pageSize }` 对象，这是**破坏性变更**。WebUI 是唯一消费方且尚未上线，直接切换新格式
- 路由注册时 `/api/projects/runtime` **必须**在 `/api/projects/<arg>` 之前注册。QHttpServer Router 按注册顺序遍历、首次命中返回，若动态路由先注册，`runtime` 会被当作 `id=runtime` 吞掉

---

## 2. 背景与问题

当前 `GET /api/projects` 返回全量列表，无过滤和分页，Project 数量较多时效率低下。WebUI Dashboard 需要按条件筛选和分页展示。启停开关避免了 start/stop 的重量级操作。Project 级日志支持 Instance 退出后仍可查看历史日志。批量运行态接口避免逐个查询 N+1 问题。

---

## 3. 技术要点

### 3.1 `GET /api/projects` 增强

新增查询参数：

| 参数 | 类型 | 说明 |
|------|------|------|
| `serviceId` | string | 按 Service ID 过滤 |
| `status` | string | 按状态过滤：`running`/`stopped`/`disabled`/`invalid` |
| `enabled` | bool | 按启用状态过滤 |
| `page` | int | 页码（从 1 开始，默认 1） |
| `pageSize` | int | 每页数量（默认 20，最大 100） |

响应增加分页元信息：

```json
{
  "projects": [...],
  "total": 42,
  "page": 1,
  "pageSize": 20
}
```

实现：在内存中过滤 `m_projects`，计算分页偏移，无需数据库查询。

**状态判定逻辑**：

- `running`：`project.valid && project.enabled` 且有运行中 Instance
- `stopped`：`project.valid && project.enabled` 且无运行中 Instance
- `disabled`：`project.valid && !project.enabled`
- `invalid`：`!project.valid`

### 3.2 `PATCH /api/projects/{id}/enabled`

请求体：

```json
{
  "enabled": false
}
```

响应（200 OK）：返回更新后的 Project JSON。

实现要点：

- 仅修改 `enabled` 字段，通过 `ProjectManager::updateProject()` 写入文件
- 如 `enabled: false`，通过 `ScheduleEngine` 停止该 Project 的调度，终止运行中 Instance
- 如 `enabled: true`，恢复调度（如 daemon 模式自动拉起）

### 3.3 `GET /api/projects/{id}/logs?lines=N`

查询参数：

| 参数 | 类型 | 说明 |
|------|------|------|
| `lines` | int | 返回最后 N 行（默认 100，范围 1-5000） |

响应（200 OK）：

```json
{
  "projectId": "silo-a",
  "lines": ["line1", "line2", "..."],
  "logPath": "/path/to/logs/silo-a.log",
  "totalLines": 1500
}
```

实现：直接读取 `logs/{projectId}.log` 文件的最后 N 行。采用从文件尾部反向读取的策略，避免加载整个文件。

日志文件不存在时返回空数组（非 404），因为 Project 可能从未运行过。

### 3.4 `GET /api/projects/runtime`

查询参数：

| 参数 | 类型 | 说明 |
|------|------|------|
| `ids` | string | 逗号分隔的 Project ID 列表（为空则返回所有） |

响应（200 OK）：

```json
{
  "runtimes": [
    {
      "id": "silo-a",
      "enabled": true,
      "valid": true,
      "status": "running",
      "runningInstances": 1,
      "instances": [...],
      "schedule": {
        "type": "daemon",
        "timerActive": false,
        "restartSuppressed": false,
        "consecutiveFailures": 0,
        "shuttingDown": false,
        "autoRestarting": true
      }
    }
  ]
}
```

实现：复用 `handleProjectRuntime()` 中已有的单 Project 运行态逻辑，批量遍历。

---

## 4. 实现方案

### 4.1 `handleProjectList` 改造

```cpp
QHttpServerResponse ApiRouter::handleProjectList(const QHttpServerRequest& req) {
    const QUrlQuery query(req.url());

    // 解析过滤参数
    const QString filterServiceId = query.queryItemValue("serviceId");
    const QString filterStatus = query.queryItemValue("status");
    const QString filterEnabled = query.queryItemValue("enabled");

    // 解析分页参数
    int page = qMax(1, query.queryItemValue("page").toInt());
    int pageSize = qBound(1, query.queryItemValue("pageSize").toInt(), 100);
    if (!query.hasQueryItem("page")) page = 1;
    if (!query.hasQueryItem("pageSize")) pageSize = 20;

    // 过滤
    QVector<const Project*> filtered;
    for (const auto& p : m_manager->projects()) {
        if (!filterServiceId.isEmpty() && p.serviceId != filterServiceId) continue;
        if (!filterEnabled.isEmpty()) {
            bool en = (filterEnabled == "true");
            if (p.enabled != en) continue;
        }
        if (!filterStatus.isEmpty()) {
            QString st = computeProjectStatus(p);
            if (st != filterStatus) continue;
        }
        filtered.append(&p);
    }

    // 分页
    int total = filtered.size();
    int offset = (page - 1) * pageSize;
    // ... 构建响应
}
```

### 4.2 日志文件尾部读取

> **注意**：`api_router.cpp` 中已有 `readTailLines()` 实现（用于 `GET /api/instances/{id}/logs`），但当前实现是加载整个文件后取尾部行。本里程碑应将其重构为从文件尾部反向读取的高效实现，并在两处复用。

```cpp
QStringList readLastLines(const QString& filePath, int count) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    // 从文件尾部向前读取块，逐块查找换行符
    const qint64 fileSize = file.size();
    const qint64 chunkSize = 4096;
    QStringList lines;
    QByteArray buffer;
    qint64 pos = fileSize;

    while (pos > 0 && lines.size() < count) {
        qint64 readSize = qMin(chunkSize, pos);
        pos -= readSize;
        file.seek(pos);
        buffer.prepend(file.read(readSize));

        // 从 buffer 中提取完整行
        // ...
    }

    return lines;
}
```

### 4.3 enabled 切换与调度联动

```cpp
QHttpServerResponse ApiRouter::handleProjectEnabled(const QString& id,
                                                     const QHttpServerRequest& req) {
    auto it = m_manager->projects().find(id);
    if (it == m_manager->projects().end())
        return errorResponse(StatusCode::NotFound, "project not found");

    auto body = QJsonDocument::fromJson(req.body()).object();
    if (!body.contains("enabled") || !body["enabled"].isBool())
        return errorResponse(StatusCode::BadRequest, "enabled field required (bool)");

    bool newEnabled = body["enabled"].toBool();
    it->enabled = newEnabled;

    // 持久化
    QString error;
    if (!ProjectManager::saveProject(m_manager->dataRoot() + "/projects", *it, error))
        return errorResponse(StatusCode::InternalServerError, error);

    // 调度联动
    if (!newEnabled) {
        m_manager->scheduleEngine()->stopProject(id);
        m_manager->instanceManager()->terminateByProject(id);
    } else {
        // ScheduleEngine 已有 resumeProject() 方法（见 schedule_engine.h），
        // 仅恢复单个 Project 的调度状态，无需调用重量级的 startScheduling()
        m_manager->scheduleEngine()->resumeProject(id);
    }

    return jsonResponse(projectToJson(*it));
}
```

### 4.4 路由注册（关键）

QHttpServer Router 按注册顺序遍历规则列表，首次命中即返回。`/api/projects/runtime` **必须**在 `/api/projects/<arg>` 之前注册，否则 `runtime` 会被动态占位符当作 `id=runtime` 吞掉：

```cpp
server.route("/api/projects/runtime", Method::Get, ...);   // 必须先注册
server.route("/api/projects/<arg>", Method::Get, ...);     // 后注册
```

---

## 5. 文件变更清单

### 5.1 修改文件

- `src/stdiolink_server/http/api_router.h` — 新增 `handleProjectEnabled`/`handleProjectLogs`/`handleProjectRuntimeBatch` handler 声明
- `src/stdiolink_server/http/api_router.cpp` — 实现四个 handler + 路由注册 + `handleProjectList` 改造
- `src/stdiolink_server/manager/schedule_engine.h` — 复用现有 `stopProject()`/`resumeProject()`；无需新增 `startProject()`
- `src/stdiolink_server/manager/schedule_engine.cpp` — 如需补充细节逻辑再扩展

### 5.2 测试文件

- 修改 `src/tests/test_api_router.cpp` — 新增四个 API 测试组

---

## 6. 测试与验收

### 6.1 单元测试场景

**Project 列表过滤与分页**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 无过滤参数 | 返回所有 Project + 分页元信息 |
| 2 | `serviceId` 过滤 | 仅返回匹配的 Project |
| 3 | `enabled=true` 过滤 | 仅返回启用的 Project |
| 4 | `enabled=false` 过滤 | 仅返回禁用的 Project |
| 5 | `status=running` 过滤 | 仅返回有运行中 Instance 的 Project |
| 6 | `page=1&pageSize=2` | 返回前 2 条 + `total` 为全量 |
| 7 | `page=999` 越界 | 返回空 `projects` 数组 + 正确 `total` |
| 8 | `pageSize=0` 或负数 | 归一化为 1 |
| 9 | `pageSize=200` 超上限 | 归一化为 100 |
| 10 | 组合过滤 + 分页 | 先过滤再分页 |

**启停开关**：

| # | 场景 | 验证点 |
|---|------|--------|
| 11 | `PATCH enabled=false` | Project 标记为 disabled，调度停止 |
| 12 | `PATCH enabled=true` | Project 恢复 enabled，调度恢复 |
| 13 | 请求体缺少 `enabled` | 返回 400 |
| 14 | Project 不存在 | 返回 404 |
| 15 | `enabled` 非 bool 类型 | 返回 400 |

**Project 级日志**：

| # | 场景 | 验证点 |
|---|------|--------|
| 16 | 正常读取日志 | 返回最后 N 行 |
| 17 | `lines=10` | 最多返回 10 行 |
| 18 | 日志文件不存在 | 返回空数组（非 404） |
| 19 | `lines` 超出范围（> 5000） | 归一化为 5000 |
| 20 | `lines=0` 或负数 | 归一化为 100 |
| 21 | Project 不存在 | 返回 404 |

**批量运行态**：

| # | 场景 | 验证点 |
|---|------|--------|
| 22 | 无 `ids` 参数 | 返回所有 Project 运行态 |
| 23 | `ids=p1,p2` | 仅返回 p1 和 p2 的运行态 |
| 24 | `ids` 中包含不存在的 ID | 跳过不存在的，返回存在的 |
| 25 | 运行态含 schedule 信息 | daemon 类型包含 `restartSuppressed`/`consecutiveFailures` |

### 6.2 验收标准

- `GET /api/projects` 支持过滤和分页。响应格式从纯数组变为 `{ projects, total, page, pageSize }` 对象（破坏性变更，WebUI 尚未上线故可直接切换）
- `PATCH /api/projects/{id}/enabled` 切换启停并联动调度
- `GET /api/projects/{id}/logs` 从文件尾部高效读取日志
- `GET /api/projects/runtime` 批量返回运行态
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：`handleProjectList` 响应格式变更
  - 控制：响应从纯数组变为 `{ projects, total, page, pageSize }` 对象，是破坏性变更。WebUI 尚未上线，无外部消费方，直接切换新格式。已有测试需同步更新断言
- **风险 2**：enabled 切换与调度引擎的线程安全
  - 控制：所有操作在 Qt 主事件循环中串行执行，无并发问题
- **风险 3**：大日志文件尾部读取性能
  - 控制：从文件尾部按块反向读取，不加载整个文件；`lines` 上限 5000
- **风险 4**：`/api/projects/runtime` 被 `/api/projects/<arg>` 路由吞掉
  - 控制：QHttpServer Router 按注册顺序首次命中返回，静态路由 `/api/projects/runtime` 必须先于动态路由 `/api/projects/<arg>` 注册。增加回归测试覆盖该路径

---

## 8. 里程碑完成定义（DoD）

- `GET /api/projects` 支持过滤 + 分页
- `PATCH /api/projects/{id}/enabled` 实现并联动调度
- `GET /api/projects/{id}/logs` 实现
- `GET /api/projects/runtime` 实现
- 对应单元测试完成并通过
- 本里程碑文档入库


---

# 里程碑 52：Service 创建与删除

> **前置条件**: 里程碑 49 已完成（CORS 中间件已就绪）
> **目标**: 实现 Service 目录创建 API 和删除 API，支持从模板创建标准三文件结构，为 WebUI Service 管理页面提供后端能力

---

## 1. 目标

- 实现 `POST /api/services` — 创建新 Service 目录及标准三文件结构
- 实现 `DELETE /api/services/{id}` — 删除 Service 目录（含关联 Project 检查）
- `ServiceScanner` 新增 `loadSingle()` public 方法，支持单个 Service 目录加载
- 支持三种模板：`empty`、`basic`、`driver_demo`
- 创建后自动加载到内存，无需手动重扫

---

## 2. 背景与问题

当前 Service 的生命周期完全依赖文件系统扫描（`ServiceScanner` 从 `data_root/services/` 读取子目录），不支持通过 API 创建或删除。WebUI 需要在界面上创建新的 Service（包含 JS 脚本、manifest、schema），也需要删除不再使用的 Service。

---

## 3. 技术要点

### 3.1 `POST /api/services` — 创建 Service

请求体：

```json
{
  "id": "my-service",
  "name": "My Service",
  "version": "1.0.0",
  "description": "A demo service",
  "author": "dev",
  "template": "empty",
  "indexJs": "// optional initial code",
  "configSchema": { "port": { "type": "int", "required": true, "default": 8080 } }
}
```

字段约束：

| 字段 | 类型 | 必填 | 校验规则 |
|------|------|------|----------|
| `id` | string | ✅ | 非空，合法字符 `[a-zA-Z0-9_-]`，长度 1-128 |
| `name` | string | ✅ | 非空 |
| `version` | string | ✅ | 非空 |
| `description` | string | ❌ | — |
| `author` | string | ❌ | — |
| `template` | string | ❌ | `empty`（默认）/ `basic` / `driver_demo` |
| `indexJs` | string | ❌ | 为空则使用模板默认代码 |
| `configSchema` | object | ❌ | 为空则使用模板默认 schema |

**模板内容定义**：

**`empty`**（默认）：

```js
// index.js
import { getConfig } from 'stdiolink';

const config = getConfig();
```

`config.schema.json` → `{}`

**`basic`**：

```js
import { getConfig, openDriver } from 'stdiolink';
import { log } from 'stdiolink/log';

const config = getConfig();
log.info('service started', { config });

// TODO: implement service logic
```

`config.schema.json` → `{ "name": { "type": "string", "required": true, "description": "Service display name" } }`

**`driver_demo`**：

```js
import { getConfig, openDriver } from 'stdiolink';
import { log } from 'stdiolink/log';

const config = getConfig();
const driver = openDriver(config.driverPath);
const task = driver.request('meta.describe');
const meta = task.wait();
log.info('driver meta', meta);
driver.close();
```

`config.schema.json` → `{ "driverPath": { "type": "string", "required": true, "description": "Path to driver executable" } }`

如果用户同时提供了 `indexJs` 和/或 `configSchema`，则忽略模板默认内容，以用户提供的为准。

> **后续优化**：当前模板 JS 代码硬编码在 C++ 源码中，修改需重新编译。后续可将模板文件迁移到 `data_root/templates/` 或 Qt 资源系统（`qrc`）中运行时读取，便于维护和用户自定义。首版硬编码方案可接受，模板内容稳定后再迁移。

响应（201 Created）：

```json
{
  "id": "my-service",
  "name": "My Service",
  "version": "1.0.0",
  "serviceDir": "/path/to/data_root/services/my-service",
  "hasSchema": true,
  "created": true
}
```

错误码：`400`（id 不合法/必填缺失）、`409`（id 已存在）、`500`（文件写入失败）

### 3.2 `DELETE /api/services/{id}` — 删除 Service

查询参数：

| 参数 | 类型 | 说明 |
|------|------|------|
| `force` | bool | 是否强制删除（默认 false） |

```
DELETE /api/services/my-service?force=true
```

> **设计决策**：`force` 使用查询参数而非请求体。部分 HTTP 客户端和代理对 DELETE 请求体的支持不一致（RFC 9110 允许但不鼓励），查询参数更可靠。

前置检查：

1. Service 存在性
2. 检查是否有关联 Project（`project.serviceId == id`）
3. 有关联 Project 且 `force != true` → 返回 409

强制删除时：关联 Project 标记为 `invalid`，设置 error 信息。

响应：204 No Content

错误码：`404`（不存在）、`409`（有关联 Project 且未 force）、`500`（文件删除失败）

### 3.3 `ServiceScanner::loadSingle()`

新增 public 方法：

```cpp
std::optional<ServiceInfo> loadSingle(const QString& serviceDir, QString& error) const;
```

复用已有 `loadService()` 的逻辑，不执行目录扫描，仅加载指定目录的 Service。创建 Service 后优先调用此方法加载到内存，避免全量 `scan()`。`error` 参数用于返回加载失败的具体原因（如 manifest 格式错误），便于 API 层返回有意义的错误信息。

如不希望暴露 `loadSingle()`，可退回到调用 `ServiceScanner::scan()` 全量重扫，保证方案可落地。

---

## 4. 实现方案

### 4.1 Service 创建流程

```
1. 校验请求体（id 格式、必填字段）
2. 检查 id 唯一性（内存 m_services + 文件系统）
3. 创建目录 data_root/services/{id}/
4. 根据 template + 用户覆盖生成内容
5. 写入 manifest.json（manifestVersion 固定 "1"）
6. 写入 index.js
7. 写入 config.schema.json
8. 调用 ServiceScanner::loadSingle() 加载到内存
9. 注册到 ServerManager::m_services
10. 返回 201 响应
```

任何步骤失败时回滚：递归删除已创建的目录（`QDir(serviceDir).removeRecursively()`）。回滚本身失败时记录 warning 日志但不影响错误响应返回。

### 4.2 manifest.json 生成

```json
{
  "manifestVersion": "1",
  "id": "my-service",
  "name": "My Service",
  "version": "1.0.0",
  "description": "A demo service",
  "author": "dev"
}
```

### 4.3 ID 合法性校验

```cpp
bool isValidServiceId(const QString& id) {
    if (id.isEmpty() || id.size() > 128) return false;
    static QRegularExpression re("^[a-zA-Z0-9_-]+$");
    return re.match(id).hasMatch();
}
```

### 4.4 Service 删除流程

```
1. 检查 Service 存在性（内存 m_services + 文件系统目录）
2. 查找关联 Project
3. 非 force 且有关联 → 409（响应体列出关联 Project ID）
4. force 时：标记关联 Project invalid + 设 error 信息
5. 递归删除 data_root/services/{id}/ 目录
6. 从 m_services 移除
7. 返回 204
```

### 4.5 ServerManager 新增方法

```cpp
// server_manager.h 新增
struct ServiceCreateRequest {
    QString id;
    QString name;
    QString version;
    QString description;
    QString author;
    QString templateType;  // "empty" / "basic" / "driver_demo"
    QString indexJs;
    QJsonObject configSchema;
};

struct ServiceCreateResult {
    bool success = false;
    QString error;
    ServiceInfo serviceInfo;
};

ServiceCreateResult createService(const ServiceCreateRequest& request);
bool deleteService(const QString& id, bool force, QString& error);
```

---

## 5. 文件变更清单

### 5.1 修改文件

- `src/stdiolink_server/scanner/service_scanner.h` — 新增 `loadSingle()` public 方法
- `src/stdiolink_server/scanner/service_scanner.cpp` — 实现 `loadSingle()`
- `src/stdiolink_server/server_manager.h` — 新增 `ServiceCreateRequest`/`ServiceCreateResult`、`createService()`/`deleteService()`
- `src/stdiolink_server/server_manager.cpp` — 实现创建/删除逻辑
- `src/stdiolink_server/http/api_router.h` — 新增 `handleServiceCreate`/`handleServiceDelete` 声明
- `src/stdiolink_server/http/api_router.cpp` — 实现 handler + 路由注册
- `src/stdiolink_server/CMakeLists.txt` — 如有新增源文件

### 5.2 测试文件

- 修改 `src/tests/test_api_router.cpp` — 新增 Service CRUD 测试
- 修改 `src/tests/test_server_manager.cpp` — 新增 createService/deleteService 测试
- 修改 `src/tests/test_service_scanner.cpp` — 新增 loadSingle 测试

---

## 6. 测试与验收

### 6.1 单元测试场景

**Service 创建**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 最小请求（id + name + version） | 201 + 目录和三文件已创建 |
| 2 | empty 模板 | index.js 包含 `getConfig` 导入，schema 为空对象 |
| 3 | basic 模板 | index.js 包含 `log.info`，schema 含 `name` 字段 |
| 4 | driver_demo 模板 | index.js 包含 `openDriver`，schema 含 `driverPath` |
| 5 | 用户提供 `indexJs` 覆盖模板 | index.js 内容为用户提供值 |
| 6 | 用户提供 `configSchema` 覆盖模板 | schema 内容为用户提供值 |
| 7 | 创建后 `m_services` 中可查到 | `services()` 包含新 Service |
| 8 | id 含非法字符（空格、中文、`/`） | 400 |
| 9 | id 为空 | 400 |
| 10 | name 缺失 | 400 |
| 11 | version 缺失 | 400 |
| 12 | id 已存在 | 409 |
| 13 | manifest.json 内容校验 | `manifestVersion` 为 `"1"`，字段完整 |
| 14 | 创建失败时目录回滚 | 写入失败后目录不遗留 |

**Service 删除**：

| # | 场景 | 验证点 |
|---|------|--------|
| 15 | 删除无关联 Project 的 Service | 204 + 目录已删 + m_services 已移除 |
| 16 | 删除有关联 Project 的 Service（非 force） | 409 + 目录仍在 |
| 17 | force 删除有关联 Project 的 Service | 204 + 目录已删 + Project.valid = false |
| 18 | 删除不存在的 Service | 404 |
| 19 | force 删除后 Project.error 包含提示 | error 信息说明 Service 已被删除 |

**ServiceScanner::loadSingle**：

| # | 场景 | 验证点 |
|---|------|--------|
| 20 | 加载有效 Service 目录 | 返回 ServiceInfo，字段完整 |
| 21 | 目录缺少 manifest.json | 返回 nullopt 或 error |
| 22 | manifest 格式错误 | 返回 nullopt 或 error |

### 6.2 验收标准

- Service 创建 API 生成正确的三文件结构
- 三种模板内容正确
- 用户自定义内容可覆盖模板
- 创建后立即可通过 `GET /api/services` 查到
- 删除时正确检查关联 Project
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：文件写入失败后回滚不完整
  - 控制：创建流程使用 try-catch + cleanup pattern，失败时递归删除已创建的目录
- **风险 2**：并发创建相同 ID
  - 控制：Qt 主事件循环串行处理请求，无并发问题；目录创建时 `QDir::mkpath()` + 存在性检查已防护
- **风险 3**：`loadSingle()` 与 `loadService()` 逻辑不一致
  - 控制：`loadSingle()` 内部直接调用 `loadService()`，共享同一实现
- **风险 4**：`ServerManager` 职责持续膨胀（M50 新增 `m_startedAt`，M52 新增 `createService`/`deleteService`，后续 M53–M57 继续新增成员）
  - 控制：当前阶段 `ServerManager` 作为编排层持有各子系统引用是合理的。如后续成员超过 10 个，可考虑将 API 相关子系统（CORS、WebSocket handler、EventBus）封装到 `ApiServer` 类，`ServerManager` 只持有 `ApiServer*`

---

## 8. 里程碑完成定义（DoD）

- `POST /api/services` 实现并支持三种模板
- `DELETE /api/services/{id}` 实现并支持 force 选项
- `ServiceScanner::loadSingle()` 实现
- Service 创建后自动加载到内存
- 对应单元测试完成并通过
- 本里程碑文档入库


---

# 里程碑 53：Service 文件操作与路径安全

> **前置条件**: 里程碑 52 已完成（Service CRUD 已就绪）
> **目标**: 实现 Service 目录下文件的列表、读取、写入、创建、删除操作，含路径穿越防护和原子写入机制

---

## 1. 目标

- 新增 `ServiceFileHandler` 工具类，封装路径安全校验与文件操作
- 实现 `GET /api/services/{id}/files` — 文件列表
- 实现 `GET /api/services/{id}/files/content?path=` — 读取文件
- 实现 `PUT /api/services/{id}/files/content?path=` — 更新文件（原子写入）
- 实现 `POST /api/services/{id}/files/content?path=` — 创建新文件
- 实现 `DELETE /api/services/{id}/files/content?path=` — 删除文件
- 路径穿越防护覆盖至少 10 种攻击变体
- 写入 `manifest.json`/`config.schema.json` 时自动校验格式并更新内存

---

## 2. 背景与问题

WebUI 需要代码编辑器（Monaco/CodeMirror）编辑 Service 的 `index.js`、`config.schema.json` 等文件。服务端必须提供文件读写 API，同时确保安全——防止通过相对路径穿越访问 Service 目录外的文件。此外，文件写入需要原子性保障（write-to-temp-then-rename），避免写入中途崩溃导致文件损坏。

---

## 3. 技术要点

### 3.1 路径安全校验（核心安全机制）

`ServiceFileHandler::isPathSafe()` 实现：

```cpp
bool ServiceFileHandler::isPathSafe(const QString& serviceDir,
                                     const QString& relativePath) {
    // 1. 禁止空路径
    if (relativePath.isEmpty()) return false;

    // 2. 禁止绝对路径
    if (QDir::isAbsolutePath(relativePath)) return false;

    // 3. 禁止含 ".." 路径段（按路径分隔符拆分后逐段检查）
    //    注意：不能用 contains("..") 做子串匹配，否则会误拦 "..hidden" 等合法文件名
    const QStringList segments = relativePath.split(QRegularExpression("[/\\\\]"));
    for (const auto& seg : segments) {
        if (seg == "..") return false;
    }

    // 4. 规范化后检查前缀
    const QString basePath = QDir::cleanPath(QDir(serviceDir).absolutePath());
    const QString resolved = QDir::cleanPath(
        QDir(serviceDir).absoluteFilePath(relativePath));
    if (!resolved.startsWith(basePath + "/")) return false;

    // 5. 符号链接检查：逐级检查路径上的每个中间目录和目标文件（如存在）
    //    是否为符号链接，命中则拒绝
    QFileInfo resolvedInfo(resolved);
    if (resolvedInfo.exists() && resolvedInfo.isSymLink()) return false;
    // 检查中间目录
    QDir dir(serviceDir);
    for (const auto& seg : segments) {
        if (seg.isEmpty() || seg == ".") continue;
        QString stepPath = dir.absoluteFilePath(seg);
        QFileInfo stepInfo(stepPath);
        if (stepInfo.exists() && stepInfo.isSymLink()) return false;
        dir.setPath(stepPath);
    }

    return true;
}
```

**设计决策**：

- 使用 `QDir::cleanPath()` + `absoluteFilePath()`，**不使用** `canonicalFilePath()`
- 原因：`canonicalFilePath()` 对不存在的文件返回空字符串，导致新文件创建场景误判
- 对符号链接：除 `cleanPath` 前缀检查外，额外校验路径上的中间目录和目标文件（如存在）不得为符号链接；命中符号链接直接拒绝（400）
- **已知局限（TOCTOU）**：`isPathSafe()` 返回 true 后、实际读写文件前存在竞态窗口，攻击者理论上可在此间隙将合法路径替换为符号链接。当前场景下 stdiolink_server 运行在受控环境中，风险可接受。如需进一步加固，可在写入时使用 `O_NOFOLLOW`（Linux）或等效机制

### 3.2 原子写入（QSaveFile）

Qt 提供 `QSaveFile` 内置原子写入支持（write-to-temp-then-rename），跨平台处理了 Windows 上 rename 不支持覆盖等细节，无需手动实现：

```cpp
bool ServiceFileHandler::atomicWrite(const QString& filePath,
                                      const QByteArray& content,
                                      QString& error) {
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        error = QString("failed to open for writing: %1").arg(file.errorString());
        return false;
    }
    if (file.write(content) != content.size()) {
        file.cancelWriting();
        error = "write incomplete";
        return false;
    }
    if (!file.commit()) {
        error = QString("commit failed: %1").arg(file.errorString());
        return false;
    }
    return true;
}
```

> **设计决策**：使用 `QSaveFile` 而非手动 temp+rename。`QSaveFile` 内部处理了 POSIX `rename(2)` 原子性和 Windows 上的覆盖问题，代码更简洁且经过 Qt 充分测试。

### 3.3 文件列表 `GET /api/services/{id}/files`

响应（200 OK）：

```json
{
  "serviceId": "my-service",
  "serviceDir": "/path/to/services/my-service",
  "files": [
    { "name": "manifest.json", "path": "manifest.json", "size": 234,
      "modifiedAt": "2026-02-12T10:30:00Z", "type": "json" },
    { "name": "index.js", "path": "index.js", "size": 1024,
      "modifiedAt": "2026-02-12T10:30:00Z", "type": "javascript" },
    { "name": "utils.js", "path": "lib/utils.js", "size": 256,
      "modifiedAt": "2026-02-12T10:30:00Z", "type": "javascript" }
  ]
}
```

递归遍历 Service 目录，返回相对路径。文件类型根据扩展名推断：`.json` → `json`、`.js` → `javascript`、其他文本文件 → `text`。

### 3.4 读取文件 `GET /api/services/{id}/files/content?path=`

- `path` 查询参数（URL 编码）
- 路径安全校验
- 文件大小上限 1MB（超出返回 413）
- 仅允许文本文件
- 响应含 `content`/`size`/`modifiedAt`

### 3.5 更新文件 `PUT /api/services/{id}/files/content?path=`

请求体：

```json
{
  "content": "import { getConfig } from 'stdiolink';\n// updated\n"
}
```

**特殊处理**：

- 写入 `manifest.json` 时：先校验 JSON 格式和 manifest 字段合法性，校验通过后原子写入，然后更新内存中 `ServiceInfo.manifest`
- 写入 `config.schema.json` 时：先校验 JSON 格式和 schema 合法性（使用 `ServiceConfigSchema::fromJsonObject()`，该方法在 M54 中新增），校验通过后原子写入，然后更新内存中 `ServiceInfo.configSchema`
- **依赖说明**：`fromJsonObject()` 在 M54 中实现。M53 的 schema 写入校验功能依赖 M54 先完成。如需并行开发，M53 可先使用简单的 JSON 格式校验（`QJsonDocument::fromJson()` 验证合法 JSON），M54 完成后再增强为完整 schema 校验
- 校验失败直接返回 400，不触发文件 I/O

### 3.6 创建文件 `POST /api/services/{id}/files/content?path=`

- 文件已存在 → 409
- 如目标路径包含子目录（如 `lib/helper.js`），自动创建中间目录

### 3.7 删除文件 `DELETE /api/services/{id}/files/content?path=`

- 不允许删除 `manifest.json`、`index.js`、`config.schema.json` 三个核心文件（返回 400）
- 文件不存在 → 404

---

## 4. 实现方案

### 4.1 ServiceFileHandler 类

```cpp
// src/stdiolink_server/http/service_file_handler.h
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace stdiolink_server {

struct FileInfo {
    QString name;       // 文件名
    QString path;       // 相对路径
    qint64 size;
    QString modifiedAt; // ISO 8601
    QString type;       // json / javascript / text / unknown
};

class ServiceFileHandler {
public:
    /// 路径安全校验
    static bool isPathSafe(const QString& serviceDir, const QString& relativePath);

    /// 解析安全路径（校验 + 返回绝对路径）
    static QString resolveSafePath(const QString& serviceDir,
                                   const QString& relativePath,
                                   QString& error);

    /// 原子写入文件
    static bool atomicWrite(const QString& filePath,
                            const QByteArray& content,
                            QString& error);

    /// 递归列出文件
    static QVector<FileInfo> listFiles(const QString& serviceDir);

    /// 推断文件类型
    static QString inferFileType(const QString& fileName);

    /// 核心文件列表（不可删除）
    static const QStringList& coreFiles();

    static constexpr qint64 kMaxFileSize = 1 * 1024 * 1024; // 1MB
};

} // namespace stdiolink_server
```

### 4.2 ApiRouter 新增路由

```cpp
// 文件列表
server.route("/api/services/<arg>/files",
             QHttpServerRequest::Method::Get,
             [this](const QString& id, const QHttpServerRequest& req) {
                 return handleServiceFiles(id, req);
             });

// 文件读取
server.route("/api/services/<arg>/files/content",
             QHttpServerRequest::Method::Get,
             [this](const QString& id, const QHttpServerRequest& req) {
                 return handleServiceFileRead(id, req);
             });

// 文件更新
server.route("/api/services/<arg>/files/content",
             QHttpServerRequest::Method::Put,
             [this](const QString& id, const QHttpServerRequest& req) {
                 return handleServiceFileWrite(id, req);
             });

// 文件创建
server.route("/api/services/<arg>/files/content",
             QHttpServerRequest::Method::Post,
             [this](const QString& id, const QHttpServerRequest& req) {
                 return handleServiceFileCreate(id, req);
             });

// 文件删除
server.route("/api/services/<arg>/files/content",
             QHttpServerRequest::Method::Delete,
             [this](const QString& id, const QHttpServerRequest& req) {
                 return handleServiceFileDelete(id, req);
             });
```

注意：`?path=` 查询参数通过 `QUrlQuery(req.url())` 获取。

### 4.3 manifest 和 schema 写入后更新内存

```cpp
// 在 handleServiceFileWrite 中
if (relativePath == "manifest.json") {
    // 先校验
    auto doc = QJsonDocument::fromJson(content.toUtf8());
    if (!doc.isObject()) return errorResponse(400, "invalid JSON");
    // 校验 manifest 字段...
    // 写入
    if (!ServiceFileHandler::atomicWrite(absPath, content.toUtf8(), error))
        return errorResponse(500, error);

    // 不要直接写 m_manager->services()（当前是 const 访问器）
    // 方案 A（推荐）：新增 ServerManager::reloadService(serviceId, error)
    //   仅重载单个 Service（复用 ServiceScanner::loadSingle()，M52 已新增）
    // 方案 B：调用全量重扫（实现更简单但性能差）
    //
    // 注意：reloadService() 是本里程碑新增的方法，需在 ServerManager 中实现。
    // 内部逻辑：调用 ServiceScanner::loadSingle() 重新解析目录，
    // 然后替换 m_services 中对应条目。
    QString refreshError;
    if (!m_manager->reloadService(serviceId, refreshError))
        return errorResponse(500, refreshError);
}
```

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/stdiolink_server/http/service_file_handler.h`
- `src/stdiolink_server/http/service_file_handler.cpp`

### 5.2 修改文件

- `src/stdiolink_server/http/api_router.h` — 新增五个 handler 声明
- `src/stdiolink_server/http/api_router.cpp` — 实现五个 handler + 路由注册
- `src/stdiolink_server/server_manager.h` — 新增 `reloadService()`（或等价刷新方法）声明
- `src/stdiolink_server/server_manager.cpp` — 实现单 Service 内存刷新逻辑
- `src/stdiolink_server/CMakeLists.txt` — 新增源文件

### 5.3 测试文件

- 新增 `src/tests/test_service_file_handler.cpp` — 路径安全 + 原子写入
- 修改 `src/tests/test_api_router.cpp` — 文件操作 API 测试

---

## 6. 测试与验收

### 6.1 单元测试场景

**路径安全校验（test_service_file_handler.cpp）**：

| # | 输入 relativePath | 预期 | 说明 |
|---|-------------------|------|------|
| 1 | `"index.js"` | ✅ safe | 正常文件 |
| 2 | `"lib/utils.js"` | ✅ safe | 子目录文件 |
| 3 | `""` | ❌ unsafe | 空路径 |
| 4 | `"../etc/passwd"` | ❌ unsafe | 简单穿越 |
| 5 | `"foo/../../etc/passwd"` | ❌ unsafe | 嵌套穿越 |
| 6 | `"foo/./bar/../../../etc/passwd"` | ❌ unsafe | 混合穿越 |
| 7 | `"/etc/passwd"` | ❌ unsafe | 绝对路径 |
| 8 | `"foo/../bar"` | ❌ unsafe | 含 `..` |
| 9 | `"..hidden"` | ✅ safe | 合法文件名（按段检查 `..`，不做子串匹配） |
| 10 | `"foo/bar/../../baz"` | ❌ unsafe | 多层回退 |
| 11 | `"./index.js"` | ✅ safe | 当前目录前缀 |
| 12 | `"a/b/c/d.js"` | ✅ safe | 深层子目录 |
| 13 | `"link_outside/passwd"`（符号链接） | ❌ unsafe | 路径段命中符号链接 |

**原子写入（test_service_file_handler.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 14 | 写入新文件 | 文件内容正确 |
| 15 | 覆盖已有文件 | 旧内容被替换 |
| 16 | 写入后无 .tmp 残留 | 临时文件已清理 |
| 17 | 路径不存在目录 | 返回失败（不自动创建） |

**文件操作 API（test_api_router.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 18 | `GET /files` 列出三个核心文件 | files 数组含 manifest.json / index.js / config.schema.json |
| 19 | `GET /files` 含子目录文件 | path 为相对路径如 `lib/utils.js` |
| 20 | `GET /files/content?path=index.js` | 返回文件内容 |
| 21 | `GET /files/content?path=../etc/passwd` | 返回 400 |
| 22 | `GET /files/content?path=nonexist.js` | 返回 404 |
| 23 | `GET /files/content` 无 path 参数 | 返回 400 |
| 24 | `PUT /files/content?path=index.js` 更新 | 返回 200 + 内容已更新 |
| 25 | `PUT /files/content?path=manifest.json` 合法内容 | 返回 200 + 内存已更新 |
| 26 | `PUT /files/content?path=manifest.json` 非法 JSON | 返回 400 + 文件未变 |
| 27 | `PUT /files/content?path=config.schema.json` 合法 schema | 返回 200 + 内存更新 |
| 28 | `PUT /files/content?path=config.schema.json` 非法类型 | 返回 400 |
| 29 | `PUT` 内容超 1MB | 返回 413 |
| 30 | `POST /files/content?path=lib/helper.js` 创建新文件 | 返回 201 + 自动创建子目录 |
| 31 | `POST /files/content?path=index.js` 文件已存在 | 返回 409 |
| 32 | `DELETE /files/content?path=lib/helper.js` 删除 | 返回 204 |
| 33 | `DELETE /files/content?path=manifest.json` 核心文件 | 返回 400 |
| 34 | `DELETE /files/content?path=index.js` 核心文件 | 返回 400 |
| 35 | `DELETE /files/content?path=nonexist.js` | 返回 404 |
| 36 | Service 不存在时所有文件操作 | 返回 404 |

### 6.2 验收标准

- 路径安全校验拦截所有已知穿越变体（≥ 12 种）
- 原子写入确保文件完整性
- 五个文件操作 API 全部可用
- manifest 和 schema 写入后内存自动更新
- 核心文件不可删除
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：路径穿越防护遗漏
  - 控制：测试覆盖 12+ 种变体；`..` 检测作为第一道防线，`cleanPath` + 前缀检查作为第二道，符号链接检测作为第三道
- **风险 2**：原子写入跨平台差异
  - 控制：使用 Qt 内置 `QSaveFile`，已处理 POSIX/Windows 差异
- **风险 3**：manifest/schema 写入校验与 ServiceScanner 解析逻辑不一致
  - 控制：复用相同的解析函数（`ServiceManifest::fromJson`、`ServiceConfigSchema` 的解析逻辑）

---

## 8. 里程碑完成定义（DoD）

- `ServiceFileHandler` 类实现（路径安全 + 原子写入 + 文件列表）
- 五个文件操作 API 实现并注册路由
- manifest/schema 写入时自动校验并更新内存
- 路径安全测试覆盖 ≥ 12 种穿越变体
- 对应单元测试完成并通过
- 本里程碑文档入库


---

# 里程碑 54：Schema 校验与配置工具 API

> **前置条件**: 里程碑 52 已完成（Service CRUD 已就绪）
> **目标**: 实现 Schema 格式校验、默认配置生成、配置预校验三个工具 API，并增强 `GET /api/services/{id}` 返回 `configSchemaFields`

---

## 1. 目标

- 实现 `POST /api/services/{id}/validate-schema` — 校验 Schema 格式合法性
- 实现 `POST /api/services/{id}/generate-defaults` — 根据 Schema 生成默认配置
- 实现 `POST /api/services/{id}/validate-config` — 创建 Project 前的配置预校验
- 增强 `GET /api/services/{id}` — 新增 `configSchemaFields` 字段（FieldMeta 数组格式）
- 为 `ServiceConfigSchema` 新增 `fromJsonObject()` public 静态方法

---

## 2. 背景与问题

WebUI 需要在 Schema 编辑器中实时校验 schema 格式、在 Project 创建界面自动填充默认值并预校验配置。当前后端缺少这三个工具 API。此外，Service 的 `configSchema` 是 key-value 格式，而 Driver Meta 的 `params` 是数组格式，前端需要统一的 FieldMeta 数组格式来复用表单生成逻辑。

---

## 3. 技术要点

### 3.1 `POST /api/services/{id}/validate-schema`

校验一份 config schema JSON 是否合法（不写入文件）。

> **语义说明**：此 API 校验的是请求体中的 `schema` 对象，而非该 Service 当前已有的 schema。`{id}` 仅用于验证 Service 存在性，确保调用方在有效的 Service 上下文中操作。保留 `{id}` 是为了与 `generate-defaults`、`validate-config` 等同级 API 保持 URL 模式一致。

请求体：

```json
{
  "schema": {
    "port": { "type": "int", "required": true, "default": 8080 },
    "name": { "type": "string", "required": true }
  }
}
```

成功响应（200 OK）：

```json
{
  "valid": true,
  "fields": [
    { "name": "port", "type": "int", "required": true, "defaultValue": 8080, "description": "" },
    { "name": "name", "type": "string", "required": true, "description": "" }
  ]
}
```

失败响应（200 OK）：

```json
{
  "valid": false,
  "error": "unknown field type \"datetime\" for field \"createdAt\""
}
```

实现要点：

- 需为 `ServiceConfigSchema` 新增 `fromJsonObject(const QJsonObject& obj, QString& error)` public 方法
- 复用已有的 `parseObject()` 内部校验逻辑
- 注意：`integer`（→ int）、`number`（→ double）、`boolean`（→ bool）是合法的类型别名
- 不可使用 `fromJsObject()`，它不返回错误信息

### 3.2 `POST /api/services/{id}/generate-defaults`

根据 Service 的 config schema 生成默认配置。

响应（200 OK）：

```json
{
  "serviceId": "my-service",
  "config": {
    "port": 8080,
    "debug": false,
    "ratio": 0.5
  },
  "requiredFields": ["name", "port"],
  "optionalFields": ["ratio", "debug"]
}
```

实现：遍历 `ServiceInfo.configSchema.fields`，有 `defaultValue` 的字段填入默认值。同时分类返回必填字段和可选字段列表。

### 3.3 `POST /api/services/{id}/validate-config`

在创建 Project 之前预校验配置。

请求体：

```json
{
  "config": {
    "port": 8080,
    "name": "test"
  }
}
```

成功响应（200 OK）：

```json
{
  "valid": true
}
```

失败响应（200 OK）：

```json
{
  "valid": false,
  "errors": [
    { "field": "name", "message": "required field missing" }
  ]
}
```

实现要点：

- 调用 `ServiceConfigValidator::validate()` 针对 Service schema 校验
- **当前限制**：`validate()` 采用 fail-fast 策略，遇到第一个错误即返回
- **阶段性方案**：先返回单一错误（`errors` 数组长度为 1），保持向前兼容格式
- 后续可增强为 `validateAll()` 遍历所有字段收集全部错误

### 3.4 `GET /api/services/{id}` 增强

在现有响应中新增 `configSchemaFields` 字段：

```json
{
  "id": "my-service",
  "name": "My Service",
  "configSchema": { "port": { "type": "int", ... } },
  "configSchemaFields": [
    { "name": "port", "type": "int", "required": true, ... }
  ]
}
```

实现：`ServiceInfo` 已持有解析后的 `ServiceConfigSchema`（含 `QVector<FieldMeta> fields`）。在 `handleServiceDetail()` 中新增 `configSchemaFields` 时，建议通过 `FieldMeta::toJson()` 逐个构造数组。

注意：保留现有 `ServiceConfigSchema::toJson()` 的返回类型（`QJsonObject`），避免破坏现有调用方；新增独立方法导出 FieldMeta 数组。

---

## 4. 实现方案

### 4.1 ServiceConfigSchema 扩展

```cpp
// 新增 public 静态方法
class ServiceConfigSchema {
public:
    // ... existing methods

    /// 从 JSON 对象解析 schema，带错误检查
    static ServiceConfigSchema fromJsonObject(const QJsonObject& obj,
                                               QString& error);

    /// 保持现有接口：导出为 {"fields":[...]} 数组包装格式
    /// 与当前 toJson() 实现一致（见 service_config_schema.cpp:172）
    QJsonObject toJson() const;

    /// 新增：直接导出 FieldMeta 数组格式 [{"name":"fieldName","type":...}, ...]
    /// 用于 API 返回 configSchemaFields，与 DriverMeta.params 格式统一
    /// 前端可复用同一套表单生成逻辑
    QJsonArray toFieldMetaArray() const;

    /// 生成默认配置
    QJsonObject generateDefaults() const;

    /// 获取必填字段名列表
    QStringList requiredFieldNames() const;

    /// 获取可选字段名列表
    QStringList optionalFieldNames() const;
};
```

### 4.2 `fromJsonObject()` 实现

`service_config_schema.cpp` 中已有匿名命名空间的 `parseObject(const QJsonObject&, const QString&, QString&)` 函数，实现了完整的带错误检查的递归解析。`fromJsonObject()` 应直接调用它：

```cpp
ServiceConfigSchema ServiceConfigSchema::fromJsonObject(const QJsonObject& obj,
                                                          QString& error) {
    // 直接复用已有的 parseObject()（匿名命名空间内部函数）
    // parseObject 已处理类型校验、约束解析、嵌套结构等
    return parseObject(obj, "", error);
}
```

> **注意**：`parseObject()` 当前在匿名命名空间中，`fromJsonObject()` 作为同文件的成员函数可以直接调用。无需提取或重构 `parseObject()`。

### 4.3 `generateDefaults()` 实现

```cpp
QJsonObject ServiceConfigSchema::generateDefaults() const {
    QJsonObject config;
    for (const auto& field : fields) {
        if (!field.defaultValue.isNull() && !field.defaultValue.isUndefined()) {
            config[field.name] = field.defaultValue;
        }
    }
    return config;
}
```

### 4.4 ApiRouter handler

```cpp
QHttpServerResponse ApiRouter::handleValidateSchema(const QString& id,
                                                     const QHttpServerRequest& req) {
    // 1. 查找 Service
    auto it = m_manager->services().find(id);
    if (it == m_manager->services().end())
        return errorResponse(StatusCode::NotFound, "service not found");

    // 2. 解析请求
    auto body = QJsonDocument::fromJson(req.body()).object();
    if (!body.contains("schema") || !body["schema"].isObject())
        return errorResponse(StatusCode::BadRequest, "schema field required");
    auto schemaObj = body["schema"].toObject();

    // 3. 校验
    QString error;
    auto schema = ServiceConfigSchema::fromJsonObject(schemaObj, error);
    if (!error.isEmpty()) {
        return jsonResponse(QJsonObject{
            {"valid", false},
            {"error", error}
        });
    }

    // 4. 返回解析后的 fields
    return jsonResponse(QJsonObject{
        {"valid", true},
        {"fields", schema.toFieldMetaArray()}
    });
}
```

---

## 5. 文件变更清单

### 5.1 修改文件

- `src/stdiolink_service/config/service_config_schema.h` — 新增 `fromJsonObject()`/`toFieldMetaArray()`/`generateDefaults()`/`requiredFieldNames()`/`optionalFieldNames()`，保留 `toJson()`
- `src/stdiolink_service/config/service_config_schema.cpp` — 实现新增方法
- `src/stdiolink_server/http/api_router.h` — 新增 `handleValidateSchema`/`handleGenerateDefaults`/`handleValidateConfig` handler 声明
- `src/stdiolink_server/http/api_router.cpp` — 实现三个 handler + 路由注册 + `handleServiceDetail` 增加 `configSchemaFields`

### 5.2 测试文件

- 修改 `src/tests/test_service_config_schema.cpp` — 新增 `fromJsonObject`/`toFieldMetaArray`/`generateDefaults` 测试
- 修改 `src/tests/test_api_router.cpp` — 新增三个工具 API 测试

---

## 6. 测试与验收

### 6.1 单元测试场景

**ServiceConfigSchema::fromJsonObject（test_service_config_schema.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 合法 schema（含 int/string/bool/double） | 返回正确 fields，error 为空 |
| 2 | 类型别名 `integer` → `int` | 合法通过 |
| 3 | 类型别名 `number` → `double` | 合法通过 |
| 4 | 类型别名 `boolean` → `bool` | 合法通过 |
| 5 | 未知类型 `"datetime"` | error 非空，包含字段名 |
| 6 | 空 schema `{}` | 合法，fields 为空 |
| 7 | 含 constraints（min/max） | constraints 正确解析 |
| 8 | 含 ui hints（widget/group） | ui 信息正确解析 |
| 9 | 含 enum 类型 + enumValues | 合法通过 |
| 10 | 含 array 类型 + items | 合法通过 |
| 11 | 含 object 类型 + fields | 嵌套结构正确 |

**ServiceConfigSchema::toFieldMetaArray**：

| # | 场景 | 验证点 |
|---|------|--------|
| 12 | 转换为 FieldMeta 数组 | 每个元素含 `name`/`type`/`required` |
| 13 | 与 fromJsonObject 往返一致 | `toFieldMetaArray` 后结构与原始 fields 对应 |

**generateDefaults**：

| # | 场景 | 验证点 |
|---|------|--------|
| 14 | 含 default 的字段 | 填入默认值 |
| 15 | 无 default 的字段 | 不出现在结果中 |
| 16 | 不同类型的默认值（int/string/bool/double） | 类型正确 |

**API 测试（test_api_router.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 17 | `POST validate-schema` 合法 schema | 200 + `valid: true` + `fields` 数组 |
| 18 | `POST validate-schema` 非法类型 | 200 + `valid: false` + `error` |
| 19 | `POST validate-schema` 缺少 `schema` 字段 | 400 |
| 20 | `POST validate-schema` Service 不存在 | 404 |
| 21 | `POST generate-defaults` | 200 + `config` 含默认值 + `requiredFields`/`optionalFields` |
| 22 | `POST generate-defaults` schema 无 default | 200 + `config` 为空对象 |
| 23 | `POST generate-defaults` Service 不存在 | 404 |
| 24 | `POST validate-config` 合法配置 | 200 + `valid: true` |
| 25 | `POST validate-config` 必填字段缺失 | 200 + `valid: false` + `errors` 数组 |
| 26 | `POST validate-config` 值超出范围 | 200 + `valid: false` |
| 27 | `POST validate-config` Service 不存在 | 404 |
| 28 | `GET /api/services/{id}` 含 `configSchemaFields` | 响应中有 FieldMeta 数组 |
| 29 | `configSchemaFields` 与 `configSchema` 字段一致 | 数量和字段名对应 |

### 6.2 验收标准

- validate-schema 正确识别合法/非法 schema，返回解析后的 FieldMeta
- generate-defaults 正确生成默认配置和字段分类
- validate-config 正确校验配置并返回错误列表（阶段性单错误）
- `GET /api/services/{id}` 包含 `configSchemaFields` 数组
- `ServiceConfigSchema::fromJsonObject()` 可供其他模块复用
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：`fromJsonObject()` 与已有 `fromJsonFile()` 解析逻辑不一致
  - 控制：`fromJsonObject()` 内部调用相同的 `parseObject()` / 字段解析逻辑
- **风险 2**：validate-config 的 fail-fast 限制导致用户体验差
  - 控制：阶段性方案先返回单一错误，`errors` 数组格式保持向前兼容；后续增强为 `validateAll()`
- **风险 3**：直接改 `toJson()` 签名引发兼容性破坏
  - 控制：保留现有 `toJson()`，新增 `toFieldMetaArray()` 承载 API 新需求

---

## 8. 里程碑完成定义（DoD）

- `ServiceConfigSchema::fromJsonObject()` 实现并可被 API 调用
- `ServiceConfigSchema::toFieldMetaArray()`/`generateDefaults()` 实现（`toJson()` 保持兼容）
- 三个工具 API 实现并注册路由
- `GET /api/services/{id}` 返回 `configSchemaFields`
- 对应单元测试完成并通过
- 本里程碑文档入库


---

# 里程碑 55：DriverLab WebSocket 测试会话

> **前置条件**: 里程碑 50 已完成（Driver 详情 API 已就绪），里程碑 49 已完成（CORS 已就绪）
> **目标**: 实现 WebSocket 端点 `WS /api/driverlab/{driverId}`，用连接生命周期绑定 Driver 进程生命周期，支持 OneShot 和 KeepAlive 两种模式

---

## 1. 目标

- 实现 `DriverLabWsHandler` — 注册 WebSocket 升级验证器，管理全局连接
- 实现 `DriverLabWsConnection` — 单个 WebSocket 连接的 Driver 进程管理与消息转发
- 核心原则：**WebSocket 连接 = DriverLab 测试会话**，连接断开即结束会话并清理进程
- 支持 `oneshot` 和 `keepalive` 两种运行模式
- 定义完整的上下行消息协议
- 全局连接数限制（默认 10）

---

## 2. 背景与问题

桌面端 DriverLab 直接拉起 Driver 子进程进行测试。Web 版需要服务端代理整个 Driver 进程交互。与 REST Session 方案（需 idle timeout、session 表、轮询）相比，WebSocket 方案利用连接状态作为"用户是否在场"的天然信号，代码更简洁，资源泄漏风险更低。

Qt 6.8+ 的 `QAbstractHttpServer::addWebSocketUpgradeVerifier()` 原生支持 WebSocket 升级，HTTP 和 WebSocket 共享同一端口，无需独立 `QWebSocketServer`。

---

## 3. 技术要点

### 3.1 连接生命周期

```
客户端 WebSocket 握手
  → 服务端 verifyUpgrade（校验 driverId、连接数）
  → 握手成功，创建 DriverLabWsConnection
  → 拉起 Driver 子进程（QProcess）
  → queryMeta → 推送 meta 消息给客户端
  → 双向通信（exec 命令 → stdin，stdout → stdout 消息）
  → WebSocket 断开 → terminate + kill Driver 进程
  → 析构 DriverLabWsConnection
```

### 3.2 生命周期绑定规则

| 事件 | KeepAlive 模式 | OneShot 模式 |
|------|---------------|-------------|
| WebSocket 断开 | kill Driver | kill Driver |
| Driver 正常退出 | 推送 `driver.exited`，关闭 WebSocket | 推送 `driver.exited`，**不关闭 WebSocket**（保留会话） |
| Driver 崩溃 | 推送 `driver.exited`，关闭 WebSocket | 推送 `driver.exited`，**不关闭 WebSocket**（等待下一条命令） |
| 新命令（Driver 已退出） | 不支持（连接已关闭） | 自动重启 Driver + 推送 `driver.restarted`（受退避策略约束） |
| 服务端 shutdown | kill 所有 Driver，关闭所有 WebSocket | 同左 |

### 3.3 下行消息协议（服务端 → 客户端）

所有下行消息为 JSON，含 `type` 字段：

| type | 说明 | 字段 |
|------|------|------|
| `meta` | 连接建立后首条消息 | `driverId`/`pid`/`runMode`/`meta` |
| `stdout` | Driver stdout 转发 | `message`（原样 JSONL 行） |
| `driver.started` | Driver 进程启动 | `pid` |
| `driver.exited` | Driver 进程退出 | `exitCode`/`exitStatus`/`reason` |
| `driver.restarted` | OneShot 自动重启 | `pid`/`reason` |
| `error` | 错误通知 | `message` |

### 3.4 上行消息协议（客户端 → 服务端）

| type | 说明 | 字段 |
|------|------|------|
| `exec` | 执行命令 | `cmd`/`data` |
| `cancel` | 终止当前命令（可选） | — |

`exec` 消息转发逻辑：将 `{"cmd":"read_register","data":{"address":100}}` 写入 Driver stdin。

`cancel` 语义：关闭 Driver 进程的 stdin（`QProcess::closeWriteChannel()`），使 Driver 感知到输入结束并自行退出。不发送 SIGINT/SIGTERM，因为 Driver 协议层没有"取消"语义，关闭 stdin 是最安全的中断方式。KeepAlive 模式下 cancel 会导致 Driver 退出并关闭 WebSocket；OneShot 模式下 Driver 退出后等待下一条 exec 自动重启。

### 3.5 连接参数

```
ws://127.0.0.1:8080/api/driverlab/driver_modbustcp?runMode=keepalive&args=--verbose
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `runMode` | string | ❌ | `oneshot`（默认）或 `keepalive` |
| `args` | string | ❌ | 额外启动参数，逗号分隔 |

### 3.6 错误处理

| 场景 | 行为 |
|------|------|
| driverId 不存在 | 拒绝握手（verifier 返回 deny + 404） |
| 连接数已满 | 拒绝握手（verifier 返回 deny + 429） |
| Driver 启动失败 | 握手成功后推送 `error`，关闭 WebSocket |
| Meta 查询失败 | 推送 `error`（不中断连接） |
| 客户端发送非法 JSON | 推送 `error`（不中断连接） |
| 客户端发送未知 type | 推送 `error`（不中断连接） |

---

## 4. 实现方案

### 4.1 DriverLabWsHandler

```cpp
// src/stdiolink_server/http/driverlab_ws_handler.h
#pragma once

#include <QHttpServer>
#include <QObject>
#include <QVector>

namespace stdiolink {
class DriverCatalog;
}

namespace stdiolink_server {

class DriverLabWsConnection;

class DriverLabWsHandler : public QObject {
    Q_OBJECT
public:
    explicit DriverLabWsHandler(stdiolink::DriverCatalog* catalog,
                                QObject* parent = nullptr);
    ~DriverLabWsHandler();

    void registerVerifier(QHttpServer& server);
    int activeConnectionCount() const;
    static constexpr int kMaxConnections = 10;

private slots:
    void onNewWebSocketConnection();
    void onConnectionClosed(DriverLabWsConnection* conn);

private:
    QHttpServerWebSocketUpgradeResponse
    verifyUpgrade(const QHttpServerRequest& request);

    stdiolink::DriverCatalog* m_catalog;
    QHttpServer* m_server = nullptr;
    QVector<DriverLabWsConnection*> m_connections;

    // onNewWebSocketConnection 中通过 socket->requestUrl() 重新解析参数。
    // 不使用 QQueue 暂存 verifier 中的参数，因为 Qt 未保证 verifier 回调
    // 与 newWebSocketConnection 信号之间的 1:1 顺序对应关系。
    // requestUrl() 解析代价仅为一次轻量级 URL 解析，可靠性远高于队列方案。
    struct ConnectionParams {
        QString driverId;
        QString runMode;
        QStringList extraArgs;
    };
    static ConnectionParams parseConnectionParams(const QUrl& url);
};

} // namespace stdiolink_server
```

### 4.2 DriverLabWsConnection

```cpp
// src/stdiolink_server/http/driverlab_ws_connection.h
#pragma once

#include <QObject>
#include <QProcess>
#include <QWebSocket>
#include <memory>

namespace stdiolink_server {

class DriverLabWsConnection : public QObject {
    Q_OBJECT
public:
    DriverLabWsConnection(std::unique_ptr<QWebSocket> socket,
                          const QString& driverId,
                          const QString& program,
                          const QString& runMode,
                          const QStringList& extraArgs,
                          QObject* parent = nullptr);
    ~DriverLabWsConnection();

    QString driverId() const { return m_driverId; }

signals:
    void closed(DriverLabWsConnection* conn);

private slots:
    void onTextMessageReceived(const QString& message);
    void onSocketDisconnected();
    void onDriverStdoutReady();
    void onDriverFinished(int exitCode, QProcess::ExitStatus status);

private:
    void startDriver();
    void stopDriver();
    void sendJson(const QJsonObject& msg);
    void forwardStdoutLine(const QByteArray& line);
    void handleExecMessage(const QJsonObject& msg);
    void handleCancelMessage();
    void restartDriverForOneShot();

    std::unique_ptr<QWebSocket> m_socket;
    std::unique_ptr<QProcess> m_process;
    QString m_driverId;
    QString m_program;
    QString m_runMode;  // "oneshot" | "keepalive"
    QStringList m_extraArgs;
    QByteArray m_stdoutBuffer;  // readyRead 可能不是完整行
    bool m_metaSent = false;

    // OneShot 崩溃退避：连续 3 次在 2 秒内退出则暂停自动重启
    static constexpr int kMaxRapidCrashes = 3;
    static constexpr int kRapidCrashWindowMs = 2000;
    int m_consecutiveFastExits = 0;
    QDateTime m_lastDriverStart;
    bool m_restartSuppressed = false;  // true 时不再自动重启，需用户发 restart 命令
};

} // namespace stdiolink_server
```

### 4.3 注册流程

在 `ServerManager::initialize()` 或 `main.cpp` 中：

```cpp
m_driverLabWsHandler = new DriverLabWsHandler(driverCatalog(), this);
m_driverLabWsHandler->registerVerifier(server);
```

### 4.4 verifyUpgrade 实现

```cpp
QHttpServerWebSocketUpgradeResponse
DriverLabWsHandler::verifyUpgrade(const QHttpServerRequest& request) {
    const QString path = request.url().path();
    if (!path.startsWith("/api/driverlab/"))
        return QHttpServerWebSocketUpgradeResponse::passToNext();

    const QString driverId = path.mid(QString("/api/driverlab/").size());
    if (driverId.isEmpty() || !m_catalog->hasDriver(driverId))
        return QHttpServerWebSocketUpgradeResponse::deny(404, "driver not found");

    if (m_connections.size() >= kMaxConnections)
        return QHttpServerWebSocketUpgradeResponse::deny(429, "too many connections");

    // 解析查询参数
    QUrlQuery query(request.url());
    PendingInfo info;
    info.driverId = driverId;
    info.runMode = query.queryItemValue("runMode");
    if (info.runMode.isEmpty()) info.runMode = "oneshot";
    if (info.runMode != "oneshot" && info.runMode != "keepalive")
        return QHttpServerWebSocketUpgradeResponse::deny(400, "invalid runMode");

    QString argsStr = query.queryItemValue("args");
    if (!argsStr.isEmpty())
        info.extraArgs = argsStr.split(",", Qt::SkipEmptyParts);

    // verifier 仅做校验和拒绝，不暂存参数
    // onNewWebSocketConnection 中通过 socket->requestUrl() 重新解析
    return QHttpServerWebSocketUpgradeResponse::accept();
}
```

### 4.5 stdout 转发

Driver stdout 采用 JSONL（每行一个 JSON）。`readyRead` 可能返回不完整的行，需要行缓冲：

```cpp
void DriverLabWsConnection::onDriverStdoutReady() {
    m_stdoutBuffer.append(m_process->readAllStandardOutput());

    while (true) {
        int nlIndex = m_stdoutBuffer.indexOf('\n');
        if (nlIndex < 0) break;

        QByteArray line = m_stdoutBuffer.left(nlIndex).trimmed();
        m_stdoutBuffer.remove(0, nlIndex + 1);

        if (!line.isEmpty())
            forwardStdoutLine(line);
    }
}

void DriverLabWsConnection::forwardStdoutLine(const QByteArray& line) {
    auto doc = QJsonDocument::fromJson(line);
    QJsonObject msg;
    msg["type"] = "stdout";
    msg["message"] = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(QString(line));
    sendJson(msg);
}
```

### 4.6 Meta 查询

连接建立后自动查询 Meta：向 Driver stdin 写入 `{"cmd":"meta.describe","data":{}}\n`，等待 stdout 返回 ok 消息。首条 stdout ok 消息作为 meta 推送给客户端。

实现方式：使用 `m_metaSent` 标志位 + `QTimer::singleShot()` 实现异步超时。`onDriverStdoutReady` 中检查 `!m_metaSent` 时将首条 ok 响应作为 meta 推送，并设置 `m_metaSent = true`。超时回调中检查 `!m_metaSent` 则推送 error 消息。不使用阻塞等待，不阻塞 Qt 事件循环。

如果 Meta 查询超时（5 秒），推送 error 消息但不关闭连接（Driver 可能不支持 meta.describe 但仍可正常工作）。

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/stdiolink_server/http/driverlab_ws_handler.h`
- `src/stdiolink_server/http/driverlab_ws_handler.cpp`
- `src/stdiolink_server/http/driverlab_ws_connection.h`
- `src/stdiolink_server/http/driverlab_ws_connection.cpp`

### 5.2 修改文件

- `src/stdiolink_server/server_manager.h` — 新增 `DriverLabWsHandler*` 成员和 getter
- `src/stdiolink_server/server_manager.cpp` — 初始化和 shutdown 时管理 WebSocket handler
- `src/stdiolink_server/CMakeLists.txt` — 新增源文件，链接 `Qt6::WebSockets`
- `src/stdiolink_server/http/api_router.cpp` — 在 `registerRoutes` 中调用 `registerVerifier`（或在 main.cpp 中单独注册）

### 5.3 测试文件

- 新增 `src/tests/test_driverlab_ws_handler.cpp`

---

## 6. 测试与验收

### 6.1 单元测试场景

**DriverLabWsHandler**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | verifyUpgrade：合法 driverId | 返回 accept |
| 2 | verifyUpgrade：不存在 driverId | 返回 deny + 404 |
| 3 | verifyUpgrade：非 `/api/driverlab/` 路径 | 返回 passToNext |
| 4 | verifyUpgrade：连接数已满 | 返回 deny + 429 |
| 5 | verifyUpgrade：无效 runMode | 返回 deny + 400 |
| 6 | verifyUpgrade：默认 runMode 为 oneshot | PendingInfo.runMode == "oneshot" |
| 7 | activeConnectionCount 正确 | 新建连接后递增，断开后递减 |

**DriverLabWsConnection**：

| # | 场景 | 验证点 |
|---|------|--------|
| 8 | 连接建立后拉起 Driver 进程 | m_process 状态为 Running |
| 9 | Meta 推送作为首条消息 | type == "meta" + 含 driverId/pid/runMode |
| 10 | exec 消息转发到 stdin | Driver stdin 收到 JSON 命令 |
| 11 | Driver stdout 转发 | 客户端收到 type == "stdout" |
| 12 | stdout 行缓冲正确 | 不完整行不会提前发送 |
| 13 | WebSocket 断开 → Driver killed | m_process 已终止 |
| 14 | Driver 退出 → 推送 driver.exited | 客户端收到 exitCode + exitStatus |
| 15 | KeepAlive: Driver 退出 → 关闭 WebSocket | WebSocket 状态为 closed |
| 16 | OneShot: Driver 退出 → **不关闭** WebSocket | WebSocket 仍为 open |
| 17 | OneShot: 退出后发 exec → 自动重启 | 推送 driver.restarted + 新 pid |
| 18 | Driver 启动失败 → 推送 error | type == "error" + 关闭 WebSocket |
| 19 | 客户端发送非法 JSON | 推送 error（不关闭连接） |
| 20 | 客户端发送未知 type | 推送 error（不关闭连接） |
| 21 | 析构时清理 Driver 进程 | ~DriverLabWsConnection 后进程不遗留 |
| 22 | cancel 消息处理 | Driver stdin 关闭或发送中断信号 |

**注意**：WebSocket 测试需要使用 `QWebSocket` 客户端连接到 QHttpServer。测试需启动完整的 HTTP server + 注册 verifier。如果 QHttpServer 的 WebSocket 在单元测试中不易搭建，可使用 mock 验证 Handler/Connection 的逻辑，集成测试验证完整流程。

### 6.2 验收标准

- WebSocket 握手成功后 Driver 进程自动拉起
- Meta 作为首条消息推送
- exec 命令正确转发到 Driver stdin
- stdout 实时转发到客户端
- 连接断开后 Driver 进程被终止
- OneShot 模式下 Driver 退出后自动重启
- 连接数限制生效
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：`QHttpServer::addWebSocketUpgradeVerifier` API 行为与预期不符
  - 控制：Qt 6.10.0 文档已确认此 API；编写最小 demo 先行验证 verifier + `nextPendingWebSocketConnection` 流程
- **风险 2**：verifier 回调中无法传递 driverId 到 `onNewWebSocketConnection`
  - 控制：不使用队列暂存参数。`onNewWebSocketConnection` 中通过 `socket->requestUrl().path()` 和 `QUrlQuery` 重新解析 `driverId`/`runMode`/`args`，彻底消除对 verifier 与信号顺序对应的假设依赖
- **风险 3**：Driver 进程退出信号和 WebSocket 断开信号的竞态
  - 控制：在 `onSocketDisconnected` 中检查进程是否已退出再决定是否 terminate；在 `onDriverFinished` 中检查 socket 是否已断开再决定是否发送消息。OneShot 模式下自动重启时，需加 `m_restarting` 标志位防止 `onDriverFinished` 和 `handleExecMessage` 中的重启逻辑重入
- **风险 4**：OneShot 模式下 Driver 因配置错误秒退导致无限重启循环
  - 控制：连续 3 次在 2 秒内崩溃则推送 `error` 消息通知用户"Driver 频繁崩溃，请检查配置"，暂停自动重启直到用户显式发送 `restart` 命令
- **风险 5**：Meta 查询超时阻塞事件循环
  - 控制：使用异步等待（QTimer + 信号槽），不阻塞 Qt 事件循环

---

## 8. 里程碑完成定义（DoD）

- `DriverLabWsHandler` 和 `DriverLabWsConnection` 实现
- WebSocket 端点注册并可接受连接
- 上下行消息协议完整实现
- OneShot 和 KeepAlive 两种模式行为正确
- 连接数限制生效
- 对应单元测试完成并通过
- 本里程碑文档入库


---

# 里程碑 56：跨平台 ProcessMonitor 与进程树/资源 API

> **前置条件**: 里程碑 50 已完成（Instance 详情 API 已就绪）
> **目标**: 实现以 Linux 为主的进程树采集和资源监控工具类 `ProcessMonitor`，并暴露 `GET /api/instances/{id}/process-tree` 和 `GET /api/instances/{id}/resources` API；Windows/macOS 先提供安全降级实现

---

## 1. 目标

- 实现 `ProcessMonitor` 工具类（Linux 完整实现，Windows/macOS 先提供不崩溃的降级实现）
- 支持获取进程树（以指定 PID 为根递归获取子进程）
- 支持获取进程资源信息（CPU%、RSS、VMS、线程数、运行时长、I/O）
- 实现 `GET /api/instances/{id}/process-tree` — 进程树 API
- 实现 `GET /api/instances/{id}/resources` — 资源快照 API
- CPU 使用率采用两次采样差值计算，首次可返回 0

---

## 2. 背景与问题

WebUI 需要展示 Instance 的完整进程树及每个进程的资源占用。当前 Instance 仅记录顶层 `QProcess` 的 PID 和状态，缺少子进程枚举和资源采集能力。获取进程信息的 API 在 Linux/macOS/Windows 上完全不同，需要封装跨平台实现。

---

## 3. 技术要点

### 3.1 数据模型

```cpp
// src/stdiolink_server/model/process_info.h

struct ProcessInfo {
    qint64 pid = 0;
    qint64 parentPid = 0;
    QString name;
    QString commandLine;
    QString status;          // "running" / "sleeping" / "zombie" / "stopped"
    QDateTime startedAt;

    double cpuPercent = 0.0;
    qint64 memoryRssBytes = 0;
    qint64 memoryVmsBytes = 0;
    int threadCount = 0;
    qint64 uptimeSeconds = 0;
    qint64 ioReadBytes = 0;
    qint64 ioWriteBytes = 0;
};

struct ProcessTreeNode {
    ProcessInfo info;
    QVector<ProcessTreeNode> children;
};

struct ProcessTreeSummary {
    int totalProcesses = 0;
    double totalCpuPercent = 0.0;
    qint64 totalMemoryRssBytes = 0;
    int totalThreads = 0;
};
```

### 3.2 平台实现

**Linux**（`/proc` 文件系统）：

| 数据 | 来源 |
|------|------|
| 进程名 | `/proc/{pid}/comm` |
| 命令行 | `/proc/{pid}/cmdline`（NUL 分隔 → 空格拼接） |
| 状态 | `/proc/{pid}/stat` 第 3 字段（`R`/`S`/`Z`/`T`） |
| CPU 时间 | `/proc/{pid}/stat` 第 14-17 字段（utime + stime） |
| 内存 RSS | `/proc/{pid}/stat` 第 24 字段 × pagesize |
| 内存 VMS | `/proc/{pid}/stat` 第 23 字段 |
| 线程数 | `/proc/{pid}/stat` 第 20 字段 |
| 启动时间 | `/proc/{pid}/stat` 第 22 字段 + boot time |
| I/O | `/proc/{pid}/io`（`read_bytes`/`write_bytes`） |
| 子进程 | 优先使用 `/proc/{pid}/task/{pid}/children`（内核 ≥ 3.5），仅返回直接子进程 PID，无需遍历整个 `/proc`；如该文件不存在则回退到遍历 `/proc/*/stat` 匹配 ppid |

**Windows**：

| 数据 | 来源 |
|------|------|
| 进程枚举 | `CreateToolhelp32Snapshot` + `Process32First/Next` |
| 内存 | `GetProcessMemoryInfo`（`WorkingSetSize` → RSS） |
| CPU 时间 | `GetProcessTimes`（`KernelTime` + `UserTime`） |
| 线程数 | `PROCESSENTRY32.cntThreads` |
| 命令行 | `NtQueryInformationProcess` 或 `QueryFullProcessImageName` |
| I/O | `GetProcessIoCounters` |

**macOS**：

| 数据 | 来源 |
|------|------|
| 子进程 | `proc_listchildpids()` |
| 进程信息 | `proc_pidinfo(PROC_PIDTASKINFO)` |
| 命令行 | `sysctl(CTL_KERN, KERN_PROCARGS2)` |
| 内存 | `proc_taskinfo.pti_resident_size` |
| CPU 时间 | `proc_taskinfo.pti_total_user + pti_total_system` |

### 3.3 CPU 使用率计算

CPU% 需要两次采样间的差值：

```
cpu% = (cpu_time_delta / wall_time_delta) × 100
```

`ProcessMonitor` 维护一个 `QMap<qint64, CpuSample>` 缓存上次采样时间和 CPU 累计时间。首次采样某 PID 时 CPU% 返回 0。

```cpp
struct CpuSample {
    qint64 cpuTimeMs;     // 累计 CPU 时间（毫秒）
    QDateTime timestamp;   // 采样时间
};
```

### 3.4 `GET /api/instances/{id}/process-tree`

响应（200 OK）：

```json
{
  "instanceId": "inst_abc",
  "rootPid": 12345,
  "tree": {
    "pid": 12345,
    "name": "stdiolink_service",
    "commandLine": "...",
    "status": "running",
    "startedAt": "...",
    "resources": {
      "cpuPercent": 2.5,
      "memoryRssBytes": 52428800,
      "memoryVmsBytes": 134217728,
      "threadCount": 8,
      "uptimeSeconds": 3600
    },
    "children": [ ... ]
  },
  "summary": {
    "totalProcesses": 3,
    "totalCpuPercent": 18.5,
    "totalMemoryRssBytes": 337641472,
    "totalThreads": 23
  }
}
```

错误码：`404`（Instance 不存在——包括已退出的 Instance，因为当前实现在进程退出后立即从内存删除）

> **注意**：当前 `InstanceManager::onProcessFinished` 在进程退出后调用 `m_instances.erase()`，已退出的 Instance 无法通过 ID 查到，统一返回 404。若未来需要保留历史 Instance 记录（如用于审计），可引入 `410 Gone` 区分"从未存在"和"曾存在但已退出"。

### 3.5 `GET /api/instances/{id}/resources`

查询参数：

| 参数 | 类型 | 说明 |
|------|------|------|
| `includeChildren` | bool | 是否包含子进程（默认 true） |

响应（200 OK）：

```json
{
  "instanceId": "inst_abc",
  "timestamp": "2026-02-12T10:30:00Z",
  "processes": [
    { "pid": 12345, "name": "stdiolink_service", "cpuPercent": 2.5,
      "memoryRssBytes": 52428800, "threadCount": 8, "uptimeSeconds": 3600,
      "ioReadBytes": 1048576, "ioWriteBytes": 524288 }
  ],
  "summary": { ... }
}
```

本接口为纯轮询模式，不提供服务端推送。前端建议 2-5 秒间隔轮询。

---

## 4. 实现方案

### 4.1 ProcessMonitor 类

```cpp
// src/stdiolink_server/manager/process_monitor.h
#pragma once

#include "model/process_info.h"
#include <QMap>
#include <QMutex>

namespace stdiolink_server {

class ProcessMonitor {
public:
    ProcessMonitor() = default;

    /// 获取进程的完整子进程树（含资源信息）
    ProcessTreeNode getProcessTree(qint64 rootPid);

    /// 获取单个进程的资源信息
    ProcessInfo getProcessInfo(qint64 pid);

    /// 获取进程及其所有后代的平坦列表
    QVector<ProcessInfo> getProcessFamily(qint64 rootPid,
                                           bool includeChildren = true);

    /// 计算树的汇总统计
    static ProcessTreeSummary summarize(const ProcessTreeNode& tree);
    static ProcessTreeSummary summarize(const QVector<ProcessInfo>& processes);

private:
    /// 获取指定进程的子进程 PID 列表（平台相关实现）
    QVector<qint64> getChildPids(qint64 pid);

    /// 读取单个进程的原始信息（平台相关实现）
    ProcessInfo readProcessInfo(qint64 pid);

    /// CPU 采样缓存
    struct CpuSample {
        qint64 cpuTimeMs = 0;
        QDateTime timestamp;
    };
    QMap<qint64, CpuSample> m_cpuSamples;

    /// 根据两次采样计算 CPU%
    double calculateCpuPercent(qint64 pid, qint64 currentCpuTimeMs);

    /// 清理已退出进程的采样缓存
    void cleanupSamples(const QSet<qint64>& alivePids);
};

} // namespace stdiolink_server
```

### 4.2 Linux 实现（首要平台）

```cpp
// process_monitor_linux.cpp (条件编译)
#ifdef Q_OS_LINUX

ProcessInfo ProcessMonitor::readProcessInfo(qint64 pid) {
    ProcessInfo info;
    info.pid = pid;

    // 读取 /proc/{pid}/stat
    QFile statFile(QString("/proc/%1/stat").arg(pid));
    if (!statFile.open(QIODevice::ReadOnly)) return info;
    QByteArray statData = statFile.readAll();
    // 解析字段时注意：第 2 字段 comm 被括号包围（如 "(my process)"），
    // 且进程名可包含空格、括号、换行符。必须先定位最后一个 ')' 的位置，
    // 以此为分界点解析后续字段。字段 2（comm）由第一个 '(' 到最后一个 ')'
    // 之间的内容确定。简单的空格分割会导致后续字段全部偏移。
    // ...

    // 读取 /proc/{pid}/comm
    QFile commFile(QString("/proc/%1/comm").arg(pid));
    if (commFile.open(QIODevice::ReadOnly))
        info.name = QString(commFile.readAll()).trimmed();

    // 读取 /proc/{pid}/cmdline
    QFile cmdFile(QString("/proc/%1/cmdline").arg(pid));
    if (cmdFile.open(QIODevice::ReadOnly)) {
        QByteArray cmdData = cmdFile.readAll();
        info.commandLine = QString(cmdData.replace('\0', ' ')).trimmed();
    }

    return info;
}

QVector<qint64> ProcessMonitor::getChildPids(qint64 pid) {
    QVector<qint64> children;

    // 优先使用 /proc/{pid}/task/{pid}/children（内核 ≥ 3.5，高效）
    QFile childrenFile(QString("/proc/%1/task/%1/children").arg(pid));
    if (childrenFile.open(QIODevice::ReadOnly)) {
        const QString data = QString(childrenFile.readAll()).trimmed();
        if (!data.isEmpty()) {
            for (const auto& token : data.split(' ', Qt::SkipEmptyParts)) {
                bool ok;
                qint64 childPid = token.toLongLong(&ok);
                if (ok) children.append(childPid);
            }
        }
        return children;
    }

    // 回退：遍历 /proc/*/stat 匹配 ppid
    QDir procDir("/proc");
    for (const auto& entry : procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok;
        qint64 childPid = entry.toLongLong(&ok);
        if (!ok) continue;

        QFile statFile(QString("/proc/%1/stat").arg(childPid));
        if (!statFile.open(QIODevice::ReadOnly)) continue;
        QByteArray data = statFile.readAll();
        // 解析 ppid（第 4 个字段）
        // 如果 ppid == pid，添加到 children
    }
    return children;
}

#endif
```

### 4.3 Windows / macOS 存根

首版为 Windows 和 macOS 提供存根实现（返回空数据 + warning 日志），后续里程碑完善：

```cpp
#if !defined(Q_OS_LINUX)
ProcessInfo ProcessMonitor::readProcessInfo(qint64 pid) {
    qWarning() << "ProcessMonitor: not implemented for this platform";
    ProcessInfo info;
    info.pid = pid;
    info.name = "unknown";
    return info;
}

QVector<qint64> ProcessMonitor::getChildPids(qint64 pid) {
    Q_UNUSED(pid);
    qWarning() << "ProcessMonitor: getChildPids not implemented for this platform";
    return {};
}
#endif
```

### 4.4 ApiRouter handler

```cpp
QHttpServerResponse ApiRouter::handleProcessTree(const QString& id,
                                                  const QHttpServerRequest& req) {
    // 1. 查找 Instance
    auto* inst = m_manager->instanceManager()->findInstance(id);
    if (!inst) return errorResponse(StatusCode::NotFound, "instance not found");
    if (inst->status != "running")
        return errorResponse(StatusCode::NotFound, "instance not running");

    // 2. 获取进程树
    auto tree = m_manager->processMonitor()->getProcessTree(inst->pid);
    auto summary = ProcessMonitor::summarize(tree);

    // 3. 序列化响应
    // ...
}
```

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/stdiolink_server/model/process_info.h` — ProcessInfo/ProcessTreeNode/ProcessTreeSummary 数据模型
- `src/stdiolink_server/manager/process_monitor.h` — ProcessMonitor 声明
- `src/stdiolink_server/manager/process_monitor.cpp` — 通用逻辑 + Linux 实现 + 其他平台存根

### 5.2 修改文件

- `src/stdiolink_server/server_manager.h` — 新增 `ProcessMonitor*` 成员和 getter
- `src/stdiolink_server/server_manager.cpp` — 初始化 ProcessMonitor
- `src/stdiolink_server/http/api_router.h` — 新增 `handleProcessTree`/`handleResources` handler
- `src/stdiolink_server/http/api_router.cpp` — 实现 handler + 路由注册
- `src/stdiolink_server/CMakeLists.txt` — 新增源文件

### 5.3 测试文件

- 新增 `src/tests/test_process_monitor.cpp`
- 修改 `src/tests/test_api_router.cpp` — 进程树/资源 API 测试

---

## 6. 测试与验收

### 6.1 单元测试场景

**ProcessMonitor（test_process_monitor.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 获取当前进程（`getpid()`）的 ProcessInfo | pid 正确，name 非空 |
| 2 | 当前进程 RSS > 0 | memoryRssBytes > 0 |
| 3 | 当前进程线程数 ≥ 1 | threadCount ≥ 1 |
| 4 | 获取不存在的 PID | 返回空/无效 ProcessInfo |
| 5 | 获取当前进程的进程树 | 树根 pid == getpid() |
| 6 | 启动子进程后获取进程树 | children 包含子进程 |
| 7 | 子进程退出后清理采样缓存 | 不遗留已退出 PID 的缓存 |
| 8 | CPU% 首次采样返回 0 | cpuPercent == 0.0 |
| 9 | 两次采样间 CPU% ≥ 0 | 第二次调用 cpuPercent >= 0 |
| 10 | summarize 正确汇总 | totalProcesses/totalMemory/totalThreads 正确 |
| 11 | getProcessFamily 平坦列表 | 包含根进程和所有子进程 |
| 12 | includeChildren=false | 仅返回根进程 |

**注意**：ProcessMonitor 测试依赖平台，CI 在 Linux 上运行。测试中启动 `sleep` 等简单子进程验证进程树采集。macOS/Windows 上使用 `QSKIP("ProcessMonitor not implemented on this platform")` 跳过平台相关测试，仅运行 `summarize()` 等纯逻辑测试。

**API 测试（test_api_router.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 13 | `GET /process-tree` 运行中 Instance | 200 + tree 含根节点 |
| 14 | `GET /process-tree` tree.summary 字段完整 | totalProcesses ≥ 1 |
| 15 | `GET /process-tree` Instance 不存在 | 404 |
| 16 | `GET /process-tree` Instance 已退出 | 404（当前实现进程退出后从内存删除） |
| 17 | `GET /resources` 运行中 Instance | 200 + processes 数组非空 |
| 18 | `GET /resources?includeChildren=false` | processes 仅含 1 个（根进程） |
| 19 | `GET /resources` summary 字段完整 | totalProcesses/totalCpuPercent 等存在 |

### 6.2 验收标准

- ProcessMonitor 在 Linux 上正确采集进程树和资源信息
- 进程树 API 返回完整树结构 + 汇总统计
- 资源 API 返回平坦列表 + 汇总统计
- CPU 采样机制工作正常
- Windows/macOS 有存根实现（不崩溃，返回空数据）
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：`/proc` 文件系统读取权限不足（容器环境）
  - 控制：读取 `/proc/{pid}/stat` 只需对进程有读权限，通常同用户进程无问题；测试中验证权限
- **风险 2**：进程枚举遍历 `/proc` 性能问题
  - 控制：优先使用 `/proc/{pid}/task/{pid}/children`（内核 ≥ 3.5），直接返回子进程列表无需遍历；仅在该文件不存在时回退到全量扫描。Instance 通常子进程较少，性能影响可控
- **风险 3**：CPU 采样缓存无限增长
  - 控制：`cleanupSamples()` 在每次 getProcessTree/getProcessFamily 调用后清理已退出进程的缓存
- **风险 4**：跨平台差异导致数据语义不一致
  - 控制：首版聚焦 Linux 实现，Windows/macOS 提供存根；数据语义在 API 文档中明确说明

---

## 8. 里程碑完成定义（DoD）

- `ProcessMonitor` 类实现（Linux 完整 + Windows/macOS 存根）
- `ProcessInfo`/`ProcessTreeNode`/`ProcessTreeSummary` 数据模型定义
- `GET /api/instances/{id}/process-tree` 实现
- `GET /api/instances/{id}/resources` 实现
- CPU 采样机制工作正常
- 对应单元测试完成并通过
- 本里程碑文档入库


---

# 里程碑 57：SSE 实时事件流

> **前置条件**: 里程碑 49–51 已完成（CORS、Dashboard、Project 操作已就绪）
> **优先级**: P1（所有 P0 里程碑完成后实施）
> **目标**: 实现 `GET /api/events/stream` SSE 端点，推送系统级实时事件，降低前端轮询频率

---

## 1. 目标

- 实现 `EventBus` 全局事件分发器，收集各 Manager 的信号
- 实现 `EventStreamHandler` 管理 SSE 连接和事件推送
- 实现 `GET /api/events/stream` — SSE（Server-Sent Events）端点
- 支持 `?filter=instance,project` 按事件类型过滤
- 连接断开时自动清理资源

---

## 2. 背景与问题

当前 WebUI 需要轮询 API 获取状态变更（Instance 启停、Project 状态变更等），实时性差且增加服务端负载。SSE 提供轻量级的服务端推送能力，浏览器原生支持 `EventSource` API，实现简单。

**技术前置**：基于 `QHttpServerResponder` 的 chunked 写入实现 SSE；本里程碑不引入独立 WebSocket 替代通道。

---

## 3. 技术要点

### 3.1 事件类型

| 事件名 | 触发条件 | 数据 |
|--------|----------|------|
| `instance.started` | Instance 启动 | `{ instanceId, projectId, pid }` |
| `instance.finished` | Instance 退出 | `{ instanceId, projectId, exitCode, status }` |
| `project.status_changed` | Project 状态变更 | `{ projectId, oldStatus, newStatus }` |
| `service.scanned` | Service 扫描完成 | `{ added, removed, updated }` |
| `driver.scanned` | Driver 扫描完成 | `{ scanned, updated }` |
| `schedule.triggered` | 调度触发 | `{ projectId, scheduleType }` |
| `schedule.suppressed` | 调度被抑制 | `{ projectId, reason, consecutiveFailures }` |

### 3.2 SSE 响应格式

```
HTTP/1.1 200 OK
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive

event: instance.started
data: {"instanceId":"inst_abc","projectId":"silo-a","pid":12345}

event: instance.finished
data: {"instanceId":"inst_abc","projectId":"silo-a","exitCode":0,"status":"stopped"}
```

### 3.3 QHttpServer SSE 支持

本里程碑采用 `QHttpServerResponder` 的 chunked 写入能力实现 SSE：

- 连接建立时调用 chunked 写入 API 发送 `text/event-stream` 响应头
- 事件到达时调用 chunk 写入持续推送 `event: ...\ndata: ...\n\n`
- 连接关闭时结束 chunked 传输并清理连接对象

> **实现前须验证**：`QHttpServerResponder` 的 chunked 写入 API 名称和签名需以 Qt 6.10.0 实际头文件为准。文档中使用的 `writeBeginChunked()`/`writeChunk()`/`writeEndChunked()` 为推测名称，实际可能不同。建议先编写最小 demo 验证 chunked 写入流程和 responder 生命周期管理。

不使用 `QTcpSocket*` 绕过 QHttpServer 的实现，避免引入额外生命周期管理风险。

### 3.3.1 心跳与断连检测

SSE 连接断开时，服务端可能无法立即感知（TCP 半开连接问题）。通过定期发送 SSE 注释行作为心跳：

```
: heartbeat\n\n
```

每 30 秒发送一次。如果 `writeChunk()` 返回写入失败，则认为连接已断开，触发清理。

### 3.3.2 连接数限制

SSE 连接数上限 `kMaxSseConnections = 32`。超出时返回 `429 Too Many Requests`。每次新连接建立前检查 `activeConnectionCount() >= kMaxSseConnections`。

### 3.3.3 已知局限：无事件 ID

SSE 标准支持 `id:` 字段用于断线重连（客户端通过 `Last-Event-Id` 请求头恢复丢失事件）。首版不实现事件 ID 机制——断线重连不保证不丢事件。客户端断线重连后应主动调用 REST API 刷新完整状态。后续可引入自增事件 ID + 有限环形缓冲区实现断线恢复。

### 3.4 过滤机制

```
GET /api/events/stream?filter=instance,project
```

`filter` 参数指定感兴趣的事件前缀。如 `instance` 匹配 `instance.started` 和 `instance.finished`。为空则接收所有事件。

---

## 4. 实现方案

### 4.1 EventBus

```cpp
// src/stdiolink_server/http/event_bus.h
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace stdiolink_server {

struct ServerEvent {
    QString type;       // "instance.started" etc
    QJsonObject data;
    QDateTime timestamp;
};

class EventBus : public QObject {
    Q_OBJECT
public:
    explicit EventBus(QObject* parent = nullptr);

    /// 发布事件
    void publish(const QString& type, const QJsonObject& data);

signals:
    /// 所有 SSE 连接监听此信号
    void eventPublished(const ServerEvent& event);
};

} // namespace stdiolink_server
```

### 4.2 EventStreamHandler

```cpp
// src/stdiolink_server/http/event_stream_handler.h
#pragma once

#include <QObject>
#include <QHttpServerResponder>
#include <QSet>
#include <QVector>

namespace stdiolink_server {

class EventBus;

class EventStreamConnection : public QObject {
    Q_OBJECT
public:
    EventStreamConnection(QHttpServerResponder&& responder,
                          const QSet<QString>& filters,
                          QObject* parent = nullptr);
    ~EventStreamConnection();

    void beginStream();
    void sendEvent(const ServerEvent& event);
    bool matchesFilter(const QString& eventType) const;
    bool isOpen() const;

private:
    QHttpServerResponder m_responder;
    QSet<QString> m_filters;
    bool m_open = true;
};

class EventStreamHandler : public QObject {
    Q_OBJECT
public:
    explicit EventStreamHandler(EventBus* bus, QObject* parent = nullptr);

    int activeConnectionCount() const;
    static constexpr int kMaxSseConnections = 32;

private slots:
    void onEventPublished(const ServerEvent& event);
    void onConnectionDisconnected(EventStreamConnection* conn);

private:
    EventBus* m_bus;
    QVector<EventStreamConnection*> m_connections;
};

} // namespace stdiolink_server
```

### 4.3 信号连接

在 `ServerManager` 初始化时，将各 Manager 的信号连接到 `EventBus`：

```cpp
// InstanceManager 信号（已有，参数需适配）
connect(m_instanceManager, &InstanceManager::instanceStarted,
        m_eventBus, [this](const QString& instanceId, const QString& projectId) {
    // pid 需从 Instance 对象获取（信号本身不携带 pid）
    qint64 pid = 0;
    if (auto* inst = m_instanceManager->getInstance(instanceId))
        pid = inst->pid;
    m_eventBus->publish("instance.started", QJsonObject{
        {"instanceId", instanceId},
        {"projectId", projectId},
        {"pid", pid}
    });
});

connect(m_instanceManager, &InstanceManager::instanceFinished,
        m_eventBus, [this](const QString& instanceId, const QString& projectId,
                           int exitCode, QProcess::ExitStatus exitStatus) {
    m_eventBus->publish("instance.finished", QJsonObject{
        {"instanceId", instanceId},
        {"projectId", projectId},
        {"exitCode", exitCode},
        {"status", exitStatus == QProcess::NormalExit ? "normal" : "crashed"}
    });
});

// ScheduleEngine 信号（需新增）类似连接...
```

### 4.4 所需的 Manager 信号补充

`InstanceManager` 已有 `instanceStarted` 和 `instanceFinished` 信号，但参数签名与 EventBus 所需不完全匹配：

| 类 | 现有信号 | 需要调整 |
|----|----------|----------|
| `InstanceManager` | `instanceStarted(instanceId, projectId)` | 缺少 `pid` 参数，需补充或在 EventBus 连接时从 Instance 对象获取 |
| `InstanceManager` | `instanceFinished(instanceId, projectId, exitCode, exitStatus)` | `exitStatus` 是 `QProcess::ExitStatus` 枚举，需转换为字符串 |

其他 Manager 可能缺少必要的信号：

| 类 | 信号 | 说明 |
|----|------|------|
| `ScheduleEngine` | `scheduleTriggered(projectId, scheduleType)` | 调度触发（需新增） |
| `ScheduleEngine` | `scheduleSuppressed(projectId, reason, failures)` | 调度抑制（需新增） |

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/stdiolink_server/http/event_bus.h`
- `src/stdiolink_server/http/event_bus.cpp`
- `src/stdiolink_server/http/event_stream_handler.h`
- `src/stdiolink_server/http/event_stream_handler.cpp`

### 5.2 修改文件

- `src/stdiolink_server/server_manager.h` — 新增 `EventBus*` 成员
- `src/stdiolink_server/server_manager.cpp` — 初始化 EventBus、连接信号
- `src/stdiolink_server/manager/instance_manager.h` — 补充信号声明
- `src/stdiolink_server/manager/instance_manager.cpp` — 在适当位置 emit 信号
- `src/stdiolink_server/manager/schedule_engine.h` — 补充信号声明
- `src/stdiolink_server/manager/schedule_engine.cpp` — 在适当位置 emit 信号
- `src/stdiolink_server/http/api_router.cpp` — 注册 SSE 路由
- `src/stdiolink_server/CMakeLists.txt` — 新增源文件

### 5.3 测试文件

- 新增 `src/tests/test_event_bus.cpp`
- 修改 `src/tests/test_api_router.cpp` — SSE 连接测试（如可行）

---

## 6. 测试与验收

### 6.1 单元测试场景

**EventBus（test_event_bus.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | publish 事件后信号触发 | eventPublished 信号参数正确 |
| 2 | 事件包含 timestamp | ServerEvent.timestamp 有效 |
| 3 | 多次 publish 多次触发 | 每次 publish 独立触发信号 |
| 4 | 连接过滤器 `instance` 匹配 `instance.started` | matchesFilter 返回 true |
| 5 | 过滤器 `instance` 不匹配 `project.status_changed` | matchesFilter 返回 false |
| 6 | 空过滤器匹配所有事件 | matchesFilter 始终返回 true |
| 7 | 多个过滤器 `instance,project` | 匹配两类事件 |

**Manager 信号触发**：

| # | 场景 | 验证点 |
|---|------|--------|
| 8 | Instance 启动时 emit instanceStarted | 信号参数含 instanceId/projectId/pid |
| 9 | Instance 退出时 emit instanceFinished | 信号参数含 exitCode/status |
| 10 | 调度触发时 emit scheduleTriggered | 信号参数含 projectId/scheduleType |
| 11 | 调度抑制时 emit scheduleSuppressed | 信号参数含 reason/failures |

**SSE API（test_api_router.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 12 | `GET /api/events/stream` | 响应 Content-Type 为 `text/event-stream` |
| 13 | 事件推送格式 | `event: type\ndata: json\n\n` |
| 14 | filter 参数过滤 | 仅收到匹配的事件 |
| 15 | 客户端断开后清理 | 连接数递减，无资源泄漏 |

### 6.2 验收标准

- EventBus 正确分发事件
- 各 Manager 在关键节点 emit 信号
- SSE 端点可接受连接并推送事件
- 过滤机制工作正常
- 连接断开后自动清理
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：SSE chunked 写入 API 名称/签名与实际不符
  - 控制：实现前先编写最小 demo 验证 `QHttpServerResponder` 的 chunked 写入流程；确认 responder 的所有权语义（move-only 还是可拷贝）
  - **降级方案**：如 `QHttpServerResponder` 不支持 chunked streaming，改用 M55 已验证的 WebSocket 通道推送事件（复用 `addWebSocketUpgradeVerifier` 基础设施），或退回到客户端短轮询（`GET /api/events/poll?since=<timestamp>`）
- **风险 2**：SSE 连接断开后服务端无法及时感知（TCP 半开连接）
  - 控制：每 30 秒发送心跳注释行（`: heartbeat\n\n`），写入失败即触发清理
- **风险 3**：Manager 信号补充影响已有逻辑
  - 控制：仅新增信号声明和 emit，不修改已有逻辑流程；信号是单向通知，不影响调用方

---

## 8. 里程碑完成定义（DoD）

- `EventBus` 实现并连接各 Manager 信号
- `EventStreamHandler` 实现
- SSE 端点可推送事件
- Manager 信号在关键节点正确触发
- 过滤机制可用
- 对应单元测试完成并通过
- 本里程碑文档入库

