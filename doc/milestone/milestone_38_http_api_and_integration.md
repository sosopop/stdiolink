# 里程碑 38：HTTP API 与 ServerManager 集成

> **前置条件**: 里程碑 34–37 全部完成
> **目标**: 基于 `QHttpServer` 实现 RESTful API，提供 Service/Project/Instance/Driver 接口，整合 ServerManager 编排层，预留 WebSocket 扩展点

---

## 1. 目标

- 基于 Qt 官方 `QHttpServer`（`Qt6::HttpServer`）实现 HTTP API 服务器
- 实现 Service API（只读）、Project API（CRUD + 操作）、Instance API（只读 + 终止）、Driver API（列表 + 重扫）
- 实现 `ServerManager` 编排层，统一持有所有子系统引用
- 在 `main.cpp` 中完成最终集成：启动 HTTP 服务器
- 预留 WebSocket 扩展点（`Qt6::WebSockets`），为后续实时事件推送做准备
- 安全边界：默认监听 `127.0.0.1`，不内置鉴权

---

## 2. 技术要点

### 2.1 HTTP 服务器选型

使用 Qt 官方 `QHttpServer`（`Qt6::HttpServer` 模块）。原因：

- Qt 官方维护，API 稳定，与 Qt 事件循环无缝集成
- 内置路由注册（`route()`）、路径参数（`<arg>`）、HTTP 方法过滤
- 无需手动解析 HTTP 协议，减少自定义代码量
- 已通过 vcpkg 引入 `qthttpserver` 依赖

**关键 API**：

| 类 | 用途 |
|----|------|
| `QHttpServer` | 路由注册、请求分发 |
| `QHttpServerRequest` | 只读请求对象（method、body、url、query、headers） |
| `QHttpServerResponse` | 响应值类型（status code、body、content-type） |
| `QAbstractHttpServer::listen()` | 监听端口 |

### 2.2 WebSocket 预留

当前里程碑不实现 WebSocket 功能，但在架构上预留扩展点：

- CMakeLists.txt 中链接 `Qt6::WebSockets`
- `ServerManager` 预留 `QWebSocketServer*` 成员（注释状态）
- 后续可通过 `QAbstractHttpServer::bind()` 共享 TCP 端口，或独立端口提供 WebSocket 服务

### 2.3 API 路由总览

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/services` | 列出所有 Service |
| GET | `/api/services/<arg>` | 获取 Service 详情和 Schema |
| GET | `/api/projects` | 列出所有 Project |
| GET | `/api/projects/<arg>` | 获取 Project 详情 |
| POST | `/api/projects` | 创建 Project |
| PUT | `/api/projects/<arg>` | 更新 Project |
| DELETE | `/api/projects/<arg>` | 删除 Project |
| POST | `/api/projects/<arg>/validate` | 验证配置（不保存） |
| POST | `/api/projects/<arg>/start` | 启动 Project |
| POST | `/api/projects/<arg>/stop` | 停止 Project |
| POST | `/api/projects/<arg>/reload` | 重新加载配置 |
| GET | `/api/instances` | 列出运行中 Instance |
| POST | `/api/instances/<arg>/terminate` | 终止 Instance |
| GET | `/api/instances/<arg>/logs` | 获取 Instance 日志 |
| GET | `/api/drivers` | 列出已发现 Driver |
| POST | `/api/drivers/scan` | 手动触发 Driver 重扫 |

### 2.4 请求/响应约定

- 请求体和响应体均为 JSON（`Content-Type: application/json`）
- 成功响应：`200 OK`、`201 Created`、`204 No Content`
- 错误响应：`400 Bad Request`、`404 Not Found`、`409 Conflict`、`500 Internal Server Error`
- 错误响应体格式：`{"error": "描述信息"}`
- JSON 响应统一使用 `QHttpServerResponse("application/json", body, statusCode)` 构造
- `GET /api/services/{id}` 的 `configSchema` 建议返回 M34 缓存的源格式 schema（`rawConfigSchema`），避免与 `toJson()` 标准化输出混用

### 2.5 安全边界

- v1 默认监听 `127.0.0.1`，不内置鉴权
- 若使用 `--host=0.0.0.0`，需在外层反向代理提供鉴权
- 破坏性接口（start/stop/delete/terminate/scan）建议仅在受信网络暴露

---

## 3. 实现步骤

### 3.1 JSON 响应辅助函数

由于 `QHttpServerResponse` 不会自动设置 `Content-Type: application/json`，封装辅助函数统一处理：

```cpp
// src/stdiolink_server/http/http_helpers.h
#pragma once

#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace stdiolink_server {

inline QHttpServerResponse jsonResponse(
    const QJsonObject& obj,
    QHttpServerResponse::StatusCode code = QHttpServerResponse::StatusCode::Ok)
{
    return QHttpServerResponse(
        "application/json",
        QJsonDocument(obj).toJson(QJsonDocument::Compact),
        code);
}

inline QHttpServerResponse jsonResponse(
    const QJsonArray& arr,
    QHttpServerResponse::StatusCode code = QHttpServerResponse::StatusCode::Ok)
{
    return QHttpServerResponse(
        "application/json",
        QJsonDocument(arr).toJson(QJsonDocument::Compact),
        code);
}

inline QHttpServerResponse errorResponse(
    QHttpServerResponse::StatusCode code,
    const QString& message)
{
    QJsonObject obj{{"error", message}};
    return QHttpServerResponse(
        "application/json",
        QJsonDocument(obj).toJson(QJsonDocument::Compact),
        code);
}

} // namespace stdiolink_server
```

### 3.2 ApiRouter 头文件

将路由注册与处理逻辑封装为 `ApiRouter` 类，负责在 `QHttpServer` 上注册所有 API 路由：

```cpp
// src/stdiolink_server/http/api_router.h
#pragma once

#include <QHttpServer>
#include <QObject>

namespace stdiolink_server {

class ServerManager;

class ApiRouter : public QObject {
    Q_OBJECT
public:
    explicit ApiRouter(ServerManager* manager,
                       QObject* parent = nullptr);

    /// 在指定 QHttpServer 上注册所有 API 路由
    void registerRoutes(QHttpServer& server);

private:
    // Service API
    QHttpServerResponse handleServiceList(
        const QHttpServerRequest& req);
    QHttpServerResponse handleServiceDetail(
        const QString& id, const QHttpServerRequest& req);

    // Project API
    QHttpServerResponse handleProjectList(
        const QHttpServerRequest& req);
    QHttpServerResponse handleProjectDetail(
        const QString& id, const QHttpServerRequest& req);
    QHttpServerResponse handleProjectCreate(
        const QHttpServerRequest& req);
    QHttpServerResponse handleProjectUpdate(
        const QString& id, const QHttpServerRequest& req);
    QHttpServerResponse handleProjectDelete(
        const QString& id, const QHttpServerRequest& req);
    QHttpServerResponse handleProjectValidate(
        const QString& id, const QHttpServerRequest& req);
    QHttpServerResponse handleProjectStart(
        const QString& id, const QHttpServerRequest& req);
    QHttpServerResponse handleProjectStop(
        const QString& id, const QHttpServerRequest& req);
    QHttpServerResponse handleProjectReload(
        const QString& id, const QHttpServerRequest& req);

    // Instance API
    QHttpServerResponse handleInstanceList(
        const QHttpServerRequest& req);
    QHttpServerResponse handleInstanceTerminate(
        const QString& id, const QHttpServerRequest& req);
    QHttpServerResponse handleInstanceLogs(
        const QString& id, const QHttpServerRequest& req);

    // Driver API
    QHttpServerResponse handleDriverList(
        const QHttpServerRequest& req);
    QHttpServerResponse handleDriverScan(
        const QHttpServerRequest& req);

    ServerManager* m_manager;
};

} // namespace stdiolink_server
```

### 3.3 ServerManager 编排层

```cpp
// src/stdiolink_server/server_manager.h
#pragma once

#include <QObject>
#include "config/server_config.h"
#include "scanner/service_scanner.h"
#include "scanner/driver_manager_scanner.h"
#include "manager/project_manager.h"
#include "manager/instance_manager.h"
#include "manager/schedule_engine.h"
#include "stdiolink/host/driver_catalog.h"

namespace stdiolink_server {

class ServerManager : public QObject {
    Q_OBJECT
public:
    explicit ServerManager(const QString& dataRoot,
                           const ServerConfig& config,
                           QObject* parent = nullptr);

    /// 初始化：扫描 Service/Driver，加载 Project
    bool initialize(QString& error);

    /// 启动调度
    void startScheduling();

    /// 优雅关闭
    void shutdown();

    // 子系统访问
    const QMap<QString, ServiceInfo>& services() const;
    QMap<QString, Project>& projects();
    InstanceManager* instanceManager();
    ScheduleEngine* scheduleEngine();
    ProjectManager* projectManager();
    stdiolink::DriverCatalog* driverCatalog();

    /// Driver 重扫
    DriverManagerScanner::ScanStats rescanDrivers(
        bool refreshMeta = true);

    QString dataRoot() const { return m_dataRoot; }

private:
    QString m_dataRoot;
    ServerConfig m_config;

    ServiceScanner m_serviceScanner;
    DriverManagerScanner m_driverScanner;
    ProjectManager m_projectManager;
    InstanceManager* m_instanceManager;
    ScheduleEngine* m_scheduleEngine;

    QMap<QString, ServiceInfo> m_services;
    QMap<QString, Project> m_projects;
    stdiolink::DriverCatalog m_driverCatalog;

    // WebSocket 预留（后续里程碑启用）
    // QWebSocketServer* m_wsServer = nullptr;
};

} // namespace stdiolink_server
```

### 3.4 ServerManager 实现

```cpp
// src/stdiolink_server/server_manager.cpp
#include "server_manager.h"
#include <QDir>

namespace stdiolink_server {

ServerManager::ServerManager(const QString& dataRoot,
                             const ServerConfig& config,
                             QObject* parent)
    : QObject(parent)
    , m_dataRoot(dataRoot)
    , m_config(config)
{
    m_instanceManager = new InstanceManager(
        dataRoot, config, this);
    m_scheduleEngine = new ScheduleEngine(
        m_instanceManager, this);
}
```

**`initialize()` 初始化流程**：

```cpp
bool ServerManager::initialize(QString& error) {
    QDir root(m_dataRoot);
    if (!root.exists()) {
        error = "data root does not exist: " + m_dataRoot;
        return false;
    }

    // 1. 扫描 Service
    ServiceScanner::ScanStats svcStats;
    m_services = m_serviceScanner.scan(
        m_dataRoot + "/services", &svcStats);
    qInfo("Services: %d loaded, %d failed",
          svcStats.loadedServices, svcStats.failedServices);

    // 2. 扫描 Driver
    QString driversDir = m_dataRoot + "/drivers";
    if (QDir(driversDir).exists()) {
        DriverManagerScanner::ScanStats drvStats;
        auto drivers = m_driverScanner.scan(
            driversDir, true, &drvStats);
        m_driverCatalog.replaceAll(drivers);
        qInfo("Drivers: %d updated, %d failed, %d skipped",
              drvStats.updated, drvStats.newlyFailed,
              drvStats.skippedFailed);
    }

    // 3. 加载并验证 Project
    ProjectManager::LoadStats projStats;
    m_projects = m_projectManager.loadAll(
        m_dataRoot + "/projects", m_services, &projStats);
    qInfo("Projects: %d loaded, %d invalid",
          projStats.loaded, projStats.invalid);

    error.clear();
    return true;
}
```

**`startScheduling()` 和 `shutdown()`**：

```cpp
void ServerManager::startScheduling() {
    m_scheduleEngine->startAll(m_projects, m_services);
}

void ServerManager::shutdown() {
    m_scheduleEngine->setShuttingDown(true);
    m_scheduleEngine->stopAll();
    m_instanceManager->terminateAll();
    m_instanceManager->waitAllFinished(5000);
}
```

### 3.5 ApiRouter 路由注册

```cpp
// src/stdiolink_server/http/api_router.cpp（路由注册）
#include "api_router.h"
#include "http_helpers.h"
#include "server_manager.h"

using Method = QHttpServerRequest::Method;

namespace stdiolink_server {

ApiRouter::ApiRouter(ServerManager* manager, QObject* parent)
    : QObject(parent), m_manager(manager) {}

void ApiRouter::registerRoutes(QHttpServer& server) {
    // Service API
    server.route("/api/services", Method::Get,
        [this](const QHttpServerRequest& req) {
            return handleServiceList(req);
        });
    server.route("/api/services/<arg>", Method::Get,
        [this](const QString& id,
               const QHttpServerRequest& req) {
            return handleServiceDetail(id, req);
        });

    // Project API
    server.route("/api/projects", Method::Get,
        [this](const QHttpServerRequest& req) {
            return handleProjectList(req);
        });
    server.route("/api/projects", Method::Post,
        [this](const QHttpServerRequest& req) {
            return handleProjectCreate(req);
        });
    server.route("/api/projects/<arg>", Method::Get,
        [this](const QString& id,
               const QHttpServerRequest& req) {
            return handleProjectDetail(id, req);
        });
    server.route("/api/projects/<arg>", Method::Put,
        [this](const QString& id,
               const QHttpServerRequest& req) {
            return handleProjectUpdate(id, req);
        });
    server.route("/api/projects/<arg>", Method::Delete,
        [this](const QString& id,
               const QHttpServerRequest& req) {
            return handleProjectDelete(id, req);
        });

    // Project 操作 API
    server.route("/api/projects/<arg>/validate", Method::Post,
        [this](const QString& id,
               const QHttpServerRequest& req) {
            return handleProjectValidate(id, req);
        });
    server.route("/api/projects/<arg>/start", Method::Post,
        [this](const QString& id,
               const QHttpServerRequest& req) {
            return handleProjectStart(id, req);
        });
    server.route("/api/projects/<arg>/stop", Method::Post,
        [this](const QString& id,
               const QHttpServerRequest& req) {
            return handleProjectStop(id, req);
        });
    server.route("/api/projects/<arg>/reload", Method::Post,
        [this](const QString& id,
               const QHttpServerRequest& req) {
            return handleProjectReload(id, req);
        });

    // Instance API
    server.route("/api/instances", Method::Get,
        [this](const QHttpServerRequest& req) {
            return handleInstanceList(req);
        });
    server.route("/api/instances/<arg>/terminate", Method::Post,
        [this](const QString& id,
               const QHttpServerRequest& req) {
            return handleInstanceTerminate(id, req);
        });
    server.route("/api/instances/<arg>/logs", Method::Get,
        [this](const QString& id,
               const QHttpServerRequest& req) {
            return handleInstanceLogs(id, req);
        });

    // Driver API
    server.route("/api/drivers", Method::Get,
        [this](const QHttpServerRequest& req) {
            return handleDriverList(req);
        });
    server.route("/api/drivers/scan", Method::Post,
        [this](const QHttpServerRequest& req) {
            return handleDriverScan(req);
        });

    // 404 兜底
    server.setMissingHandler(
        [](const QHttpServerRequest&,
           QHttpServerResponder&& responder) {
            QJsonObject err{{"error", "not found"}};
            responder.write(
                QJsonDocument(err),
                QHttpServerResponder::StatusCode::NotFound);
        });
}

} // namespace stdiolink_server
```

### 3.6 API 处理示例

**Service 列表**：

```cpp
QHttpServerResponse ApiRouter::handleServiceList(
    const QHttpServerRequest& req)
{
    Q_UNUSED(req);
    const auto& services = m_manager->services();
    const auto& projects = m_manager->projects();

    QJsonArray arr;
    for (auto it = services.begin();
         it != services.end(); ++it) {
        const ServiceInfo& svc = it.value();
        int projCount = 0;
        for (const auto& p : projects) {
            if (p.serviceId == svc.id) projCount++;
        }
        QJsonObject obj;
        obj["id"] = svc.id;
        obj["name"] = svc.name;
        obj["version"] = svc.version;
        obj["serviceDir"] = svc.serviceDir;
        obj["hasSchema"] = svc.hasSchema;
        obj["projectCount"] = projCount;
        arr.append(obj);
    }
    return jsonResponse(QJsonObject{{"services", arr}});
}
```

**Project 启动（含并发控制）**：

```cpp
QHttpServerResponse ApiRouter::handleProjectStart(
    const QString& id, const QHttpServerRequest& req)
{
    Q_UNUSED(req);
    auto& projects = m_manager->projects();
    if (!projects.contains(id))
        return errorResponse(
            QHttpServerResponse::StatusCode::NotFound,
            "project not found");

    const Project& p = projects[id];
    if (!p.valid)
        return errorResponse(
            QHttpServerResponse::StatusCode::BadRequest,
            "project invalid: " + p.error);

    const auto& services = m_manager->services();
    if (!services.contains(p.serviceId))
        return errorResponse(
            QHttpServerResponse::StatusCode::BadRequest,
            "service not found: " + p.serviceId);

    auto* instMgr = m_manager->instanceManager();
    int running = instMgr->instanceCount(id);
```

```cpp
    // 并发控制
    switch (p.schedule.type) {
    case ScheduleType::Manual:
        if (running > 0)
            return errorResponse(
                QHttpServerResponse::StatusCode::Conflict,
                "already running");
        break;
    case ScheduleType::FixedRate:
        if (running >= p.schedule.maxConcurrent)
            return errorResponse(
                QHttpServerResponse::StatusCode::Conflict,
                "max concurrent reached");
        break;
    case ScheduleType::Daemon:
        if (running > 0)
            return jsonResponse(QJsonObject{{"noop", true}});
        break;
    }
```

```cpp
    QString serviceDir = services[p.serviceId].serviceDir;
    QString error;
    QString instId = instMgr->startInstance(
        p, serviceDir, error);
    if (instId.isEmpty())
        return errorResponse(
            QHttpServerResponse::StatusCode::InternalServerError,
            error);

    auto* inst = instMgr->getInstance(instId);
    QJsonObject result;
    result["instanceId"] = instId;
    result["pid"] = inst ? (qint64)inst->pid : 0;
    return jsonResponse(result);
}
```

### 3.7 main.cpp 最终集成

```cpp
// src/stdiolink_server/main.cpp — 最终版本
#include <QCoreApplication>
#include <QDir>
#include <QHttpServer>
#include <csignal>

#include "config/server_args.h"
#include "config/server_config.h"
#include "server_manager.h"
#include "http/api_router.h"
#include "stdiolink/platform/platform_utils.h"

using namespace stdiolink_server;
```

**`main()` 函数**：

```cpp
int main(int argc, char* argv[]) {
    stdiolink::PlatformUtils::initConsoleEncoding();
    QCoreApplication app(argc, argv);

    // 1. 解析命令行参数
    auto args = ServerArgs::parse(app.arguments());
    if (args.help) { printHelp(); return 0; }
    if (args.version) {
        fprintf(stderr, "stdiolink_server 0.1.0\n");
        return 0;
    }
    if (!args.error.isEmpty()) {
        fprintf(stderr, "Error: %s\n",
                qUtf8Printable(args.error));
        return 2;
    }

    QString dataRoot = QDir(args.dataRoot).absolutePath();
```

```cpp
    // 2. 加载配置
    QString cfgErr;
    auto config = ServerConfig::loadFromFile(
        dataRoot + "/config.json", cfgErr);
    if (!cfgErr.isEmpty()) {
        fprintf(stderr, "Error: %s\n",
                qUtf8Printable(cfgErr));
        return 2;
    }
    config.applyArgs(args);

    if (!ensureDirectories(dataRoot)) return 1;
```

```cpp
    // 3. 初始化 ServerManager
    ServerManager manager(dataRoot, config);
    QString initErr;
    if (!manager.initialize(initErr)) {
        fprintf(stderr, "Init error: %s\n",
                qUtf8Printable(initErr));
        return 1;
    }

    // 4. 启动调度
    manager.startScheduling();
```

```cpp
    // 5. 启动 HTTP 服务器
    QHttpServer httpServer;
    ApiRouter apiRouter(&manager);
    apiRouter.registerRoutes(httpServer);

    const auto port = httpServer.listen(
        QHostAddress(config.host), config.port);
    if (port == 0) {
        fprintf(stderr, "Cannot listen on %s:%d\n",
                qUtf8Printable(config.host), config.port);
        return 1;
    }
    qInfo("HTTP server listening on %s:%d",
          qUtf8Printable(config.host), config.port);
```

```cpp
    // 6. 信号处理（示例：生产环境建议使用更安全的 signal-bridge）
    std::signal(SIGINT, [](int) {
        QMetaObject::invokeMethod(
            qApp, []() { qApp->quit(); },
            Qt::QueuedConnection);
    });
    std::signal(SIGTERM, [](int) {
        QMetaObject::invokeMethod(
            qApp, []() { qApp->quit(); },
            Qt::QueuedConnection);
    });

    QObject::connect(&app,
        &QCoreApplication::aboutToQuit,
        [&]() {
            manager.shutdown();
        });

    return app.exec();
}
```

---

## 4. 文件清单

| 操作 | 文件 | 说明 |
|------|------|------|
| 新增 | `src/stdiolink_server/http/http_helpers.h` | JSON 响应辅助函数 |
| 新增 | `src/stdiolink_server/http/api_router.h` | API 路由注册头文件 |
| 新增 | `src/stdiolink_server/http/api_router.cpp` | API 路由注册及处理实现 |
| 新增 | `src/stdiolink_server/server_manager.h` | ServerManager 头文件 |
| 新增 | `src/stdiolink_server/server_manager.cpp` | ServerManager 实现 |
| 修改 | `src/stdiolink_server/main.cpp` | 最终集成版本 |
| 修改 | `src/stdiolink_server/CMakeLists.txt` | 新增源文件，链接 Qt6::HttpServer + Qt6::WebSockets |
| 修改 | `CMakeLists.txt` | find_package 添加 HttpServer 组件 |

---

## 5. CMake 构建配置变更

### 5.1 根 CMakeLists.txt

```cmake
# 修改 find_package，添加 HttpServer
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network WebSockets HttpServer)
```

### 5.2 src/stdiolink_server/CMakeLists.txt

```cmake
# 新增源文件
set(STDIOLINK_SERVER_SOURCES
    main.cpp
    config/server_args.cpp
    config/server_config.cpp
    scanner/service_scanner.cpp
    scanner/driver_manager_scanner.cpp
    manager/project_manager.cpp
    manager/instance_manager.cpp
    manager/schedule_engine.cpp
    model/schedule.cpp
    http/api_router.cpp
    server_manager.cpp
)
```

```cmake
# 链接库：新增 Qt6::HttpServer，预留 Qt6::WebSockets
target_link_libraries(stdiolink_server PRIVATE
    stdiolink_service_config
    stdiolink
    Qt6::Core
    Qt6::Network
    Qt6::HttpServer
    Qt6::WebSockets    # 预留，后续里程碑启用
)
```

---

## 6. 验收标准

1. HTTP 服务器能在指定 host:port 上监听（通过 `QHttpServer::listen()`）
2. `GET /api/services` 返回所有已加载 Service 列表
3. `GET /api/services/{id}` 返回 Service 详情和 Schema
4. `GET /api/projects` 返回所有 Project 及运行状态
5. `GET /api/projects/{id}` 返回 Project 详情含 Instance 列表
6. `POST /api/projects` 创建 Project，验证通过返回 201，失败返回 400
7. `PUT /api/projects/{id}` 更新 Project，请求体 id 与路径不一致返回 409
8. `DELETE /api/projects/{id}` 删除 Project 返回 204
9. `POST /api/projects/{id}/validate` 验证配置返回验证结果
10. `POST /api/projects/{id}/start` 启动 Project，并发控制正确
11. `POST /api/projects/{id}/stop` 停止 Project 所有 Instance
12. `POST /api/projects/{id}/reload` 重新加载配置文件
13. `GET /api/instances` 返回运行中 Instance 列表，支持 `projectId` 筛选
14. `POST /api/instances/{id}/terminate` 终止指定 Instance
15. `GET /api/instances/{id}/logs` 返回 Project 级日志
16. `GET /api/drivers` 返回已发现 Driver 列表
17. `POST /api/drivers/scan` 触发 Driver 重扫并返回统计
18. 不存在的路径返回 404（通过 `setMissingHandler`）
19. `ServerManager.initialize()` 正确编排 Service/Driver/Project 扫描
20. `ServerManager.shutdown()` 优雅关闭所有子系统
21. 所有 JSON 响应 Content-Type 为 `application/json`
22. CMakeLists.txt 正确链接 `Qt6::HttpServer` 和 `Qt6::WebSockets`

---

## 7. 单元测试用例

### 7.1 ApiRouter 路由测试

```cpp
#include <gtest/gtest.h>
#include <QHttpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>

// 集成测试：验证路由注册和 JSON 响应格式
// 需要 mock ServerManager 或使用测试替身
```

### 7.2 JSON 辅助函数测试

```cpp
#include <gtest/gtest.h>
#include "http/http_helpers.h"

using namespace stdiolink_server;

TEST(HttpHelpersTest, JsonResponseObject) {
    auto resp = jsonResponse(
        QJsonObject{{"ok", true}});
    EXPECT_EQ(resp.statusCode(),
              QHttpServerResponse::StatusCode::Ok);
}

TEST(HttpHelpersTest, ErrorResponse) {
    auto resp = errorResponse(
        QHttpServerResponse::StatusCode::NotFound,
        "not found");
    EXPECT_EQ(resp.statusCode(),
              QHttpServerResponse::StatusCode::NotFound);
}
```

---

## 8. 依赖关系

### 8.1 前置依赖

| 依赖项 | 说明 |
|--------|------|
| 里程碑 34（Server 脚手架） | `ServerArgs`、`ServerConfig`、`ServiceScanner` |
| 里程碑 35（DriverManagerScanner） | `DriverManagerScanner` |
| 里程碑 36（ProjectManager） | `ProjectManager`、`Project`、`Schedule` |
| 里程碑 37（InstanceManager） | `InstanceManager`、`ScheduleEngine` |
| Qt6::HttpServer | `QHttpServer`、`QHttpServerRequest`、`QHttpServerResponse` |
| Qt6::WebSockets | 预留，当前仅链接不使用 |

### 8.2 后置影响

本里程碑为 `stdiolink_server` 的最终集成里程碑，完成后服务管理器具备完整功能。后续可基于 `Qt6::WebSockets` 扩展实时事件推送。
