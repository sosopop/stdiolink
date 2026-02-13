# 里程碑 58：后端静态文件服务

> **前置条件**: 里程碑 49–57 已完成（HTTP API、CORS、SSE 已就绪）
> **目标**: 在 `stdiolink_server` 中实现静态文件服务路由，支持嵌入式部署 WebUI 前端构建产物，无需外部 Nginx 反向代理

---

## 1. 目标

- 实现 `StaticFileServer` 工具类，安全地从指定目录提供静态文件服务
- 在 `ApiRouter` 中注册静态文件路由，将非 `/api` 请求映射到 WebUI 构建产物目录
- 支持 SPA 路由回退（所有未匹配路径返回 `index.html`）
- 支持常见 MIME 类型自动检测
- 支持 `Cache-Control` 响应头（带 hash 的资源文件长缓存，`index.html` 不缓存）
- 通过 `--webui-dir` 命令行参数或 `config.json` 中的 `webuiDir` 字段配置静态文件目录
- 目录不存在或为空时静默跳过（不影响 API 服务正常运行）

---

## 2. 背景与问题

当前 `stdiolink_server` 仅提供 REST API / WebSocket / SSE 服务，WebUI 前端需要通过 Nginx 等外部反向代理部署。嵌入式部署（前端构建产物与后端二进制同目录）可简化部署流程，降低运维复杂度。

设计文档（`doc/stdiolink_webui_design.md` §10.3）描述了两种部署方案：嵌入式和独立部署。本里程碑实现嵌入式部署所需的后端静态文件服务能力。

---

## 3. 技术要点

### 3.1 路由优先级

静态文件路由必须在所有 API 路由之后注册，确保 `/api/*` 请求优先匹配 API handler：

```
1. /api/*          → ApiRouter handlers（已有）
2. /assets/*       → 静态文件（带 hash 的资源，长缓存）
3. /*              → SPA 回退（返回 index.html）
```

### 3.2 MIME 类型映射

```cpp
static const QMap<QString, QByteArray> kMimeTypes = {
    {"html",  "text/html; charset=utf-8"},
    {"js",    "application/javascript; charset=utf-8"},
    {"css",   "text/css; charset=utf-8"},
    {"json",  "application/json; charset=utf-8"},
    {"png",   "image/png"},
    {"jpg",   "image/jpeg"},
    {"jpeg",  "image/jpeg"},
    {"gif",   "image/gif"},
    {"svg",   "image/svg+xml"},
    {"ico",   "image/x-icon"},
    {"woff",  "font/woff"},
    {"woff2", "font/woff2"},
    {"ttf",   "font/ttf"},
    {"map",   "application/json"},
};
```

### 3.3 缓存策略

| 路径模式 | Cache-Control | 说明 |
|----------|---------------|------|
| `index.html` | `no-cache, no-store, must-revalidate` | 每次请求都验证，确保获取最新版本 |
| `/assets/*`（含 hash） | `public, max-age=31536000, immutable` | Vite 构建产物带内容 hash，可长期缓存 |
| 其他静态文件 | `public, max-age=3600` | 1 小时缓存 |

### 3.4 安全约束

- 路径遍历防护：禁止 `..` 路径段，使用 `QDir::cleanPath()` 规范化后验证是否仍在 webui 目录内
- 仅允许 GET 和 HEAD 方法
- 文件大小上限 10MB（防止意外大文件阻塞响应）
- 不跟随符号链接（`QFileInfo::isSymLink()` 检查）

### 3.5 配置方式

命令行参数优先于配置文件：

```bash
# 命令行
stdiolink_server --webui-dir=/path/to/webui/dist

# config.json
{
  "webuiDir": "webui"   // 相对于 dataRoot 的路径
}
```

默认值：`{dataRoot}/webui`。如果目录不存在，日志输出 info 级别提示并跳过静态文件路由注册。

---

## 4. 实现方案

### 4.1 StaticFileServer 类

```cpp
// src/stdiolink_server/http/static_file_server.h
#pragma once

#include <QHttpServerResponse>
#include <QString>
#include <QMap>

namespace stdiolink_server {

class StaticFileServer {
public:
    explicit StaticFileServer(const QString& rootDir);

    /// 检查 webui 目录是否有效（存在且包含 index.html）
    bool isValid() const;

    /// 根据请求路径返回静态文件响应
    /// @param path URL 路径（如 "/assets/index-abc123.js"）
    /// @return 文件响应或 404
    QHttpServerResponse serve(const QString& path) const;

    /// SPA 回退：返回 index.html
    QHttpServerResponse serveIndex() const;

    /// 获取根目录
    QString rootDir() const;

private:
    /// 解析安全路径，防止目录遍历
    /// @return 空字符串表示路径不安全
    QString resolveSafePath(const QString& urlPath) const;

    /// 根据文件扩展名获取 MIME 类型
    static QByteArray mimeType(const QString& filePath);

    /// 根据文件路径确定 Cache-Control 头
    static QByteArray cacheControl(const QString& filePath);

    QString m_rootDir;
    bool m_valid = false;
};

} // namespace stdiolink_server
```

### 4.2 StaticFileServer 实现

```cpp
// src/stdiolink_server/http/static_file_server.cpp

StaticFileServer::StaticFileServer(const QString& rootDir)
    : m_rootDir(QDir::cleanPath(rootDir))
{
    QFileInfo indexFile(m_rootDir + "/index.html");
    m_valid = indexFile.exists() && indexFile.isFile();
}

bool StaticFileServer::isValid() const { return m_valid; }

QString StaticFileServer::resolveSafePath(const QString& urlPath) const {
    // 禁止 .. 路径段
    if (urlPath.contains(".."))
        return {};

    QString cleaned = QDir::cleanPath(urlPath);
    // 去除前导 /
    if (cleaned.startsWith('/'))
        cleaned = cleaned.mid(1);

    QString fullPath = QDir::cleanPath(m_rootDir + "/" + cleaned);

    // 验证仍在 rootDir 内
    if (!fullPath.startsWith(m_rootDir + "/") && fullPath != m_rootDir)
        return {};

    QFileInfo fi(fullPath);
    // 不跟随符号链接
    if (fi.isSymLink())
        return {};

    if (!fi.exists() || !fi.isFile())
        return ;

    // 文件大小上限 10MB
    if (fi.size() > 10 * 1024 * 1024)
        return {};

    return fullPath;
}

QHttpServerResponse StaticFileServer::serve(const QString& path) const {
    if (!m_valid)
        return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);

    QString filePath = resolveSafePath(path);
    if (filePath.isEmpty()) {
        // SPA 回退：非 API、非静态资源的路径返回 index.html
        return serveIndex();
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QHttpServerResponse(QHttpServerResponder::StatusCode::InternalServerError);

    QByteArray content = file.readAll();
    QByteArray mime = mimeType(filePath);
    QByteArray cache = cacheControl(filePath);

    QHttpServerResponse response(mime, content, QHttpServerResponder::StatusCode::Ok);
    response.addHeader("Cache-Control", cache);
    return response;
}

QHttpServerResponse StaticFileServer::serveIndex() const {
    QFile file(m_rootDir + "/index.html");
    if (!file.open(QIODevice::ReadOnly))
        return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);

    QByteArray content = file.readAll();
    QHttpServerResponse response("text/html; charset=utf-8", content,
                                  QHttpServerResponder::StatusCode::Ok);
    response.addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    return response;
}
```

### 4.3 ServerArgs 扩展

```cpp
// server_args.h 新增
QString webuiDir;  // --webui-dir 参数
```

### 4.4 ServerConfig 扩展

```cpp
// server_config.h 新增
QString webuiDir;  // config.json 中的 "webuiDir" 字段
```

### 4.5 ApiRouter 路由注册

在所有 API 路由注册完成后，追加静态文件路由：

```cpp
// api_router.cpp — registerRoutes() 末尾

if (m_staticFileServer && m_staticFileServer->isValid()) {
    // 捕获所有未匹配的 GET 请求
    server.route("/<arg>", QHttpServerRequest::Method::Get,
                 [this](const QString& path, const QHttpServerRequest& req) {
        Q_UNUSED(req);
        return m_staticFileServer->serve("/" + path);
    });

    // 根路径
    server.route("/", QHttpServerRequest::Method::Get,
                 [this](const QHttpServerRequest& req) {
        Q_UNUSED(req);
        return m_staticFileServer->serveIndex();
    });
}
```

> **注意**：QHttpServer 按注册顺序匹配路由。静态文件路由必须在所有 `/api/*` 路由之后注册，否则会拦截 API 请求。需验证 `/<arg>` 通配路由不会与已注册的 `/api/...` 路由冲突。如果冲突，改用 `missingHandler` 回退方案。

### 4.6 missingHandler 回退方案

如果通配路由与 API 路由冲突，改用 QHttpServer 的 missing handler：

```cpp
// 在 missingHandler 中增加静态文件回退
server.setMissingHandler([this](const QHttpServerRequest& req,
                                 QHttpServerResponder&& responder) {
    if (req.method() == QHttpServerRequest::Method::Get && m_staticFileServer) {
        auto response = m_staticFileServer->serve(req.url().path());
        // 写入 response...
        return;
    }
    // 原有 404 处理
    // ...
});
```

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/stdiolink_server/http/static_file_server.h` — StaticFileServer 声明
- `src/stdiolink_server/http/static_file_server.cpp` — StaticFileServer 实现

### 5.2 修改文件

- `src/stdiolink_server/config/server_args.h` — 新增 `webuiDir` 参数
- `src/stdiolink_server/config/server_args.cpp` — 解析 `--webui-dir`
- `src/stdiolink_server/config/server_config.h` — 新增 `webuiDir` 字段
- `src/stdiolink_server/config/server_config.cpp` — 从 JSON 读取 `webuiDir`
- `src/stdiolink_server/server_manager.h` — 新增 `StaticFileServer*` 成员
- `src/stdiolink_server/server_manager.cpp` — 初始化 StaticFileServer
- `src/stdiolink_server/http/api_router.h` — 新增 `StaticFileServer*` 成员
- `src/stdiolink_server/http/api_router.cpp` — 注册静态文件路由
- `src/stdiolink_server/CMakeLists.txt` — 新增源文件

### 5.3 测试文件

- 新增 `src/tests/test_static_file_server.cpp`

---

## 6. 测试与验收

### 6.1 单元测试场景

**StaticFileServer（test_static_file_server.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 有效目录（含 index.html） | `isValid()` 返回 true |
| 2 | 无效目录（不存在） | `isValid()` 返回 false |
| 3 | 无效目录（存在但无 index.html） | `isValid()` 返回 false |
| 4 | 请求 `/index.html` | 返回 200 + `text/html` + `no-cache` |
| 5 | 请求 `/assets/index-abc123.js` | 返回 200 + `application/javascript` + `immutable` |
| 6 | 请求 `/assets/style-def456.css` | 返回 200 + `text/css` + `immutable` |
| 7 | 请求 `/favicon.ico` | 返回 200 + `image/x-icon` |
| 8 | 请求不存在的文件 `/nonexistent.txt` | SPA 回退，返回 index.html |
| 9 | 路径遍历 `/../../../etc/passwd` | 返回 index.html（安全回退），不泄露文件 |
| 10 | 路径遍历 `/..%2F..%2Fetc/passwd` | URL 解码后仍被拦截 |
| 11 | 符号链接文件 | 不跟随，返回 index.html |
| 12 | 超大文件（>10MB） | 不提供服务，返回 index.html |
| 13 | `serveIndex()` 返回正确内容 | 内容与 index.html 文件一致 |
| 14 | MIME 类型检测：`.woff2` | 返回 `font/woff2` |
| 15 | MIME 类型检测：未知扩展名 | 返回 `application/octet-stream` |
| 16 | 根路径 `/` | 返回 index.html |

**配置测试**：

| # | 场景 | 验证点 |
|---|------|--------|
| 17 | `--webui-dir` 命令行参数 | 正确解析到 `ServerArgs::webuiDir` |
| 18 | `config.json` 中 `webuiDir` 字段 | 正确读取 |
| 19 | 命令行参数覆盖配置文件 | 命令行优先 |
| 20 | 目录不存在时 | 静态文件路由不注册，API 正常工作 |

### 6.2 验收标准

- StaticFileServer 正确提供静态文件
- 路径遍历攻击被有效防御
- SPA 路由回退正常工作
- 缓存策略正确应用
- 目录不存在时不影响 API 服务
- MIME 类型正确检测
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：通配路由 `/<arg>` 与已有 API 路由冲突
  - 控制：静态文件路由在所有 API 路由之后注册；如冲突则改用 missingHandler 方案
  - 验证：增加回归测试确认所有 API 端点仍可正常访问
- **风险 2**：大文件读取阻塞事件循环
  - 控制：10MB 文件大小上限；Vite 构建产物通常远小于此限制
- **风险 3**：路径安全漏洞
  - 控制：多层防御（`..` 检查 + `cleanPath` + 前缀验证 + 符号链接检查）

---

## 8. 里程碑完成定义（DoD）

- `StaticFileServer` 类实现
- 静态文件路由注册
- SPA 回退机制工作正常
- 路径安全防护到位
- 缓存策略正确
- 配置参数支持
- 对应单元测试完成并通过
- 本里程碑文档入库
