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

获取单个 Service 的详情，包含完整的 Manifest、配置 Schema 和关联的 Project ID 列表。

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
  "manifest": {
    "manifestVersion": 1,
    "id": "data-collector",
    "name": "数据采集服务",
    "version": "1.0.0",
    "description": "工业数据采集",
    "author": "dev"
  },
  "configSchema": {
    "device": {
      "type": "object",
      "required": true,
      "fields": {
        "host": { "type": "string", "required": true },
        "port": { "type": "int", "default": 502 }
      }
    }
  },
  "projects": ["silo-a", "silo-b"]
}
```

**字段说明：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `manifest` | `object` | 完整的 `manifest.json` 内容 |
| `configSchema` | `object` | `config.schema.json` 原始内容 |
| `projects` | `string[]` | 关联此 Service 的 Project ID 列表 |

### POST /api/services/scan

手动触发 Service 目录重扫。重新扫描 `services/` 目录，发现新增/移除/更新的 Service，并可选地重新验证关联的 Project。

```bash
curl -X POST http://127.0.0.1:8080/api/services/scan
```

**请求体（可选）：**

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `revalidateProjects` | `boolean` | `true` | 重扫后重新验证关联 Project |
| `restartScheduling` | `boolean` | `true` | 重扫后重启调度引擎 |
| `stopInvalidProjects` | `boolean` | `false` | 停止变为无效的 Project 的运行实例 |

**响应示例：**

```json
{
  "scannedDirs": 5,
  "loadedServices": 3,
  "failedServices": 1,
  "added": 1,
  "removed": 0,
  "updated": 1,
  "unchanged": 1,
  "revalidatedProjects": 4,
  "becameValid": 0,
  "becameInvalid": 1,
  "remainedInvalid": 0,
  "schedulingRestarted": true,
  "invalidProjects": ["legacy-project"]
}
```

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
      "config": { "device": { "host": "192.168.1.100", "port": 502 } },
      "instanceCount": 1,
      "status": "running"
    }
  ]
}
```

**字段说明：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `instanceCount` | `number` | 当前运行中的 Instance 数量 |
| `status` | `string` | 状态：`running` / `stopped` / `invalid` |
| `error` | `string` | 验证失败时的错误信息（仅在无效时出现） |

### GET /api/projects/{id}

获取单个 Project 的详情，包含完整配置、关联的 Instance 列表和 Service 的配置 Schema。

```bash
curl http://127.0.0.1:8080/api/projects/silo-a
```

**响应示例：**

```json
{
  "id": "silo-a",
  "name": "料仓A数据采集",
  "serviceId": "data-collector",
  "enabled": true,
  "valid": true,
  "schedule": { "type": "fixed_rate", "intervalMs": 5000, "maxConcurrent": 1 },
  "config": { "device": { "host": "192.168.1.100", "port": 502 } },
  "instanceCount": 1,
  "status": "running",
  "instances": [
    {
      "id": "inst_a1b2c3d4",
      "projectId": "silo-a",
      "serviceId": "data-collector",
      "pid": 12345,
      "startedAt": "2025-01-15T08:30:00Z",
      "status": "running"
    }
  ],
  "configSchema": {
    "device": {
      "type": "object",
      "required": true,
      "fields": {
        "host": { "type": "string", "required": true },
        "port": { "type": "int", "default": 502 }
      }
    }
  }
}
```

与列表响应相比，详情响应额外包含：

| 字段 | 类型 | 说明 |
|------|------|------|
| `instances` | `Instance[]` | 关联的运行中 Instance 列表 |
| `configSchema` | `object` | 关联 Service 的 `config.schema.json` 内容 |

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

启动 Project，创建新的 Instance。启动前会自动恢复该 Project 的调度状态。

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

停止 Project 的所有运行中 Instance，并暂停该 Project 的调度。

```bash
curl -X POST http://127.0.0.1:8080/api/projects/silo-a/stop
```

**响应示例：**

```json
{ "stopped": true }
```

### POST /api/projects/{id}/reload

重新从文件加载 Project 配置并重新验证。适用于直接修改了 `projects/{id}.json` 文件后的场景。reload 会先停止当前运行的 Instance 和调度，然后重新加载配置并验证，最后重启调度引擎。

```bash
curl -X POST http://127.0.0.1:8080/api/projects/silo-a/reload
```

**响应：**

返回重载后的 Project 完整信息（与 `GET /api/projects` 列表项格式相同）。

| 状态码 | 说明 |
|--------|------|
| `200 OK` | 重载成功 |
| `400 Bad Request` | 文件解析失败或验证失败 |
| `404 Not Found` | Project 文件不存在 |

### GET /api/projects/{id}/runtime

获取 Project 的运行态信息，包含实例列表和调度引擎状态。

```bash
curl http://127.0.0.1:8080/api/projects/silo-a/runtime
```

**响应示例：**

```json
{
  "id": "silo-a",
  "enabled": true,
  "valid": true,
  "status": "running",
  "runningInstances": 1,
  "instances": [
    {
      "id": "inst_a1b2c3d4",
      "projectId": "silo-a",
      "serviceId": "data-collector",
      "pid": 12345,
      "startedAt": "2025-01-15T08:30:00Z",
      "status": "running"
    }
  ],
  "schedule": {
    "type": "daemon",
    "timerActive": false,
    "restartSuppressed": false,
    "consecutiveFailures": 0,
    "shuttingDown": false,
    "autoRestarting": true
  }
}
```

**schedule 字段说明：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `type` | `string` | 调度类型 |
| `timerActive` | `boolean` | 定时器是否活跃（fixed_rate） |
| `restartSuppressed` | `boolean` | 重启是否被抑制（连续失败过多） |
| `consecutiveFailures` | `number` | 连续失败次数 |
| `shuttingDown` | `boolean` | 是否正在关闭 |
| `autoRestarting` | `boolean` | 是否处于自动重启状态（daemon 模式） |

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

**响应示例：**

```json
{ "terminated": true }
```

| 状态码 | 说明 |
|--------|------|
| `200 OK` | 终止信号已发送 |
| `404 Not Found` | Instance 不存在 |

### GET /api/instances/{id}/logs

获取日志内容。路径参数支持 Instance ID 或 Project ID；当传入 Instance ID 时自动解析为对应的 Project ID。日志文件位于 `logs/{projectId}.log`。

```bash
# 通过 Instance ID 查看
curl http://127.0.0.1:8080/api/instances/inst_a1b2c3d4/logs

# 通过 Project ID 查看（Instance 退出后仍可查看历史日志）
curl http://127.0.0.1:8080/api/instances/silo-a/logs

# 指定返回行数
curl "http://127.0.0.1:8080/api/instances/silo-a/logs?lines=50"
```

**查询参数：**

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `lines` | `int` | `100` | 返回最后 N 行日志（范围 1–5000） |

**响应示例：**

```json
{
  "projectId": "silo-a",
  "lines": ["2025-01-15 08:30:00 [INFO] Service started", "..."]
}
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
      "program": "/opt/data/drivers/driver_modbusrtu/stdio.drv.modbusrtu",
      "metaHash": "a3f2b1...",
      "name": "Modbus RTU Driver",
      "version": "1.0.0"
    }
  ]
}
```

**字段说明：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | `string` | Driver 标识（通常为目录名） |
| `program` | `string` | 可执行文件完整路径 |
| `metaHash` | `string` | 元数据 SHA256 哈希（用于变更检测） |
| `name` | `string` | Driver 名称（来自元数据，无 meta 时不返回） |
| `version` | `string` | Driver 版本（来自元数据，无 meta 时不返回） |

### POST /api/drivers/scan

手动触发 Driver 目录重扫。会重新执行完整的扫描流程。

```bash
curl -X POST http://127.0.0.1:8080/api/drivers/scan
```

**请求体（可选）：**

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `refreshMeta` | `boolean` | `true` | 是否刷新 Driver 元数据。设为 `false` 时仅扫描目录，跳过 meta 查询 |

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
| GET | `/api/services/{id}` | Service 详情（含 Manifest、Schema、Project 列表） |
| POST | `/api/services/scan` | 触发 Service 重扫 |
| GET | `/api/projects` | 列出所有 Project |
| POST | `/api/projects` | 创建 Project |
| GET | `/api/projects/{id}` | Project 详情（含 Instance 列表、Schema） |
| PUT | `/api/projects/{id}` | 更新 Project |
| DELETE | `/api/projects/{id}` | 删除 Project |
| POST | `/api/projects/{id}/validate` | 验证配置 |
| POST | `/api/projects/{id}/start` | 启动 Project |
| POST | `/api/projects/{id}/stop` | 停止 Project |
| POST | `/api/projects/{id}/reload` | 重载配置 |
| GET | `/api/projects/{id}/runtime` | 运行态信息 |
| GET | `/api/instances` | 列出运行中 Instance |
| POST | `/api/instances/{id}/terminate` | 终止 Instance |
| GET | `/api/instances/{id}/logs` | 查看日志（支持 Instance ID 或 Project ID） |
| GET | `/api/drivers` | 列出已发现 Driver |
| POST | `/api/drivers/scan` | 触发 Driver 重扫 |
