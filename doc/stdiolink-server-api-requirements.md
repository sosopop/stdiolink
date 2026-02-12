# stdiolink_server API 需求设计文档

> 版本: 2.0.0
> 日期: 2026-02-12
> 基线: 当前代码 M48 已落地，现有 API 见 `CLAUDE.md` 中 Server API 速览

---

## 0. 技术预演调研

在进入正式研发之前，以下技术点存在实现不确定性，需要通过独立的 spike / PoC 实验逐一验证，确保团队对实现方式达成共识，避免研发阶段出现歧义或返工。

**调研方法优先级**：每项调研优先通过 **网络搜索**（Qt 官方文档、Qt Forum、Stack Overflow、GitHub Issues/Discussions）寻找已有方案和最佳实践，仅在网络资料不足以得出结论时才编写 PoC demo 验证。这一策略已在调研项 0.1 中验证有效——通过查阅 Qt 6.8 文档即确认了 `addWebSocketUpgradeVerifier` 原生支持，避免了编写独立端口方案的无效工作。

### 0.1 QHttpServer 原生 WebSocket 升级验证

**~~不确定性~~（已解决）**：Qt 6.8 起，`QAbstractHttpServer` 新增原生 WebSocket 升级支持，无需独立的 `QWebSocketServer`。

**已确认的 API**（参见 [QHttpServerWebSocketUpgradeResponse](https://doc.qt.io/qt-6/qhttpserverwebsocketupgraderesponse.html)、[QAbstractHttpServer](https://doc.qt.io/qt-6/qabstracthttpserver.html)）：

1. `addWebSocketUpgradeVerifier(context, handler)` — 注册回调，接收 `QHttpServerRequest`，返回 `Accept` / `Deny` / `PassToNext`
2. `newWebSocketConnection()` 信号 — 升级成功后触发
3. `nextPendingWebSocketConnection()` — 返回 `std::unique_ptr<QWebSocket>`

**当前项目 Qt 版本**：6.10.0（vcpkg），满足 ≥ 6.8 要求。

**结论**：HTTP 和 WebSocket 共享同一端口，无需独立 `QWebSocketServer`，无需 `wsPort = port + 1` 方案。

**仍需验证**：

> 🔍 **优先网络搜索**：搜索 `QWebSocket disconnected signal reliability`、`QHttpServer WebSocket concurrent connections`、`addWebSocketUpgradeVerifier URL routing` 等关键词，查阅 Qt Forum 和 Qt Bug Tracker 中的相关讨论。

1. 确认 `QWebSocket::disconnected()` 信号在各种断开场景下的可靠性：正常关闭、浏览器标签页关闭、网络中断、进程崩溃
2. 确认单个 QHttpServer 实例的 WebSocket 并发连接数上限和内存开销
3. 确认 `addWebSocketUpgradeVerifier` 回调中能否根据 URL path 做路由分发（如 `/api/driverlab/{driverId}`）

**验收标准**：产出一个最小 demo，QHttpServer 同时提供 REST API 和 WebSocket 端点，WS 连接建立时拉起一个子进程，WS 断开时终止子进程。

### 0.2 WebSocket 跨域握手行为

**不确定性**：WebUI SPA 与 API 服务器必然跨域。由于 WebSocket 升级现在通过 QHttpServer 原生处理（`addWebSocketUpgradeVerifier`），需要确认升级请求中 `Origin` 头的处理方式。

> 🔍 **优先网络搜索**：搜索 `WebSocket same-origin policy browser`、`WebSocket Origin header CORS`、`QHttpServerRequest headers`。WebSocket 协议的跨域行为是通用 Web 知识，大概率可通过 MDN、RFC 6455 和 Stack Overflow 直接得出结论，无需编写 demo。

**调研目标**：

1. 确认 `addWebSocketUpgradeVerifier` 回调中的 `QHttpServerRequest` 是否包含 `Origin` 头
2. 验证浏览器端 `new WebSocket('ws://other-origin')` 是否受同源策略限制（WebSocket 协议本身不受 CORS 约束，但浏览器会发送 `Origin` 头）
3. 确认是否需要在 verifier 回调中手动校验/放行 `Origin`

**验收标准**：从 `http://localhost:3000`（前端 dev server）成功连接到 `ws://localhost:8080/api/driverlab/test`（QHttpServer 同端口 WebSocket），双向通信正常。

### 0.3 QHttpServer CORS 中间件实现

**不确定性**：QHttpServer 的中间件/过滤器机制不如 Express 等框架成熟。需要确认如何为所有响应统一注入 CORS 头，以及如何处理 `OPTIONS` 预检请求。

> 🔍 **优先网络搜索**：搜索 `QHttpServer CORS`、`QHttpServer afterRequest hook`、`QHttpServer OPTIONS preflight`、`QHttpServer middleware`。Qt 6.8+ 新增了多项 QHttpServer API，官方文档和 Qt Forum 可能已有 CORS 实现示例。同时搜索 `QHttpServer::route Method::Options` 确认 OPTIONS 路由注册方式。

**调研目标**：

1. 确认 QHttpServer 是否支持 `afterRequest` 钩子或全局响应拦截
2. 如不支持，验证替代方案：在每个 handler 中手动添加头 vs 自定义 `QHttpServerResponder` wrapper vs 使用 `QHttpServer::setMissingHandler` 兜底 OPTIONS
3. 确认 `OPTIONS` 请求的路由注册方式（QHttpServer 是否支持 `Method::Options` 通配）

**验收标准**：前端 `fetch('http://localhost:8080/api/services')` 跨域请求成功，浏览器控制台无 CORS 错误，`OPTIONS` 预检返回 204。

### 0.4 跨平台进程树采集

**不确定性**：获取子进程树和资源占用的 API 在 Linux / macOS / Windows 三个平台上完全不同，且部分 API 需要特殊权限。

> 🔍 **优先网络搜索**：各平台 API 均有成熟的文档和开源实现可参考：
> - macOS：搜索 `proc_listchildpids macOS example`、`proc_pidinfo PROC_PIDTASKINFO`、`sysctl KERN_PROC child processes`
> - Linux：搜索 `linux /proc/pid/stat parse CPU memory`、`/proc/pid/task/tid/children`
> - Windows：搜索 `CreateToolhelp32Snapshot process tree`、`GetProcessMemoryInfo example`
> - 跨平台：搜索 `Qt process tree monitoring`、`cross-platform process info C++`，查看是否有现成的轻量库（如 `reproc`、`psutil` C++ 移植）可直接复用
> - CPU 采样：搜索 `calculate CPU usage percentage two samples`

**调研目标**：

1. **macOS**：验证 `proc_listchildpids()` / `proc_pidinfo()` 的可用性和权限要求；确认是否需要 `sysctl` 方案作为 fallback
2. **Linux**：验证 `/proc/{pid}/stat` + `/proc/{pid}/status` + `/proc/{pid}/children` 的读取方式；确认非 root 用户能否读取其他用户进程的信息
3. **Windows**：验证 `CreateToolhelp32Snapshot` + `GetProcessMemoryInfo` + `GetProcessTimes` 的可用性
4. CPU 使用率需要两次采样计算差值，验证采样间隔（建议 500ms）和精度
5. 确认 Qt 是否有跨平台封装（`QProcess` 只提供 PID，不提供子进程枚举和资源查询）

**验收标准**：在当前开发平台上，给定一个 PID，能正确返回其子进程树和每个进程的 CPU%、RSS、线程数。产出 `ProcessMonitor` 工具类的平台相关实现骨架。

### 0.5 QHttpServer SSE（Server-Sent Events）支持

**不确定性**：QHttpServer 的标准用法是请求-响应模式，SSE 需要保持连接并持续写入。需要确认 `QHttpServerResponder` 是否支持流式写入。

> 🔍 **优先网络搜索**：搜索 `QHttpServer SSE Server-Sent Events`、`QHttpServerResponder streaming`、`QHttpServerResponder chunked transfer`、`QHttpServer keep connection open`。Qt 6.8+ 对 QHttpServer 做了大量增强，官方文档和 changelog 中可能已有流式响应的说明。同时搜索 `Qt 6.8 QHttpServer new features` 查看是否有相关 API 新增。如网络资料不足，再编写 demo 验证。

**调研目标**：

1. 验证 `QHttpServerResponder` 能否在不关闭连接的情况下多次写入数据
2. 确认 `Content-Type: text/event-stream` + `Transfer-Encoding: chunked` 的设置方式
3. 验证客户端断开连接时服务端能否收到通知（用于清理资源）
4. 如 QHttpServer 不支持 SSE，评估替代方案：降级为长轮询 / 使用 QWebSocketServer 替代 SSE

**验收标准**：浏览器 `EventSource` 能成功连接并持续接收服务端推送的事件，断开后服务端正确清理。

### 0.6 Service 文件操作的路径安全

**不确定性**：路径穿越防护的实现细节需要验证，特别是 `QDir::cleanPath()` 对各种恶意路径的处理行为。

> 🔍 **优先网络搜索**：搜索 `QDir::cleanPath path traversal security`、`Qt path traversal prevention`、`QDir::cleanPath symlink behavior`、`canonicalFilePath vs cleanPath`。路径穿越是常见安全问题，Qt 社区和安全相关文章中大概率有 `cleanPath` 行为的详细分析。同时搜索 `OWASP path traversal prevention cheat sheet` 获取通用防御模式，对照 Qt API 确认覆盖度。

**调研目标**：

1. 验证 `QDir::cleanPath()` 对以下输入的处理：`../etc/passwd`、`foo/../../etc/passwd`、`foo/./bar/../../../etc/passwd`、符号链接
2. 确认 `absoluteFilePath()` + `startsWith()` 前缀检查是否足以防御所有穿越场景
3. 确认符号链接场景：如果 Service 目录内有符号链接指向外部，`cleanPath` 不会解析符号链接——是否需要额外用 `QFileInfo::canonicalFilePath()` 做二次校验（仅对已存在文件）

**验收标准**：编写测试用例覆盖至少 10 种路径穿越变体，全部被正确拦截。

### 调研产出物

每项调研完成后产出：

- 最小可运行 demo 代码（放入 `src/demo/` 或独立分支）
- 结论文档（1 页以内），记录：方案选择、已验证的边界条件、已知限制
- 如有多种可行方案，给出推荐方案及理由

所有调研完成后，更新本文档中对应章节的 `⚠️ 待调研确认` 标记为最终方案。

---

## 1. 需求背景

当前 `stdiolink_server` 已具备基础的 Service 扫描、Project CRUD、Instance 管理和 Driver 管理能力。为支撑 WebUI 的完整功能落地，需要在现有 API 基础上扩展以下四大功能域：

1. **Service 创建**：支持手动编写 JS 脚本（需代码高亮编辑器）、通过 UI 创建和编辑 schema 模板
2. **Project 创建**：选择 Service 后根据其 schema 自动生成配置控件，填写配置后创建 Project
3. **Driver 在线测试（DriverLab Web）**：参照桌面端 `src/driverlab` 实现 Web 版，根据 Driver 的 meta 自动生成测试 UI，支持命令执行与结果展示
4. **Instance 进程树与资源监控**：展示单个服务的进程树，包含每个进程的 CPU、内存等资源占用数据，支持树状展示

此外，Dashboard 需要汇总系统级统计信息，提供健康检查、事件推送等辅助能力。

---

## 2. 需求拆解

### 2.1 Service 创建与管理

当前 Service 的生命周期依赖文件系统扫描（`ServiceScanner` 从 `data_root/services/` 读取子目录），不支持通过 API 创建或修改 Service 内容。WebUI 需要以下能力：

#### 2.1.1 Service 目录创建

- 在 `data_root/services/` 下创建新的 Service 子目录
- 生成标准的三文件结构：`manifest.json`、`index.js`、`config.schema.json`
- 支持从模板创建（空模板、带示例代码的模板等）

#### 2.1.2 Manifest 编辑

- 通过 UI 表单编辑 manifest 字段：`id`、`name`、`version`、`description`、`author`
- 服务端需校验 manifest 格式合法性（`manifestVersion` 固定 `"1"`、`id` 唯一性等）
- 保存后写入 `manifest.json` 文件

#### 2.1.3 JS 脚本编辑

- WebUI 端使用代码编辑器（如 Monaco / CodeMirror）编辑 `index.js`
- 服务端需提供读取和写入 `index.js` 内容的 API
- 可选：支持多文件编辑（Service 目录下可能有其他 `.js` 模块）

#### 2.1.4 Schema 模板编辑

- 通过 UI 可视化编辑 `config.schema.json`
- 支持增删改字段、设置字段类型（`string`/`int`/`double`/`bool`/`enum`/`array`/`object`/`any`）
- 支持设置约束（`min`/`max`/`minLength`/`maxLength`/`pattern`/`enumValues`/`format`/`minItems`/`maxItems`）
- 支持设置 UI Hint（`widget`/`group`/`order`/`placeholder`/`advanced`/`readonly`/`visibleIf`/`unit`/`step`）
- 支持设置 `required`、`default`、`description`
- 服务端需提供 schema 的读取与写入 API，以及 schema 格式校验能力

#### 2.1.5 Service 删除

- 删除 Service 目录（需检查是否有关联的 Project 正在使用）
- 危险操作，需前端二次确认

#### 2.1.6 Service 文件列表

- 获取 Service 目录下的文件列表（用于多文件编辑场景）
- 支持读取任意文本文件内容

### 2.2 Project 创建（基于 Schema 的配置生成）

现有 `POST /api/projects` 已支持 Project 创建，`GET /api/services/{id}` 已返回 `configSchema`。WebUI 端根据 schema 生成表单控件的逻辑在前端实现，但服务端需要补充以下能力：

#### 2.2.1 Schema 增强返回与格式统一

- 现有 `GET /api/services/{id}` 已返回 `configSchema`（原始 JSON），足以支撑前端表单生成
- 需确保 `configSchema` 中包含完整的 FieldMeta 信息（`type`/`required`/`default`/`description`/`constraints`/`ui`/`fields`/`items`）

**⚠️ 格式差异问题**：Service 的 `config.schema.json` 是扁平 key-value 格式（`{"port": {"type": "int", ...}}`，字段名是 key），而 Driver Meta 的 `commands[].params` 是数组格式（`[{"name": "port", "type": "int", ...}]`，字段名在 `name` 属性里）。前端需要根据 schema 生成配置表单（Service 场景）和命令参数表单（DriverLab 场景），两套格式意味着两套解析逻辑。

**解决方案**：`GET /api/services/{id}` 的响应中同时返回两种格式：

- `configSchema`：保留原始 key-value 格式（向后兼容，也用于 schema 编辑器回写）
- `configSchemaFields`：后端调用 `ServiceConfigSchema::toJson()` 转换为 FieldMeta 数组格式（与 Driver Meta 的 `params` 结构一致）

前端表单生成器只需对接 FieldMeta 数组格式这一套逻辑。后端实现：`ServiceInfo` 已持有解析后的 `ServiceConfigSchema configSchema`（含 `QVector<FieldMeta> fields`），只需在 `handleServiceDetail()` 中调用 `configSchema.toJson()` 即可。

#### 2.2.2 配置预校验

- 现有 `POST /api/projects/{id}/validate` 支持对已存在 Project 的配置校验
- 新增：支持在 Project 创建前，针对某个 Service 的 schema 校验一份配置草稿（无需先创建 Project）

#### 2.2.3 配置默认值生成

- 服务端根据 schema 中的 `default` 字段生成一份填好默认值的配置草稿
- 减少前端逻辑，统一默认值解析策略

### 2.3 Driver 在线测试（DriverLab Web）

桌面端 DriverLab 的核心流程：选择 Driver → 启动进程 → 获取 Meta → 展示命令列表 → 选择命令 → 根据参数 Meta 生成表单 → 执行命令 → 展示结果。Web 版需要服务端代理整个 Driver 进程交互过程。

#### 2.3.1 Driver 详情与 Meta

- 获取 Driver 的完整 Meta 信息（`DriverMeta`），包含 `info`、`config`、`commands`、`types` 等
- 现有 `GET /api/drivers` 仅返回摘要（`id`/`program`/`metaHash`/`name`/`version`），不含完整 meta
- 需新增 Driver 详情 API 返回完整 meta

#### 2.3.2 WebSocket 生命周期绑定

核心设计原则：**用 WebSocket 连接的生命周期绑定 Driver 进程的生命周期**。

Web 端无法直接拉起 Driver 进程。与其用 REST API 管理 Session（需引入 idle timeout、session 表、轮询等复杂机制），不如用 WebSocket 连接状态作为"用户是否在场"的天然信号：

- **连接 = 启动**：客户端建立 WebSocket 连接时，服务端拉起 Driver 子进程、查询 Meta、推送给前端
- **通信 = stdio 透传**：WebSocket 是 Driver stdin/stdout 的网络延伸。前端发 JSON 命令消息 → 服务端转发到 Driver stdin；Driver stdout 产出的 JSONL 消息（ok/event/error） → 服务端原样推给前端
- **断开 = 终止**：WebSocket 断开（关标签页、导航离开、网络中断） → 服务端立即 kill Driver 进程。反向同理，Driver 进程退出 → 服务端主动关闭 WebSocket。双向联动，任何一方断开另一方也断
- 前端渲染为 **Shell 风格**的交互界面，展示 stdio 输出流

#### 2.3.3 命令执行

- 通过 WebSocket 发送命令请求（JSON），服务端转发到 Driver stdin
- Driver 的所有 stdout 输出（ok/event/error）实时推送给前端
- 无需轮询，无需异步查询——WebSocket 本身就是实时双向通道

### 2.4 Instance 进程树与资源监控

当前 Instance 仅记录顶层 `QProcess` 的 PID 和状态。WebUI 需要展示完整的进程树及资源占用。

#### 2.4.1 进程树获取

- 以 Instance 主进程 PID 为根，递归获取所有子进程
- 构建树状结构返回（`stdiolink_service` 进程可能拉起多个 Driver 子进程）
- 每个进程节点包含：PID、进程名、命令行参数、父 PID、状态

#### 2.4.2 进程资源占用

- 获取每个进程的实时资源数据：
  - CPU 使用率（百分比）
  - 内存占用（RSS / VMS）
  - 线程数
  - 启动时间
  - 运行时长
- 跨平台支持（Windows: NtQueryInformationProcess / PDH, Linux: /proc, macOS: proc_pidinfo）

#### 2.4.3 实时监控数据

- 支持定时采集（轮询方式）
- 可选：通过 SSE/WebSocket 推送实时监控数据

#### 2.4.4 Instance 增强详情

- 现有 Instance 结构（`id`/`projectId`/`serviceId`/`pid`/`startedAt`/`status`）需扩展
- 新增：退出码、退出时间、工作目录、日志路径、命令行参数等信息

### 2.5 Dashboard 与系统功能

#### 2.5.1 Server 状态总览

- 系统健康状态、版本信息、启动时间、运行时长
- 各实体计数：Service 数、Project 数（按状态分类）、运行中 Instance 数、Driver 数
- 资源总览：Server 进程自身的 CPU/内存占用

#### 2.5.2 实时事件流

- 通过 SSE 或 WebSocket 推送系统事件：
  - Instance 启动/停止/异常退出
  - Project 状态变更
  - Service 扫描完成
  - Driver 扫描完成
  - Schedule 触发/抑制
- 降低前端轮询频率，提升实时性

#### 2.5.3 Project 列表增强

- 支持过滤（按 `serviceId`、`status`、`enabled`）
- 支持分页
- 支持批量运行态查询（避免 N+1 请求）

#### 2.5.4 Project 运行日志

- 直接通过 Project ID 获取日志（当前日志文件按 `logs/{projectId}.log` 存储，Instance 退出后不可查）

### 2.6 跨域访问（CORS）

WebUI 作为 SPA 与 API 服务器必然跨域（即使同机器也是不同端口）。当前 `http_helpers.h` 和整个 server 代码中没有任何 CORS 处理，前端无法正常访问 API。

#### 2.6.1 HTTP CORS

- 为所有 REST API 响应添加 CORS 头：`Access-Control-Allow-Origin`、`Access-Control-Allow-Methods`、`Access-Control-Allow-Headers`、`Access-Control-Max-Age`
- 注册 `OPTIONS` 方法的通配路由处理预检请求，返回 204 + CORS 头
- `ServerConfig` 新增 `corsOrigin` 字段（默认 `"*"`），支持配置文件指定允许的源

> ⚠️ 待调研确认：QHttpServer 的全局响应拦截机制（见调研项 0.3）

#### 2.6.2 WebSocket 跨域

- `QWebSocketServer` 的跨域握手行为需要验证
- 如需限制，在 `handleUpgrade` 中校验 `Origin` 头

> ⚠️ 待调研确认：QWebSocketServer 默认跨域行为（见调研项 0.2）

---

## 3. API 接口设计

以下 API 按功能域分组。所有 API 统一使用 JSON 请求/响应体，错误格式 `{"error": "message"}`。

### 3.1 Service 管理 API（扩展）

#### 3.1.1 `POST /api/services` — 创建 Service

创建一个新的 Service 目录，生成标准的三文件结构。

**请求体**：

```json
{
  "id": "my-service",
  "name": "My Service",
  "version": "1.0.0",
  "description": "A demo service",
  "author": "dev",
  "template": "empty",
  "indexJs": "// optional initial code\nimport { getConfig } from 'stdiolink';\n",
  "configSchema": {
    "port": {
      "type": "int",
      "required": true,
      "default": 8080,
      "description": "Listen port"
    }
  }
}
```

**字段说明**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `id` | string | ✅ | Service 唯一标识，合法字符 `[a-zA-Z0-9_-]` |
| `name` | string | ✅ | 显示名称 |
| `version` | string | ✅ | 语义化版本号 |
| `description` | string | ❌ | 描述 |
| `author` | string | ❌ | 作者 |
| `template` | string | ❌ | 模板类型：`empty`（默认）、`basic`、`driver_demo` |
| `indexJs` | string | ❌ | 初始 JS 代码，为空则使用模板默认代码 |
| `configSchema` | object | ❌ | 初始 schema，为空则使用模板默认 schema |

**模板内容定义**：

当 `indexJs` 或 `configSchema` 未提供时，根据 `template` 生成默认内容：

**`empty`**（默认）：

```js
// index.js
import { getConfig } from 'stdiolink';

const config = getConfig();
```

`config.schema.json` → `{}`

**`basic`**：

```js
// index.js
import { getConfig, openDriver } from 'stdiolink';
import { log } from 'stdiolink/log';

const config = getConfig();
log.info('service started', { config });

// TODO: implement service logic
```

`config.schema.json` →

```json
{
  "name": {
    "type": "string",
    "required": true,
    "description": "Service display name"
  }
}
```

**`driver_demo`**：

```js
// index.js
import { getConfig, openDriver } from 'stdiolink';
import { log } from 'stdiolink/log';

const config = getConfig();
const driver = openDriver(config.driverPath);
const task = driver.request('meta.describe');
const meta = task.wait();
log.info('driver meta', meta);
driver.close();
```

`config.schema.json` →

```json
{
  "driverPath": {
    "type": "string",
    "required": true,
    "description": "Path to driver executable"
  }
}
```

如果用户同时提供了 `indexJs` 和/或 `configSchema`，则忽略模板默认内容，以用户提供的为准。

**响应**（201 Created）：

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

**错误码**：

| 状态码 | 场景 |
|--------|------|
| 400 | id 不合法、必填字段缺失 |
| 409 | id 已存在 |
| 500 | 文件系统写入失败 |

**后端实现要点**：

- 在 `data_root/services/{id}/` 下创建目录
- 写入 `manifest.json`（`manifestVersion` 固定 `"1"`）
- 写入 `index.js`（用户提供或模板默认）
- 写入 `config.schema.json`（用户提供或空对象）
- 创建后通过 `ServiceScanner::scan()` 重新扫描 `data_root/services/` 加载到内存（`loadService()` 是 private 方法，不可直接调用；或为 `ServiceScanner` 新增一个 public 的 `loadSingle(const QString& serviceDir)` 方法）
- `ServerManager::m_services` 中注册新 Service

---

#### 3.1.2 `DELETE /api/services/{id}` — 删除 Service

删除 Service 目录及其所有文件。

**前置检查**：

- 检查是否有关联的 Project（`project.serviceId == id`）
- 如有关联 Project，默认拒绝删除，除非请求体包含 `"force": true`
- 强制删除时，关联 Project 将标记为 `invalid`

**请求体**（可选）：

```json
{
  "force": false
}
```

**响应**（204 No Content）

**错误码**：

| 状态码 | 场景 |
|--------|------|
| 404 | Service 不存在 |
| 409 | 有关联 Project 且未 force |
| 500 | 文件删除失败 |

**后端实现要点**：

- 递归删除 `data_root/services/{id}/` 目录
- 从 `ServerManager::m_services` 中移除
- 如 force 删除，遍历关联 Project 设 `valid = false`，更新 `error`

---

#### 3.1.3 `GET /api/services/{id}/files` — 获取 Service 文件列表

返回 Service 目录下的文件清单。

**响应**（200 OK）：

```json
{
  "serviceId": "my-service",
  "serviceDir": "/path/to/services/my-service",
  "files": [
    {
      "name": "manifest.json",
      "path": "manifest.json",
      "size": 234,
      "modifiedAt": "2026-02-12T10:30:00Z",
      "type": "json"
    },
    {
      "name": "index.js",
      "path": "index.js",
      "size": 1024,
      "modifiedAt": "2026-02-12T10:30:00Z",
      "type": "javascript"
    },
    {
      "name": "config.schema.json",
      "path": "config.schema.json",
      "size": 512,
      "modifiedAt": "2026-02-12T10:30:00Z",
      "type": "json"
    },
    {
      "name": "utils.js",
      "path": "lib/utils.js",
      "size": 256,
      "modifiedAt": "2026-02-12T10:30:00Z",
      "type": "javascript"
    }
  ]
}
```

**后端实现要点**：

- 递归遍历 `data_root/services/{id}/` 目录
- 返回相对路径、文件大小、修改时间
- 根据扩展名推断文件类型（`json`/`javascript`/`text`/`unknown`）

---

#### 3.1.4 `GET /api/services/{id}/files/content?path=` — 读取 Service 文件内容

读取 Service 目录下指定文件的文本内容。

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | string | ✅ | 文件相对路径（URL 编码），如 `index.js`、`lib/utils.js` |

**响应**（200 OK）：

```json
{
  "path": "index.js",
  "content": "import { getConfig } from 'stdiolink';\n\nconst cfg = getConfig();\nconsole.log(cfg);\n",
  "size": 82,
  "modifiedAt": "2026-02-12T10:30:00Z"
}
```

**错误码**：

| 状态码 | 场景 |
|--------|------|
| 404 | Service 或文件不存在 |
| 400 | 路径包含 `..` 等非法字符（路径穿越防护） |
| 413 | 文件过大（超过 1MB 限制） |

**后端实现要点**：

- **安全关键**：必须对 `path` 做路径穿越检测，确保最终路径仍在 Service 目录内
- 仅允许读取文本文件，二进制文件返回 415
- 文件大小上限 1MB

---

#### 3.1.5 `PUT /api/services/{id}/files/content?path=` — 写入 Service 文件内容

更新 Service 目录下指定文件的内容。

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | string | ✅ | 文件相对路径 |

**请求体**：

```json
{
  "content": "import { getConfig } from 'stdiolink';\n\nconst cfg = getConfig();\n// updated\n"
}
```

**响应**（200 OK）：

```json
{
  "path": "index.js",
  "size": 90,
  "modifiedAt": "2026-02-12T10:35:00Z"
}
```

**特殊处理**：

- 写入 `manifest.json` 时：自动解析并校验 manifest 格式，更新内存中的 ServiceInfo
- 写入 `config.schema.json` 时：自动解析并校验 schema 格式，更新内存中的 ServiceInfo；可选触发关联 Project 重验

**请求体可选字段**：

```json
{
  "content": "...",
  "revalidateProjects": true
}
```

**写入原子性保障**：

文件写入采用 write-to-temp-then-rename 策略，避免写入中途崩溃导致文件损坏：

1. 将内容写入同目录下的临时文件（如 `index.js.tmp`）
2. 调用 `QFile::rename()` 原子替换目标文件（POSIX 系统上 `rename(2)` 是原子操作）
3. 如果 rename 失败，删除临时文件并返回 500

对于 `manifest.json` 和 `config.schema.json`，先校验内容格式合法性，校验通过后再执行写入。校验失败直接返回 400，不触发任何文件 I/O。

**错误码**：

| 状态码 | 场景 |
|--------|------|
| 404 | Service 不存在 |
| 400 | 路径非法 / 内容校验失败（manifest 或 schema 格式错误） |
| 413 | 内容过大 |
| 500 | 文件写入失败（临时文件创建或 rename 失败） |

---

#### 3.1.6 `POST /api/services/{id}/files/content?path=` — 创建 Service 新文件

在 Service 目录下创建新文件（用于多文件服务场景）。

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | string | ✅ | 文件相对路径 |

**请求体**：

```json
{
  "content": "export function helper() { return 42; }\n"
}
```

**响应**（201 Created）：

```json
{
  "path": "lib/helper.js",
  "size": 42,
  "modifiedAt": "2026-02-12T10:40:00Z"
}
```

**错误码**：

| 状态码 | 场景 |
|--------|------|
| 409 | 文件已存在 |
| 400 | 路径非法 |

---

#### 3.1.7 `DELETE /api/services/{id}/files/content?path=` — 删除 Service 文件

删除 Service 目录下的指定文件。

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | string | ✅ | 文件相对路径 |

**限制**：不允许删除 `manifest.json`、`index.js`、`config.schema.json` 三个核心文件。

**响应**（204 No Content）

---

#### 3.1.8 `POST /api/services/{id}/validate-schema` — 校验 Schema

校验一份 config schema JSON 是否合法（不写入文件，仅做格式校验）。

**请求体**：

```json
{
  "schema": {
    "port": {
      "type": "int",
      "required": true,
      "default": 8080
    }
  }
}
```

**响应**（200 OK）：

```json
{
  "valid": true,
  "fields": [
    {
      "name": "port",
      "type": "int",
      "required": true,
      "defaultValue": 8080,
      "description": ""
    }
  ]
}
```

或校验失败时：

```json
{
  "valid": false,
  "error": "unknown field type \"datetime\" for field \"createdAt\""
}
```

**后端实现要点**：

- 调用 `service_config_schema.cpp` 中 `fromJsonFile()` 所使用的内部带错误检查的 `parseObject()` 逻辑进行解析校验
- 注意：`fromJsObject()` **不适合**作为校验入口，因为它不返回错误信息，不检测未知类型
- 建议为 `ServiceConfigSchema` 新增一个 public 静态方法（如 `fromJsonObject(const QJsonObject& obj, QString& error)`），复用 `parseObject()` 的校验逻辑
- 当前代码已支持的类型别名包括 `integer`（→ `int`）、`number`（→ `double`）、`boolean`（→ `bool`），校验时不应将这些视为非法
- 返回解析后的结构化 FieldMeta（帮助前端确认解析结果与预期一致）

---

### 3.2 Project 管理 API（扩展）

#### 3.2.1 `POST /api/services/{id}/generate-defaults` — 生成默认配置

根据 Service 的 config schema 生成一份填好默认值的配置 JSON。

**响应**（200 OK）：

```json
{
  "serviceId": "my-service",
  "config": {
    "port": 8080,
    "debug": false,
    "ratio": 0.5
  },
  "requiredFields": ["name", "port"],
  "optionalFields": ["ratio", "debug", "metadata"]
}
```

**后端实现要点**：

- 遍历 schema 的每个 FieldMeta
- 有 `defaultValue` 的字段填入默认值
- 返回 `requiredFields` 和 `optionalFields` 列表，帮助前端标记表单必填项

---

#### 3.2.2 `POST /api/services/{id}/validate-config` — 对 Service 校验配置（创建前预校验）

在创建 Project 之前，针对指定 Service 的 schema 校验一份配置。

**请求体**：

```json
{
  "config": {
    "port": 8080,
    "name": "test"
  }
}
```

**响应**（200 OK）：

```json
{
  "valid": true
}
```

或：

```json
{
  "valid": false,
  "errors": [
    {"field": "name", "message": "required field missing"},
    {"field": "port", "message": "value 99999 exceeds maximum 65535"}
  ]
}
```

**与现有 `POST /api/projects/{id}/validate` 的区别**：

- 现有接口要求 Project 已存在，本接口面向 Project 创建前的预校验
- 本接口按 Service ID 路由，无需 Project ID

**后端实现要点**：

- 调用 `ServiceConfigValidator::validate()` 针对指定 Service 的 schema 校验
- **⚠️ 当前限制**：`ServiceConfigValidator::validate()` 采用 fail-fast 策略，遇到第一个错误即返回 `ValidationResult`（单个 `errorField` + `errorMessage`）。要实现上述多字段错误列表，需要新增 `validateAll()` 方法，遍历所有字段收集全部错误后一次性返回
- **阶段性方案**：第一阶段可先返回单一错误（与现有 `POST /api/projects/{id}/validate` 行为一致），后续再增强为多字段错误列表。响应格式保持向前兼容——`errors` 数组长度为 1 即可

---

#### 3.2.3 `GET /api/projects?serviceId=&status=&enabled=&page=&pageSize=` — 增强列表查询

（现有 `GET /api/projects` 的增强版，已列入 todolist P0）

**查询参数**：

| 参数 | 类型 | 说明 |
|------|------|------|
| `serviceId` | string | 按 Service ID 过滤 |
| `status` | string | 按状态过滤：`running`/`stopped`/`invalid` |
| `enabled` | bool | 按启用状态过滤 |
| `page` | int | 页码（从 1 开始，默认 1） |
| `pageSize` | int | 每页数量（默认 20，最大 100） |

**响应**（200 OK）：

```json
{
  "projects": [...],
  "total": 42,
  "page": 1,
  "pageSize": 20
}
```

---

#### 3.2.4 `PATCH /api/projects/{id}/enabled` — 切换启用状态

（已列入 todolist P0）

**请求体**：

```json
{
  "enabled": false
}
```

**响应**（200 OK）：返回更新后的 Project JSON

**后端实现要点**：

- 仅修改 `enabled` 字段，写入文件
- 如 `enabled: false`，停止该 Project 的调度
- 如 `enabled: true`，恢复调度

---

#### 3.2.5 `GET /api/projects/{id}/logs?lines=N` — Project 级日志

（已列入 todolist P0）

与 Instance 日志 API 类似，但直接通过 Project ID 访问，适用于 Instance 已退出但日志文件仍在的场景。

**查询参数**：

| 参数 | 类型 | 说明 |
|------|------|------|
| `lines` | int | 返回最后 N 行（默认 100，范围 1-5000） |

**响应**（200 OK）：

```json
{
  "projectId": "silo-a",
  "lines": ["line1", "line2", "..."]
}
```

---

#### 3.2.6 `GET /api/projects/runtime` — 批量运行态查询

（已列入 todolist P1，对 Dashboard 至关重要，提升至 P0）

**查询参数**：

| 参数 | 类型 | 说明 |
|------|------|------|
| `ids` | string | 逗号分隔的 Project ID 列表（为空则返回所有） |

**响应**（200 OK）：

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

---

### 3.3 Driver 管理 API（扩展）

#### 3.3.1 `GET /api/drivers/{id}` — Driver 详情

（已列入 todolist P1，DriverLab Web 依赖此接口，提升至 P0）

返回 Driver 的完整信息，包含完整 DriverMeta。

**响应**（200 OK）：

```json
{
  "id": "driver_modbustcp",
  "program": "/path/to/driver_modbustcp",
  "metaHash": "abc123...",
  "meta": {
    "schemaVersion": "1.0",
    "info": {
      "id": "driver_modbustcp",
      "name": "Modbus TCP Driver",
      "version": "1.0.0",
      "description": "...",
      "vendor": "...",
      "capabilities": ["read", "write"],
      "profiles": ["oneshot", "keepalive"]
    },
    "config": {
      "fields": [...],
      "apply": { "method": "startupArgs" }
    },
    "commands": [
      {
        "name": "read_register",
        "description": "Read holding register",
        "title": "Read Register",
        "params": [
          {
            "name": "address",
            "type": "int",
            "required": true,
            "description": "Register address",
            "constraints": { "min": 0, "max": 65535 }
          },
          {
            "name": "count",
            "type": "int",
            "required": false,
            "defaultValue": 1,
            "description": "Number of registers"
          }
        ],
        "returns": {
          "type": "object",
          "fields": [...]
        },
        "events": [...],
        "errors": [...],
        "examples": [...]
      }
    ],
    "types": {},
    "errors": [],
    "examples": []
  }
}
```

---

#### 3.3.2 `WS /api/driverlab/{driverId}` — DriverLab WebSocket

通过 WebSocket 连接启动 Driver 测试会话。连接的生命周期即 Driver 进程的生命周期。

> ✅ 已确认：Qt 6.8+ 的 `QAbstractHttpServer::addWebSocketUpgradeVerifier()` 原生支持 WebSocket 升级，与 HTTP 共享同一端口。当前项目 Qt 6.10.0 满足要求。无需独立 `QWebSocketServer`。

**连接参数**（查询参数）：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `runMode` | string | ❌ | `oneshot`（默认）或 `keepalive` |
| `args` | string | ❌ | 额外启动参数，逗号分隔 |

**连接示例**：

```
ws://127.0.0.1:8080/api/driverlab/driver_modbustcp?runMode=keepalive
```

**连接建立流程**：

1. 客户端发起 WebSocket 握手
2. 服务端校验 `driverId` 是否存在于 `DriverCatalog`
3. 服务端拉起 Driver 子进程（`QProcess`）
4. 服务端 `queryMeta()`，将 Meta 作为第一条消息推送给客户端
5. 进入双向通信状态

**生命周期绑定规则**：

| 事件 | KeepAlive 模式 | OneShot 模式 |
|------|---------------|-------------|
| WebSocket 断开（关标签页 / 导航离开 / 网络中断） | 立即 `terminate()` + `kill()` Driver 进程 | 同左 |
| Driver 进程正常退出 | 推送 `driver.exited`，关闭 WebSocket | 推送 `driver.exited`，**不关闭 WebSocket**（等待下一条命令时自动重启） |
| Driver 进程崩溃 / 被终止 | 推送 `driver.exited`，关闭 WebSocket | 推送 `driver.exited`，**不关闭 WebSocket**（下一条命令触发重启） |
| 服务端 shutdown | 终止所有 Driver 进程，关闭所有 WebSocket | 同左 |

**OneShot 模式特殊说明**：OneShot Driver 的设计语义是"执行一条命令后退出"。因此 Driver 进程退出是 OneShot 模式的正常行为，不应导致 WebSocket 断开。WebSocket 连接代表的是"测试会话"而非"Driver 进程"。当用户发送下一条命令时，服务端自动重启 Driver 并推送 `driver.restarted` 通知。只有当用户主动关闭页面（WebSocket 断开）时，测试会话才结束。

**资源限制**：

- 全局最大同时 WebSocket 连接数（建议 10）
- 超限拒绝握手，返回 HTTP 429

---

##### 下行消息（服务端 → 客户端）

所有下行消息统一为 JSON，包含 `type` 字段区分类型：

**1. Meta 推送**（连接建立后的第一条消息）：

```json
{
  "type": "meta",
  "driverId": "driver_modbustcp",
  "pid": 12345,
  "runMode": "keepalive",
  "meta": {
    "schemaVersion": "1.0",
    "info": { "name": "Modbus TCP Driver", "version": "1.0.0", ... },
    "commands": [ ... ],
    ...
  }
}
```

**2. Driver stdout 转发**（原样转发 Driver 输出的每一行 JSONL）：

```json
{
  "type": "stdout",
  "message": {
    "ok": { "registers": [100, 200, 300] }
  }
}
```

```json
{
  "type": "stdout",
  "message": {
    "event": { "progress": 50 }
  }
}
```

```json
{
  "type": "stdout",
  "message": {
    "error": { "code": -1, "message": "connection refused" }
  }
}
```

**3. Driver 状态通知**：

```json
{
  "type": "driver.started",
  "pid": 12345
}
```

```json
{
  "type": "driver.exited",
  "exitCode": 0,
  "exitStatus": "normal",
  "reason": "process finished"
}
```

```json
{
  "type": "driver.restarted",
  "pid": 12346,
  "reason": "oneshot mode: new command after previous exit"
}
```

**4. 错误通知**：

```json
{
  "type": "error",
  "message": "driver failed to start: permission denied"
}
```

---

##### 上行消息（客户端 → 服务端）

**1. 执行命令**：

```json
{
  "type": "exec",
  "cmd": "read_register",
  "data": {
    "address": 100,
    "count": 10
  }
}
```

服务端将 `{"cmd":"read_register","data":{"address":100,"count":10}}` 写入 Driver stdin。

OneShot 模式下，如果 Driver 已退出，服务端自动重启 Driver 后再发送命令（对应桌面端 `DriverSession::executeCommand()` 的重启逻辑），并推送 `driver.restarted` 通知。

**2. 终止当前命令**（可选）：

```json
{
  "type": "cancel"
}
```

服务端关闭 Driver 的 stdin write channel 或发送中断信号。

---

##### 连接错误处理

| 场景 | 行为 |
|------|------|
| `driverId` 不存在 | 拒绝握手，HTTP 404 |
| 连接数已满 | 拒绝握手，HTTP 429 |
| Driver 启动失败 | 握手成功后推送 `error` 消息，关闭 WebSocket |
| Meta 查询失败 | 推送 `error` 消息（不中断连接，Driver 仍可用） |
| 客户端发送非法 JSON | 推送 `error` 消息（不中断连接） |
| 客户端发送未知 type | 推送 `error` 消息（不中断连接） |

---

##### 前端 Shell 渲染建议

WebSocket 消息流天然适合 Shell 风格的交互界面：

- `type: "stdout"` → 渲染为终端输出行（根据 `ok`/`event`/`error` 分色显示）
- `type: "driver.started/exited/restarted"` → 渲染为系统提示行（灰色）
- `type: "error"` → 渲染为红色错误行
- 上行 `exec` 命令 → 渲染为用户输入行（带 `$` 提示符）
- 保留完整的消息历史在前端内存中（作为 Shell 滚动缓冲区）

---

### 3.4 Instance 与进程监控 API（扩展）

#### 3.4.1 `GET /api/instances/{id}` — Instance 详情

（已列入 todolist P0）

**响应**（200 OK）：

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

**扩展字段**（相比现有 `instanceToJson`）：

| 字段 | 说明 |
|------|------|
| `workingDirectory` | 工作目录路径 |
| `logPath` | 日志文件路径 |
| `commandLine` | 完整命令行参数列表 |

---

#### 3.4.2 `GET /api/instances/{id}/process-tree` — 进程树

以 Instance 主进程为根，返回完整的进程树。

**响应**（200 OK）：

```json
{
  "instanceId": "inst_abc12345",
  "rootPid": 12345,
  "tree": {
    "pid": 12345,
    "name": "stdiolink_service",
    "commandLine": "stdiolink_service /path/to/service --config-file=...",
    "status": "running",
    "startedAt": "2026-02-12T10:00:00Z",
    "resources": {
      "cpuPercent": 2.5,
      "memoryRssBytes": 52428800,
      "memoryVmsBytes": 134217728,
      "threadCount": 8,
      "uptimeSeconds": 3600
    },
    "children": [
      {
        "pid": 12346,
        "name": "driver_modbustcp",
        "commandLine": "driver_modbustcp --profile=keepalive",
        "status": "running",
        "startedAt": "2026-02-12T10:00:01Z",
        "resources": {
          "cpuPercent": 0.8,
          "memoryRssBytes": 16777216,
          "memoryVmsBytes": 67108864,
          "threadCount": 3,
          "uptimeSeconds": 3599
        },
        "children": []
      },
      {
        "pid": 12347,
        "name": "driver_3dvision",
        "commandLine": "driver_3dvision --cmd=capture",
        "status": "running",
        "startedAt": "2026-02-12T10:00:02Z",
        "resources": {
          "cpuPercent": 15.2,
          "memoryRssBytes": 268435456,
          "memoryVmsBytes": 536870912,
          "threadCount": 12,
          "uptimeSeconds": 3598
        },
        "children": []
      }
    ]
  },
  "summary": {
    "totalProcesses": 3,
    "totalCpuPercent": 18.5,
    "totalMemoryRssBytes": 337641472,
    "totalThreads": 23
  }
}
```

**进程节点字段**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `pid` | int | 进程 ID |
| `name` | string | 进程名 |
| `commandLine` | string | 完整命令行 |
| `status` | string | `running`/`sleeping`/`zombie`/`stopped` |
| `startedAt` | string | 进程启动时间（ISO 格式） |
| `resources.cpuPercent` | double | CPU 使用率（0-100%，多核可能超过 100） |
| `resources.memoryRssBytes` | int64 | 常驻内存（RSS，字节） |
| `resources.memoryVmsBytes` | int64 | 虚拟内存（VMS，字节） |
| `resources.threadCount` | int | 线程数 |
| `resources.uptimeSeconds` | int64 | 运行时长（秒） |

**`summary` 汇总**：整棵树所有进程的聚合统计。

**后端实现要点**：

- **Linux**：读取 `/proc/{pid}/stat`、`/proc/{pid}/status`、`/proc/{pid}/cmdline`；通过 `/proc/{ppid}/task/{tid}/children` 或遍历 `/proc/` 查找子进程
- **Windows**：使用 `CreateToolhelp32Snapshot` + `Process32First/Next` 遍历进程，`GetProcessMemoryInfo` 获取内存，`GetProcessTimes` + `NtQuerySystemInformation` 计算 CPU
- **macOS**：使用 `proc_pidinfo` / `proc_listchildpids` 或 `sysctl` 方案
- 建议新增 `ProcessTreeCollector` 工具类，封装跨平台实现
- CPU 使用率需两次采样计算差值（间隔约 500ms），首次调用可能返回 0

---

#### 3.4.3 `GET /api/instances/{id}/resources` — 实时资源数据

获取 Instance 主进程及其所有子进程的实时资源占用快照。

**数据获取方式**：本接口为纯轮询（pull）模式，前端按需调用。不提供服务端推送。

**CPU 使用率说明**：CPU 使用率需要两次采样计算差值。服务端在收到请求时进行一次采样，与上次采样（或进程启动时间）计算差值。首次调用某个 Instance 的资源接口时，CPU 使用率可能返回 0（无历史采样基线）。建议前端以 2-5 秒间隔轮询，兼顾数据时效性和服务端开销。

**查询参数**：

| 参数 | 类型 | 说明 |
|------|------|------|
| `includeChildren` | bool | 是否包含子进程（默认 true） |

**响应**（200 OK）：

```json
{
  "instanceId": "inst_abc12345",
  "timestamp": "2026-02-12T10:30:00Z",
  "processes": [
    {
      "pid": 12345,
      "name": "stdiolink_service",
      "cpuPercent": 2.5,
      "memoryRssBytes": 52428800,
      "memoryVmsBytes": 134217728,
      "threadCount": 8,
      "uptimeSeconds": 3600,
      "ioReadBytes": 1048576,
      "ioWriteBytes": 524288
    },
    {
      "pid": 12346,
      "name": "driver_modbustcp",
      "cpuPercent": 0.8,
      "memoryRssBytes": 16777216,
      "memoryVmsBytes": 67108864,
      "threadCount": 3,
      "uptimeSeconds": 3599,
      "ioReadBytes": 204800,
      "ioWriteBytes": 102400
    }
  ],
  "summary": {
    "totalProcesses": 2,
    "totalCpuPercent": 3.3,
    "totalMemoryRssBytes": 69206016,
    "totalThreads": 11
  }
}
```

**扩展字段**（相比进程树）：

| 字段 | 说明 |
|------|------|
| `ioReadBytes` | 累计 I/O 读取字节数 |
| `ioWriteBytes` | 累计 I/O 写入字节数 |

---

### 3.5 Dashboard 与系统 API

#### 3.5.1 `GET /api/server/status` — Server 状态总览

（已列入 todolist P0）

**响应**（200 OK）：

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
    "drivers": 3,
    "driverlabConnections": 1
  },
  "system": {
    "platform": "linux",
    "cpuCores": 8,
    "totalMemoryBytes": 17179869184,
    "serverCpuPercent": 1.2,
    "serverMemoryRssBytes": 33554432
  }
}
```

**后端实现要点**：

- `uptimeMs`：记录 `ServerManager::initialize()` 时的时间戳，运行时计算差值
- `counts`：遍历内存中的 services/projects/instances/drivers 即可
- `system.platform`：`QSysInfo::productType()` + `QSysInfo::currentCpuArchitecture()`
- `system.serverCpuPercent/serverMemoryRssBytes`：采集当前进程自身的资源占用

---

#### 3.5.2 `GET /api/events/stream` — 实时事件流（SSE）

（已列入 todolist P1）

通过 Server-Sent Events 推送系统事件。

**事件类型**：

| 事件名 | 触发条件 | 数据 |
|--------|----------|------|
| `instance.started` | Instance 启动 | `{ instanceId, projectId, pid }` |
| `instance.finished` | Instance 退出 | `{ instanceId, projectId, exitCode, status }` |
| `project.status_changed` | Project 状态变更 | `{ projectId, oldStatus, newStatus }` |
| `service.scanned` | Service 扫描完成 | `{ added, removed, updated }` |
| `driver.scanned` | Driver 扫描完成 | `{ scanned, updated }` |
| `schedule.triggered` | 调度触发 | `{ projectId, scheduleType }` |
| `schedule.suppressed` | 调度被抑制 | `{ projectId, reason, consecutiveFailures }` |

**响应格式**（SSE）：

```
event: instance.started
data: {"instanceId":"inst_abc","projectId":"silo-a","pid":12345}

event: instance.finished
data: {"instanceId":"inst_abc","projectId":"silo-a","exitCode":0,"status":"stopped"}
```

**后端实现要点**：

- Qt HTTP Server 支持 SSE 需要使用 `QHttpServerResponder` 的流式写入模式
- 注册一个全局事件分发器 `EventBus`，各 Manager 的信号连接到 `EventBus`
- 每个 SSE 连接维护一个 `QHttpServerResponder*`，`EventBus` 广播时写入所有连接
- 支持 `?filter=instance,project` 参数按事件类型过滤
- 连接断开时自动清理

---

## 4. API 汇总表

### 4.1 现有 API（M40 已实现）

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/services` | Service 列表 |
| GET | `/api/services/{id}` | Service 详情（含 configSchema） |
| POST | `/api/services/scan` | 手动重扫 |
| GET | `/api/projects` | Project 列表 |
| POST | `/api/projects` | 创建 Project |
| GET | `/api/projects/{id}` | Project 详情 |
| PUT | `/api/projects/{id}` | 更新 Project |
| DELETE | `/api/projects/{id}` | 删除 Project |
| POST | `/api/projects/{id}/validate` | 校验 Project 配置 |
| POST | `/api/projects/{id}/start` | 启动 Project |
| POST | `/api/projects/{id}/stop` | 停止 Project |
| POST | `/api/projects/{id}/reload` | 重载 Project |
| GET | `/api/projects/{id}/runtime` | 运行态详情 |
| GET | `/api/instances` | Instance 列表 |
| POST | `/api/instances/{id}/terminate` | 终止 Instance |
| GET | `/api/instances/{id}/logs` | Instance 日志 |
| GET | `/api/drivers` | Driver 列表 |
| POST | `/api/drivers/scan` | 重扫 Driver |

### 4.2 新增 API

| 优先级 | 方法 | 路径 | 功能域 | 说明 |
|--------|------|------|--------|------|
| **P0** | POST | `/api/services` | Service 创建 | 创建新 Service |
| **P0** | DELETE | `/api/services/{id}` | Service 创建 | 删除 Service |
| **P0** | GET | `/api/services/{id}/files` | Service 创建 | 文件列表 |
| **P0** | GET | `/api/services/{id}/files/content?path=` | Service 创建 | 读取文件 |
| **P0** | PUT | `/api/services/{id}/files/content?path=` | Service 创建 | 更新文件 |
| **P0** | POST | `/api/services/{id}/files/content?path=` | Service 创建 | 创建新文件 |
| **P0** | DELETE | `/api/services/{id}/files/content?path=` | Service 创建 | 删除文件 |
| **P0** | POST | `/api/services/{id}/validate-schema` | Service 创建 | 校验 Schema |
| **P0** | POST | `/api/services/{id}/generate-defaults` | Project 创建 | 生成默认配置 |
| **P0** | POST | `/api/services/{id}/validate-config` | Project 创建 | 创建前预校验 |
| **P0** | GET | `/api/projects` (增强) | Project 创建 | 过滤 + 分页 |
| **P0** | PATCH | `/api/projects/{id}/enabled` | Project 创建 | 启停开关 |
| **P0** | GET | `/api/projects/{id}/logs` | Project 管理 | Project 级日志 |
| **P0** | GET | `/api/drivers/{id}` | DriverLab | Driver 完整详情 |
| **P0** | GET | `/api/server/status` | Dashboard | 系统状态 |
| **P0** | GET | `/api/instances/{id}` | Instance 管理 | Instance 详情 |
| **P0** | WS | `/api/driverlab/{driverId}` | DriverLab | WebSocket 测试会话（生命周期绑定） |
| **P0** | GET | `/api/instances/{id}/process-tree` | 进程监控 | 进程树 |
| **P0** | GET | `/api/instances/{id}/resources` | 进程监控 | 资源占用 |
| **P0** | GET | `/api/projects/runtime` | Dashboard | 批量运行态 |
| **P1** | GET | `/api/events/stream` | Dashboard | SSE 事件流 |

---

## 5. 后端实现建议

### 5.1 新增模块结构

```
src/stdiolink_server/
├── http/
│   ├── api_router.cpp          ← 扩展已有路由注册
│   ├── api_router.h
│   ├── http_helpers.h
│   ├── service_file_handler.*  ← 新增: Service 文件操作 (3.1.3-3.1.7)
│   ├── driverlab_ws_handler.*  ← 新增: DriverLab WebSocket 处理 (3.3.2)，基于 QHttpServer 原生升级
│   └── event_stream.*         ← 新增: SSE 事件流管理 (3.5.2)
├── manager/
│   └── process_monitor.*      ← 新增: 进程树 & 资源采集 (3.4.2-3.4.3)
├── model/
│   └── process_info.*         ← 新增: 进程信息模型
└── ...
```

### 5.2 DriverLab WebSocket Handler 设计

基于 Qt 6.8+ 原生 WebSocket 升级支持（`addWebSocketUpgradeVerifier` + `nextPendingWebSocketConnection`），无需独立 `QWebSocketServer`。

```cpp
/// 每个 WebSocket 连接对应一个 DriverLabWsConnection 实例。
/// 连接断开即销毁 Driver 进程，无需 session 表、idle timer。
class DriverLabWsConnection : public QObject {
    Q_OBJECT
public:
    DriverLabWsConnection(std::unique_ptr<QWebSocket> socket,
                          const stdiolink::DriverConfig& driverConfig,
                          const QString& runMode,
                          const QStringList& extraArgs,
                          QObject* parent = nullptr);
    ~DriverLabWsConnection(); // 析构时 terminate + kill Driver

private slots:
    void onTextMessageReceived(const QString& message);
    void onSocketDisconnected();
    void onDriverStdoutReady();
    void onDriverFinished(int exitCode, QProcess::ExitStatus status);

private:
    void startDriver();
    void sendJson(const QJsonObject& msg);
    void forwardStdoutLine(const QByteArray& line);
    void restartDriverForOneShot();  // OneShot 模式下自动重启

    std::unique_ptr<QWebSocket> m_socket;  // 持有所有权（从 nextPendingWebSocketConnection 获取）
    std::unique_ptr<QProcess> m_process;
    stdiolink::DriverConfig m_driverConfig;
    QString m_runMode;                // "oneshot" | "keepalive"
    QStringList m_extraArgs;
};

/// 全局 WebSocket 管理器，注册 verifier 并管理连接。
class DriverLabWsHandler : public QObject {
    Q_OBJECT
public:
    explicit DriverLabWsHandler(stdiolink::DriverCatalog* catalog,
                                QObject* parent = nullptr);

    /// 在 QHttpServer 上注册 WebSocket 升级验证器
    void registerVerifier(QHttpServer& server);

    int activeConnectionCount() const;
    static constexpr int kMaxConnections = 10;

private slots:
    void onNewWebSocketConnection();

private:
    /// verifier 回调：解析 URL path 提取 driverId，校验存在性和连接数
    QHttpServerWebSocketUpgradeResponse verifyUpgrade(const QHttpServerRequest& request);

    stdiolink::DriverCatalog* m_catalog;
    QHttpServer* m_server;  // 非持有，用于 nextPendingWebSocketConnection
    QVector<DriverLabWsConnection*> m_connections;
};
```

**注册流程**（在 `ServerManager::initialize()` 中）：

```cpp
// 1. 注册 WebSocket 升级验证器
m_driverLabWsHandler->registerVerifier(server);

// registerVerifier 内部实现：
void DriverLabWsHandler::registerVerifier(QHttpServer& server) {
    m_server = &server;
    server.addWebSocketUpgradeVerifier(
        this, [this](const QHttpServerRequest& request) {
            return verifyUpgrade(request);
        });
    connect(&server, &QHttpServer::newWebSocketConnection,
            this, &DriverLabWsHandler::onNewWebSocketConnection);
}

// 2. verifier 回调：
QHttpServerWebSocketUpgradeResponse
DriverLabWsHandler::verifyUpgrade(const QHttpServerRequest& request) {
    // 仅处理 /api/driverlab/* 路径
    const QString path = request.url().path();
    if (!path.startsWith("/api/driverlab/"))
        return QHttpServerWebSocketUpgradeResponse::passToNext();

    // 提取 driverId
    const QString driverId = path.mid(QString("/api/driverlab/").size());
    if (!m_catalog->hasDriver(driverId))
        return QHttpServerWebSocketUpgradeResponse::deny(404, "driver not found");

    if (m_connections.size() >= kMaxConnections)
        return QHttpServerWebSocketUpgradeResponse::deny(429, "too many connections");

    return QHttpServerWebSocketUpgradeResponse::accept();
}

// 3. 连接建立后：
void DriverLabWsHandler::onNewWebSocketConnection() {
    auto socket = m_server->nextPendingWebSocketConnection();
    // ... 创建 DriverLabWsConnection，拉起 Driver 进程
}
```

**与 REST Session 方案的对比**：

| 维度 | REST Session（已废弃） | WebSocket 生命周期绑定 |
|------|----------------------|---------------------|
| 代码量 | 7 个 API + session 表 + idle timer + execution history | 1 个 WS 端点 + 连接对象 |
| 生命周期管理 | 手动（idle timeout 兜底） | 自动（TCP 连接状态） |
| 实时性 | 轮询 | 原生推送 |
| 资源泄漏风险 | 中（timeout 可能不够及时） | 无（连接断开即释放） |

### 5.3 ProcessMonitor 设计

```cpp
struct ProcessInfo {
    qint64 pid;
    qint64 parentPid;
    QString name;
    QString commandLine;
    QString status;
    QDateTime startedAt;

    double cpuPercent;
    qint64 memoryRssBytes;
    qint64 memoryVmsBytes;
    int threadCount;
    qint64 uptimeSeconds;
    qint64 ioReadBytes;
    qint64 ioWriteBytes;
};

struct ProcessTreeNode {
    ProcessInfo info;
    QVector<ProcessTreeNode> children;
};

class ProcessMonitor {
public:
    // 获取进程的完整子进程树
    static ProcessTreeNode getProcessTree(qint64 rootPid);

    // 获取单个进程的资源信息
    static ProcessInfo getProcessInfo(qint64 pid);

    // 获取进程及其所有后代的平坦列表
    static QVector<ProcessInfo> getProcessFamily(qint64 rootPid);

private:
    // 平台相关实现
#ifdef Q_OS_LINUX
    static ProcessInfo readProcFs(qint64 pid);
    static QVector<qint64> getChildPids(qint64 pid);
#endif
#ifdef Q_OS_WIN
    static ProcessInfo readWinProcess(qint64 pid);
    static QVector<qint64> getChildPids(qint64 pid);
#endif
#ifdef Q_OS_MACOS
    static ProcessInfo readMacProcess(qint64 pid);
    static QVector<qint64> getChildPids(qint64 pid);
#endif
};
```

### 5.4 Service 文件操作安全

`ServiceFileHandler` 核心安全措施：

```cpp
class ServiceFileHandler {
public:
    // 路径安全校验 — 防止路径穿越
    static bool isPathSafe(const QString& serviceDir, const QString& relativePath);

    // 实现: 将 relativePath 与 serviceDir 拼接后规范化
    // 确保结果路径仍以 serviceDir 开头
    static QString resolveSafePath(const QString& serviceDir,
                                   const QString& relativePath,
                                   QString& error);

    static constexpr qint64 kMaxFileSize = 1 * 1024 * 1024; // 1MB
};

bool ServiceFileHandler::isPathSafe(const QString& serviceDir,
                                     const QString& relativePath) {
    // 禁止: "..", 绝对路径, 空路径
    if (relativePath.isEmpty() || relativePath.contains(".."))
        return false;
    if (QDir::isAbsolutePath(relativePath))
        return false;

    // 注意：不能使用 canonicalFilePath()，因为它对不存在的文件返回空字符串，
    // 会导致新文件创建场景误判。
    // 正确做法：使用 QDir::cleanPath() + absoluteFilePath() 并检查前缀。
    const QString basePath = QDir::cleanPath(QDir(serviceDir).absolutePath());
    const QString resolved = QDir::cleanPath(
        QDir(serviceDir).absoluteFilePath(relativePath));
    return resolved.startsWith(basePath + "/");
}
```

### 5.5 实现优先级路线图

**阶段一（P0 — 全部 WebUI 需求落地）**：

分三批实施，每批内部无强依赖，批次之间有前置关系：

**批次 A — 基础设施与只读 API（无新依赖，低风险）**：

1. CORS 中间件（所有后续 API 的前置条件）
2. `GET /api/server/status` — Dashboard 首页
3. `GET /api/instances/{id}` — Instance 详情
4. `GET /api/drivers/{id}` — Driver 详情（含完整 meta）
5. `GET /api/projects` 增强（过滤 + 分页）
6. `GET /api/projects/{id}/logs` — Project 级日志
7. `GET /api/projects/runtime` — 批量运行态
8. `PATCH /api/projects/{id}/enabled` — 启停开关

**批次 B — Service 创建与文件操作（依赖路径安全校验、原子写入机制）**：

9. `POST /api/services` — Service 创建
10. `DELETE /api/services/{id}` — Service 删除
11. Service 文件操作系列（3.1.3-3.1.7）— JS 编辑器 & Schema 编辑
12. `POST /api/services/{id}/validate-schema` — Schema 校验
13. `POST /api/services/{id}/generate-defaults` — 默认配置生成
14. `POST /api/services/{id}/validate-config` — 配置预校验

**批次 C — 高复杂度功能（依赖技术预研完成）**：

15. `WS /api/driverlab/{driverId}` — DriverLab WebSocket 测试会话（调研项 0.1 核心方案已确认，仍需验证断开可靠性和并发上限；依赖调研项 0.2 跨域验证）
16. `GET /api/instances/{id}/process-tree` — 进程树（依赖调研项 0.4）
17. `GET /api/instances/{id}/resources` — 资源监控（依赖调研项 0.4）

**阶段二（P1 — 实时能力与运维增强）**：

1. `GET /api/events/stream` — SSE 事件流（依赖调研项 0.5）

---

## 6. 变更记录

| 日期 | 版本 | 变更说明 |
|------|------|---------|
| 2026-02-12 | 1.0.0 | 初版，覆盖四大需求域 + Dashboard |
| 2026-02-12 | 1.1.0 | 实现一致性修订：①全部需求提升至 P0（DriverLab、进程树、资源监控），SSE 移至 P1；②文件操作 API 路由从 `{path}` 改为 `?path=` 查询参数，规避 Qt 路由多段参数问题；③validate-schema 实现建议改为复用 `parseObject()` 校验逻辑，不再引用无错误检查的 `fromJsObject()`，修正 `integer` 合法别名的错误示例；④Service 创建后加载方式改为 `scan()` 或新增 `loadSingle()` public 方法，不再引用 private `loadService()`；⑤路径安全校验从 `canonicalFilePath()` 改为 `cleanPath()` + `absoluteFilePath()`，修复新文件创建时返回空路径的问题；⑥Server status 版本号修正为 `0.1.0` |
| 2026-02-12 | 1.2.0 | DriverLab 重设计：废弃 7 个 REST Session API（3.3.2-3.3.8），替换为 1 个 WebSocket 端点 `WS /api/driverlab/{driverId}`；核心原则——用 WebSocket 连接生命周期绑定 Driver 进程生命周期，页面离开即终止 Driver；定义上下行消息协议；后端从 DriverLabManager + session 表简化为 DriverLabWsHandler + per-connection 对象 |
| 2026-02-12 | 2.0.0 | 第一性原理审查修订（9 项）：①新增第 0 章「技术预演调研」，覆盖 6 项实现不确定性（QHttpServer+QWebSocketServer 并行、WS 跨域、CORS 中间件、跨平台进程树、SSE 支持、路径安全）；②新增第 2.6 节 CORS 需求，补全 HTTP CORS 头注入与 OPTIONS 预检处理；③Service schema 格式统一方案——`GET /api/services/{id}` 同时返回 `configSchema`（原始 key-value）和 `configSchemaFields`（FieldMeta 数组），前端只需对接一套格式；④Service 文件写入增加原子性保障（write-to-temp-then-rename）；⑤validate-config 响应标注当前后端 fail-fast 限制，给出阶段性兼容方案；⑥DriverLab 生命周期表按 OneShot/KeepAlive 分列，修正 OneShot 模式下 Driver 正常退出不应关闭 WebSocket 的语义；⑦WebSocket 端点增加 QHttpServer 兼容性风险标注；⑧资源监控接口补充轮询模式说明和 CPU 采样机制；⑨P0 路线图拆分为 A/B/C 三批次，标注批次间依赖和技术预研前置条件；⑩Service 创建 API 补全三套模板（empty/basic/driver_demo）的具体内容定义 |
| 2026-02-12 | 2.1.0 | WebSocket 方案确认：确认 Qt 6.8+ `QAbstractHttpServer::addWebSocketUpgradeVerifier()` 原生支持 WebSocket 升级（当前项目 Qt 6.10.0）；①调研项 0.1 标记为已解决——无需独立 `QWebSocketServer`，HTTP 与 WS 共享同一端口；②调研项 0.2 简化——跨域校验在 verifier 回调中处理 `Origin` 头；③3.3.2 节 ⚠️ 标记替换为 ✅ 已确认；④5.1 模块结构移除独立 `websocket/` 目录，`driverlab_ws_handler` 归入 `http/`；⑤5.2 Handler 设计重写——`DriverLabWsConnection` 持有 `std::unique_ptr<QWebSocket>`（从 `nextPendingWebSocketConnection` 获取），`DriverLabWsHandler::registerVerifier()` 注册路由级 verifier 回调，补充完整注册流程伪代码；⑥批次 C 依赖说明更新 |
