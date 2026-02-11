# HTTP API 参考

`stdiolink_server` 基于 Qt 官方 `QHttpServer` 提供 RESTful API，默认监听 `127.0.0.1:8080`。

## 通用约定

- 请求体和响应体均为 JSON（`Content-Type: application/json`）
- 成功响应：`200 OK`、`201 Created`、`204 No Content`
- 错误响应体格式：`{"error": "描述信息"}`
- 不存在的路径统一返回 `404 Not Found`

### 错误状态码

| 状态码 | 说明 |
|--------|------|
| `400 Bad Request` | 请求参数非法或验证失败 |
| `404 Not Found` | 资源不存在 |
| `409 Conflict` | 资源冲突（如重复启动） |
| `500 Internal Server Error` | 服务器内部错误 |

---

## Service API

### GET /api/services

列出所有已加载的 Service。

**响应示例：**

```json
{
  "services": [
    {
      "id": "data-collector",
      "name": "数据采集服务",
      "version": "1.0.0",
      "serviceDir": "/opt/data/services/data-collector",
      "hasSchema": true,
      "projectCount": 2
    }
  ]
}
```

### GET /api/services/{id}

获取单个 Service 的详情，包含完整的配置 Schema。

```bash
curl http://127.0.0.1:8080/api/services/data-collector
```

**响应示例：**

```json
{
  "id": "data-collector",
  "name": "数据采集服务",
  "version": "1.0.0",
  "serviceDir": "/opt/data/services/data-collector",
  "hasSchema": true,
  "configSchema": {
    "fields": [
      { "name": "device", "type": "object", "required": true, "fields": [...] }
    ]
  }
}
```

`configSchema` 返回的是 `config.schema.json` 的原始内容。

---

## Project API

### GET /api/projects

列出所有 Project。

```bash
curl http://127.0.0.1:8080/api/projects
```

**响应示例：**

```json
{
  "projects": [
    {
      "id": "silo-a",
      "name": "料仓A数据采集",
      "serviceId": "data-collector",
      "enabled": true,
      "valid": true,
      "schedule": { "type": "fixed_rate", "intervalMs": 5000, "maxConcurrent": 1 },
      "runningInstances": 1
    }
  ]
}
```

### GET /api/projects/{id}

获取单个 Project 的详情，包含完整配置和关联的 Instance 列表。

```bash
curl http://127.0.0.1:8080/api/projects/silo-a
```

### POST /api/projects

创建新 Project。请求体中必须包含 `id` 字段。

```bash
curl -X POST http://127.0.0.1:8080/api/projects \
  -H "Content-Type: application/json" \
  -d '{
    "id": "silo-b",
    "name": "料仓B数据采集",
    "serviceId": "data-collector",
    "enabled": true,
    "schedule": {"type": "manual"},
    "config": {"device": {"host": "192.168.1.101", "port": 502}}
  }'
```

| 状态码 | 说明 |
|--------|------|
| `201 Created` | 创建成功 |
| `400 Bad Request` | 参数非法或验证失败 |
| `409 Conflict` | ID 已存在 |

### PUT /api/projects/{id}

更新已有 Project。请求体中的 `id` 必须与路径参数一致。

```bash
curl -X PUT http://127.0.0.1:8080/api/projects/silo-b \
  -H "Content-Type: application/json" \
  -d '{
    "id": "silo-b",
    "name": "料仓B数据采集（更新）",
    "serviceId": "data-collector",
    "enabled": true,
    "schedule": {"type": "daemon"},
    "config": {"device": {"host": "192.168.1.101", "port": 502}}
  }'
```

| 状态码 | 说明 |
|--------|------|
| `200 OK` | 更新成功 |
| `400 Bad Request` | 参数非法 |
| `404 Not Found` | Project 不存在 |
| `409 Conflict` | 请求体 id 与路径不一致 |

### DELETE /api/projects/{id}

删除 Project 配置文件。

```bash
curl -X DELETE http://127.0.0.1:8080/api/projects/silo-b
```

| 状态码 | 说明 |
|--------|------|
| `204 No Content` | 删除成功 |
| `404 Not Found` | Project 不存在 |

---

## Project 操作 API

### POST /api/projects/{id}/validate

验证 Project 配置（不保存）。用于在创建或更新前预检。

```bash
curl -X POST http://127.0.0.1:8080/api/projects/silo-a/validate
```

**响应示例（验证通过）：**

```json
{ "valid": true }
```

**响应示例（验证失败）：**

```json
{ "valid": false, "error": "field 'device.host' is required" }
```

### POST /api/projects/{id}/start

启动 Project，创建新的 Instance。

```bash
curl -X POST http://127.0.0.1:8080/api/projects/silo-a/start
```

**响应示例：**

```json
{ "instanceId": "inst_a1b2c3d4", "pid": 12345 }
```

并发控制规则：

| 调度类型 | 已有运行实例时的行为 |
|----------|---------------------|
| `manual` | 返回 `409 Conflict`（同时只允许一个） |
| `fixed_rate` | 达到 `maxConcurrent` 时返回 `409 Conflict` |
| `daemon` | 已有实例时返回 `{"noop": true}`（幂等） |

| 状态码 | 说明 |
|--------|------|
| `200 OK` | 启动成功 |
| `400 Bad Request` | Project 无效或 Service 不存在 |
| `404 Not Found` | Project 不存在 |
| `409 Conflict` | 并发限制 |

### POST /api/projects/{id}/stop

停止 Project 的所有运行中 Instance。

```bash
curl -X POST http://127.0.0.1:8080/api/projects/silo-a/stop
```

### POST /api/projects/{id}/reload

重新从文件加载 Project 配置并重新验证。适用于直接修改了 `projects/{id}.json` 文件后的场景。

```bash
curl -X POST http://127.0.0.1:8080/api/projects/silo-a/reload
```

---

## Instance API

### GET /api/instances

列出所有运行中的 Instance。支持 `projectId` 查询参数筛选。

```bash
# 列出全部
curl http://127.0.0.1:8080/api/instances

# 按 Project 筛选
curl "http://127.0.0.1:8080/api/instances?projectId=silo-a"
```

**响应示例：**

```json
{
  "instances": [
    {
      "id": "inst_a1b2c3d4",
      "projectId": "silo-a",
      "serviceId": "data-collector",
      "pid": 12345,
      "status": "running",
      "startedAt": "2025-01-15T08:30:00Z"
    }
  ]
}
```

### POST /api/instances/{id}/terminate

终止指定的 Instance。

```bash
curl -X POST http://127.0.0.1:8080/api/instances/inst_a1b2c3d4/terminate
```

| 状态码 | 说明 |
|--------|------|
| `200 OK` | 终止信号已发送 |
| `404 Not Found` | Instance 不存在 |

### GET /api/instances/{id}/logs

获取 Instance 所属 Project 的日志内容。日志文件位于 `logs/{projectId}.log`。

```bash
curl http://127.0.0.1:8080/api/instances/inst_a1b2c3d4/logs
```

---

## Driver API

### GET /api/drivers

列出所有已发现的 Driver 及其元数据摘要。

```bash
curl http://127.0.0.1:8080/api/drivers
```

**响应示例：**

```json
{
  "drivers": [
    {
      "id": "driver_modbusrtu",
      "name": "Modbus RTU Driver",
      "version": "1.0.0",
      "program": "/opt/data/drivers/driver_modbusrtu/driver_modbusrtu",
      "commands": ["read_registers", "write_register"]
    }
  ]
}
```

### POST /api/drivers/scan

手动触发 Driver 目录重扫。会重新执行完整的扫描流程（含 meta 刷新）。

```bash
curl -X POST http://127.0.0.1:8080/api/drivers/scan
```

**响应示例：**

```json
{
  "scanned": 3,
  "updated": 2,
  "newlyFailed": 1,
  "skippedFailed": 0
}
```

---

## API 速览表

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/services` | 列出所有 Service |
| GET | `/api/services/{id}` | Service 详情（含 Schema） |
| GET | `/api/projects` | 列出所有 Project |
| POST | `/api/projects` | 创建 Project |
| GET | `/api/projects/{id}` | Project 详情 |
| PUT | `/api/projects/{id}` | 更新 Project |
| DELETE | `/api/projects/{id}` | 删除 Project |
| POST | `/api/projects/{id}/validate` | 验证配置 |
| POST | `/api/projects/{id}/start` | 启动 Project |
| POST | `/api/projects/{id}/stop` | 停止 Project |
| POST | `/api/projects/{id}/reload` | 重载配置 |
| GET | `/api/instances` | 列出运行中 Instance |
| POST | `/api/instances/{id}/terminate` | 终止 Instance |
| GET | `/api/instances/{id}/logs` | 查看日志 |
| GET | `/api/drivers` | 列出已发现 Driver |
| POST | `/api/drivers/scan` | 触发 Driver 重扫 |
