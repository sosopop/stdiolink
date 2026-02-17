# 错误码参考

stdiolink 在 Driver JSONL 协议、Host 运行时、Server HTTP API 中使用不同错误码体系。

## Driver JSONL 协议错误码

Driver 响应消息中 `code` 字段常见取值：

| 错误码 | 名称 | 说明 |
|--------|------|------|
| 0 | Success | 成功 |
| 400 | Bad Request | 请求参数错误（含自动参数校验失败） |
| 404 | Not Found | 命令不存在 |
| 500 | Internal Error | Driver 内部错误 |
| 501 | Meta Not Supported | Driver 未启用 meta 命令处理 |

### 参数校验说明

当前内置 `MetaValidator` 自动参数校验失败时，`DriverCore` 返回统一错误码 `400`，并在 payload 中提供详细错误文本（如缺字段、类型不匹配、约束不满足）。

## Host 运行时内部错误码

Host 与 Driver 通信过程中，`Task` 可能收到以下内部错误码：

| 错误码 | 说明 |
|--------|------|
| 1000 | Driver 输出的 JSONL 响应格式非法（invalid response） |
| 1001 | Driver 进程提前退出或请求写入失败 |
| 1002 | Driver stdout 缓冲区溢出（超过 8MB） |

说明：

- `1000/1001/1002` 是 Host 运行时错误，不是 Driver 业务命令的标准返回码
- 进程退出上下文可通过 `Driver::exitContext()` 获取

## Server HTTP API 错误码

`stdiolink_server` REST API 使用标准 HTTP 状态码：

| 状态码 | 名称 | 说明 |
|--------|------|------|
| 200 | OK | 请求成功 |
| 201 | Created | 资源创建成功 |
| 204 | No Content | 操作成功，无返回体（DELETE 等） |
| 400 | Bad Request | 请求参数错误或缺失 |
| 404 | Not Found | 资源不存在（Service/Project/Instance） |
| 409 | Conflict | 资源冲突（如项目已存在、实例已运行） |
| 413 | Payload Too Large | 请求体或文件内容超限 |
| 500 | Internal Server Error | 服务器内部错误 |

### 常见 API 错误场景

| 接口 | 错误码 | 场景 |
|------|--------|------|
| POST /api/projects | 400 | 缺少 serviceId 或 name |
| POST /api/projects | 404 | serviceId 对应的 Service 不存在 |
| POST /api/projects | 409 | project 已存在 |
| POST /api/projects/:id/start | 404 | Project 不存在 |
| POST /api/projects/:id/start | 409 | 实例已在运行或超过并发限制 |
| POST /api/projects/:id/stop | 404 | Project 不存在或无运行实例 |
| DELETE /api/services/:id | 409 | 存在依赖此 Service 的 Project |
| PUT /api/services/:id/files/* | 400 | 文件路径非法或内容字段错误 |
| PUT /api/services/:id/files/* | 413 | 文件内容超过限制 |
