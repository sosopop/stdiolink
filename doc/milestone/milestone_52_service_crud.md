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

请求体（可选）：

```json
{
  "force": false
}
```

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
std::optional<ServiceInfo> loadSingle(const QString& serviceDir) const;
```

复用已有 `loadService()` 的逻辑，不执行目录扫描，仅加载指定目录的 Service。创建 Service 后优先调用此方法加载到内存，避免全量 `scan()`。

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

任何步骤失败时回滚（删除已创建的目录）。

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
1. 检查 Service 存在性（内存 m_services）
2. 查找关联 Project
3. 非 force 且有关联 → 409
4. force 时：标记关联 Project invalid + 设 error
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

---

## 8. 里程碑完成定义（DoD）

- `POST /api/services` 实现并支持三种模板
- `DELETE /api/services/{id}` 实现并支持 force 选项
- `ServiceScanner::loadSingle()` 实现
- Service 创建后自动加载到内存
- 对应单元测试完成并通过
- 本里程碑文档入库
