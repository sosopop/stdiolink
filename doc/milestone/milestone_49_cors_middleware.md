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

QHttpServer 不支持真正的通配路由（`/api/*`），需按路径段数分别注册 `<arg>` 占位符。当前 API 最深路径为 4 段（如 `/api/services/{id}/files/content`），因此注册 1-4 段即可覆盖。后续新增更深层 API 路径时需同步补充 OPTIONS 路由。

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
  - 控制：OPTIONS 与 GET/POST/PUT/DELETE 按方法分发互不冲突；当新增更深层 API 路径时同步补充 OPTIONS 规则并加回归测试
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
