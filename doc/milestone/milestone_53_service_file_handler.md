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
