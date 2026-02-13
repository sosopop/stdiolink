# StdioLink Server HTTP API 文档

## 概述

StdioLink Server 提供了一套完整的 RESTful API，用于管理服务（Services）、项目（Projects）、实例（Instances）和驱动（Drivers）。所有 API 响应均为 JSON 格式。

**基础 URL**: `http://{host}:{port}/api`

**默认配置**:
- Host: `127.0.0.1`
- Port: `8080`

**CORS 支持**: 所有接口支持跨域请求，默认允许所有来源（`*`）

---

## 通用规范

### HTTP 方法

- `GET` - 查询资源
- `POST` - 创建资源或执行操作
- `PUT` - 更新资源
- `PATCH` - 部分更新资源
- `DELETE` - 删除资源

### 响应格式

#### 成功响应

```json
{
  "field1": "value1",
  "field2": "value2"
}
```

#### 错误响应

```json
{
  "error": "错误描述信息"
}
```

### HTTP 状态码

- `200 OK` - 请求成功
- `201 Created` - 资源创建成功
- `204 No Content` - 操作成功但无返回内容
- `400 Bad Request` - 请求参数错误
- `404 Not Found` - 资源不存在
- `409 Conflict` - 资源冲突（如删除时存在关联）
- `413 Payload Too Large` - 请求体过大
- `500 Internal Server Error` - 服务器内部错误

---

## 1. Server API

### 1.1 获取服务器状态

获取服务器运行状态和统计信息。

**请求**

```http
GET /api/server/status
```

**响应**

```json
{
  "counts": {
    "services": 5,
    "projects": {
      "total": 10,
      "valid": 8,
      "invalid": 2,
      "enabled": 7,
      "disabled": 3
    },
    "instances": {
      "total": 15,
      "running": 12
    },
    "drivers": 3
  },
  "system": {
    "platform": "Linux",
    "cpuCores": 8
  }
}
```

**字段说明**
- `counts.services`: 服务总数
- `counts.projects`: 项目统计信息
- `counts.instances`: 实例统计信息
- `counts.drivers`: 驱动总数
- `system.platform`: 操作系统平台
- `system.cpuCores`: CPU 核心数

---

## 2. Service API

服务（Service）是可复用的业务模块，包含代码和配置模式。

### 2.1 获取服务列表

获取所有已注册的服务。

**请求**

```http
GET /api/services
```

**响应**

```json
{
  "services": [
    {
      "id": "my-service",
      "name": "My Service",
      "version": "1.0.0",
      "serviceDir": "/path/to/services/my-service",
      "hasSchema": true,
      "projectCount": 3
    }
  ]
}
```

**字段说明**
- `id`: 服务唯一标识符
- `name`: 服务显示名称
- `version`: 服务版本号
- `serviceDir`: 服务目录路径
- `hasSchema`: 是否包含配置 Schema
- `projectCount`: 使用此服务的项目数量

### 2.2 获取服务详情

获取指定服务的详细信息。

**请求**

```http
GET /api/services/{serviceId}
```

**路径参数**
- `serviceId`: 服务 ID

**响应**

```json
{
  "id": "my-service",
  "name": "My Service",
  "version": "1.0.0",
  "serviceDir": "/path/to/services/my-service",
  "manifest": {
    "manifestVersion": "1",
    "id": "my-service",
    "name": "My Service",
    "version": "1.0.0",
    "description": "服务描述",
    "author": "作者名称"
  },
  "configSchema": {
    "fields": [...]
  },
  "configSchemaFields": [...],
  "projects": ["project-1", "project-2"]
}
```

**字段说明**
- `manifest`: 服务清单信息
- `configSchema`: 原始配置 Schema
- `configSchemaFields`: 配置字段元数据数组
- `projects`: 使用此服务的项目 ID 列表

### 2.3 创建服务

创建新的服务。

**请求**

```http
POST /api/services
Content-Type: application/json

{
  "id": "new-service",
  "name": "New Service",
  "version": "1.0.0",
  "description": "服务描述",
  "author": "作者名称",
  "templateType": "basic",
  "indexJs": "// 可选：自定义 index.js 内容",
  "configSchema": {}
}
```

**请求参数**
- `id` (必填): 服务 ID，仅支持字母、数字、下划线、连字符
- `name` (必填): 服务名称
- `version` (必填): 服务版本
- `description` (可选): 服务描述
- `author` (可选): 作者信息
- `templateType` (可选): 模板类型（`basic`、`advanced` 等）
- `indexJs` (可选): 自定义 index.js 内容
- `configSchema` (可选): 自定义配置 Schema

**响应**

```json
{
  "id": "new-service",
  "name": "New Service",
  "version": "1.0.0",
  "serviceDir": "/path/to/services/new-service",
  "hasSchema": true,
  "created": true
}
```

**状态码**
- `201 Created`: 创建成功
- `400 Bad Request`: 参数错误
- `409 Conflict`: 服务已存在

### 2.4 删除服务

删除指定的服务。

**请求**

```http
DELETE /api/services/{serviceId}?force=false
```

**路径参数**
- `serviceId`: 服务 ID

**查询参数**
- `force` (可选): 是否强制删除（默认 `false`）
  - `false`: 如果有关联项目则拒绝删除
  - `true`: 强制删除并标记关联项目为无效

**响应**

成功时返回 `204 No Content`

**错误响应**

如果存在关联项目且未设置 `force=true`：

```json
{
  "error": "service has associated projects: project-1, project-2",
  "associatedProjects": ["project-1", "project-2"]
}
```

**状态码**
- `204 No Content`: 删除成功
- `404 Not Found`: 服务不存在
- `409 Conflict`: 存在关联项目

### 2.5 扫描服务

扫描服务目录，更新服务列表。

**请求**

```http
POST /api/services/scan
Content-Type: application/json

{
  "revalidateProjects": true,
  "restartScheduling": true,
  "stopInvalidProjects": false
}
```

**请求参数**
- `revalidateProjects` (可选): 是否重新验证项目（默认 `true`）
- `restartScheduling` (可选): 是否重启调度（默认 `true`）
- `stopInvalidProjects` (可选): 是否停止无效项目（默认 `false`）

**响应**

```json
{
  "scan": {
    "scanned": 10,
    "loaded": 8,
    "failed": 2
  },
  "added": 2,
  "updated": 3,
  "removed": 1,
  "unchanged": 4,
  "revalidatedProjects": 5,
  "becameValid": 1,
  "becameInvalid": 1,
  "remainedInvalid": 0,
  "invalidProjectIds": ["invalid-project-1"],
  "schedulingRestarted": true
}
```

### 2.6 验证配置 Schema

验证服务的配置 Schema 是否有效。

**请求**

```http
POST /api/services/{serviceId}/validate-schema
Content-Type: application/json

{
  "schema": {
    "fields": [...]
  }
}
```

**响应**

```json
{
  "valid": true
}
```

或

```json
{
  "valid": false,
  "error": "Schema 验证错误信息"
}
```

### 2.7 生成默认配置

根据 Schema 生成默认配置值。

**请求**

```http
POST /api/services/{serviceId}/generate-defaults
Content-Type: application/json

{
  "config": {}
}
```

**响应**

```json
{
  "config": {
    "field1": "defaultValue1",
    "field2": 42
  }
}
```

### 2.8 验证配置

验证配置是否符合服务的 Schema。

**请求**

```http
POST /api/services/{serviceId}/validate-config
Content-Type: application/json

{
  "config": {
    "field1": "value1",
    "field2": 42
  }
}
```

**响应**

```json
{
  "valid": true,
  "filledConfig": {
    "field1": "value1",
    "field2": 42
  }
}
```

或

```json
{
  "valid": false,
  "error": "配置验证错误",
  "errorField": "field1"
}
```

---

## 3. Service Files API

管理服务的文件系统。

### 3.1 获取文件列表

获取服务目录下的所有文件。

**请求**

```http
GET /api/services/{serviceId}/files
```

**响应**

```json
{
  "serviceId": "my-service",
  "serviceDir": "/path/to/services/my-service",
  "files": [
    {
      "name": "index.js",
      "path": "index.js",
      "size": 1024,
      "modifiedAt": "2024-01-01T12:00:00Z",
      "type": "file"
    },
    {
      "name": "manifest.json",
      "path": "manifest.json",
      "size": 256,
      "modifiedAt": "2024-01-01T12:00:00Z",
      "type": "file"
    }
  ]
}
```

### 3.2 读取文件内容

读取服务文件的内容。

**请求**

```http
GET /api/services/{serviceId}/files/content?path=index.js
```

**查询参数**
- `path` (必填): 文件相对路径

**响应**

```json
{
  "path": "index.js",
  "content": "// 文件内容...",
  "size": 1024,
  "modifiedAt": "2024-01-01T12:00:00Z"
}
```

**注意事项**
- 文件大小限制：1MB
- 路径安全检查：禁止访问服务目录外的文件
- 仅支持文本文件

### 3.3 更新文件内容

更新服务文件的内容。

**请求**

```http
PUT /api/services/{serviceId}/files/content?path=index.js
Content-Type: application/json

{
  "content": "// 新的文件内容..."
}
```

**查询参数**
- `path` (必填): 文件相对路径

**请求参数**
- `content` (必填): 新的文件内容（字符串）

**响应**

```json
{
  "path": "index.js",
  "size": 1024,
  "updated": true
}
```

**特殊处理**
- 更新 `manifest.json` 或 `config.schema.json` 后会自动重新加载服务
- 更新 `manifest.json` 会验证 JSON 格式和必填字段
- 更新 `config.schema.json` 会验证 JSON 格式

**状态码**
- `200 OK`: 更新成功
- `400 Bad Request`: 参数错误或文件验证失败
- `404 Not Found`: 文件不存在
- `413 Payload Too Large`: 内容超过 1MB

### 3.4 创建文件

在服务目录中创建新文件。

**请求**

```http
POST /api/services/{serviceId}/files/content?path=new-file.js
Content-Type: application/json

{
  "content": "// 文件内容..."
}
```

**查询参数**
- `path` (必填): 文件相对路径

**请求参数**
- `content` (必填): 文件内容（字符串）

**响应**

```json
{
  "path": "new-file.js",
  "size": 512,
  "created": true
}
```

**注意事项**
- 路径中的目录会自动创建
- 禁止覆盖已存在的文件（使用 PUT 更新）
- 禁止创建核心文件（`manifest.json`、`config.schema.json`、`index.js`）

**状态码**
- `201 Created`: 创建成功
- `400 Bad Request`: 参数错误或文件已存在
- `413 Payload Too Large`: 内容超过 1MB

### 3.5 删除文件

删除服务文件。

**请求**

```http
DELETE /api/services/{serviceId}/files/content?path=old-file.js
```

**查询参数**
- `path` (必填): 文件相对路径

**响应**

成功时返回 `204 No Content`

**注意事项**
- 禁止删除核心文件（`manifest.json`、`config.schema.json`、`index.js`）

**状态码**
- `204 No Content`: 删除成功
- `400 Bad Request`: 禁止删除核心文件
- `404 Not Found`: 文件不存在

---

## 4. Project API

项目（Project）是服务的具体运行实例。

### 4.1 获取项目列表

获取所有项目。

**请求**

```http
GET /api/projects
```

**响应**

```json
{
  "projects": [
    {
      "id": "my-project",
      "name": "My Project",
      "serviceId": "my-service",
      "enabled": true,
      "valid": true,
      "schedule": {
        "type": "daemon"
      },
      "config": {},
      "instanceCount": 1,
      "status": "running"
    }
  ]
}
```

**字段说明**
- `id`: 项目 ID
- `name`: 项目名称
- `serviceId`: 关联的服务 ID
- `enabled`: 是否启用
- `valid`: 是否有效（配置和服务是否正确）
- `schedule`: 调度配置
  - `type`: `manual`（手动）、`fixed_rate`（定时）、`daemon`（守护进程）
- `config`: 项目配置
- `instanceCount`: 运行中的实例数量
- `status`: 项目状态（`invalid`、`disabled`、`stopped`、`running`）

### 4.2 获取项目详情

获取指定项目的详细信息。

**请求**

```http
GET /api/projects/{projectId}
```

**响应**

```json
{
  "id": "my-project",
  "name": "My Project",
  "serviceId": "my-service",
  "enabled": true,
  "valid": true,
  "schedule": {
    "type": "daemon",
    "intervalSeconds": 0
  },
  "config": {
    "field1": "value1"
  },
  "instanceCount": 1,
  "status": "running"
}
```

### 4.3 创建项目

创建新项目。

**请求**

```http
POST /api/projects
Content-Type: application/json

{
  "id": "new-project",
  "name": "New Project",
  "serviceId": "my-service",
  "enabled": true,
  "schedule": {
    "type": "manual"
  },
  "config": {}
}
```

**请求参数**
- `id` (必填): 项目 ID
- `name` (必填): 项目名称
- `serviceId` (必填): 服务 ID
- `enabled` (可选): 是否启用（默认 `true`）
- `schedule` (必填): 调度配置
  - `type` (必填): `manual`、`fixed_rate`、`daemon`
  - `intervalSeconds` (可选): 定时间隔（`fixed_rate` 类型需要）
- `config` (可选): 项目配置

**响应**

```json
{
  "id": "new-project",
  "name": "New Project",
  "serviceId": "my-service",
  "enabled": true,
  "valid": true,
  "schedule": {
    "type": "manual"
  },
  "config": {},
  "instanceCount": 0,
  "status": "stopped",
  "created": true
}
```

**状态码**
- `201 Created`: 创建成功
- `400 Bad Request`: 参数错误
- `409 Conflict`: 项目 ID 已存在

### 4.4 更新项目

更新项目配置。

**请求**

```http
PUT /api/projects/{projectId}
Content-Type: application/json

{
  "name": "Updated Project Name",
  "enabled": true,
  "schedule": {
    "type": "daemon"
  },
  "config": {
    "field1": "newValue"
  }
}
```

**请求参数**
- `name` (可选): 项目名称
- `enabled` (可选): 是否启用
- `schedule` (可选): 调度配置
- `config` (可选): 项目配置

**响应**

```json
{
  "id": "my-project",
  "name": "Updated Project Name",
  "serviceId": "my-service",
  "enabled": true,
  "valid": true,
  "schedule": {
    "type": "daemon"
  },
  "config": {
    "field1": "newValue"
  },
  "instanceCount": 1,
  "status": "running",
  "updated": true
}
```

**注意事项**
- 更新后会自动重新验证配置
- 如果项目正在运行且调度类型改变，会自动重新加载

### 4.5 删除项目

删除指定项目。

**请求**

```http
DELETE /api/projects/{projectId}
```

**响应**

成功时返回 `204 No Content`

**注意事项**
- 删除前会自动停止项目并终止所有实例

**状态码**
- `204 No Content`: 删除成功
- `404 Not Found`: 项目不存在

### 4.6 验证项目

验证项目配置是否有效。

**请求**

```http
POST /api/projects/{projectId}/validate
```

**响应**

```json
{
  "valid": true,
  "config": {
    "field1": "value1"
  }
}
```

或

```json
{
  "valid": false,
  "error": "验证错误信息"
}
```

### 4.7 启动项目

手动启动项目（创建新实例）。

**请求**

```http
POST /api/projects/{projectId}/start
```

**响应**

```json
{
  "started": true,
  "instanceId": "inst-abc123"
}
```

**注意事项**
- 仅适用于 `manual` 类型的项目
- `daemon` 和 `fixed_rate` 类型的项目由调度器自动管理

**状态码**
- `200 OK`: 启动成功
- `400 Bad Request`: 项目类型不支持手动启动
- `404 Not Found`: 项目不存在

### 4.8 停止项目

停止项目（终止所有实例）。

**请求**

```http
POST /api/projects/{projectId}/stop
```

**响应**

```json
{
  "stopped": true,
  "terminatedInstances": 3
}
```

### 4.9 重新加载项目

重新加载项目配置和调度。

**请求**

```http
POST /api/projects/{projectId}/reload
```

**响应**

```json
{
  "reloaded": true
}
```

**注意事项**
- 会重新验证配置
- 会重启调度器

### 4.10 获取项目运行时信息

获取项目的运行时状态。

**请求**

```http
GET /api/projects/{projectId}/runtime
```

**响应**

```json
{
  "id": "my-project",
  "enabled": true,
  "valid": true,
  "status": "running",
  "runningInstances": 2,
  "instances": [
    {
      "id": "inst-abc123",
      "projectId": "my-project",
      "serviceId": "my-service",
      "pid": 12345,
      "startedAt": "2024-01-01T12:00:00Z",
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

**字段说明**
- `schedule.timerActive`: 定时器是否激活
- `schedule.restartSuppressed`: 是否禁止重启（连续失败过多）
- `schedule.consecutiveFailures`: 连续失败次数
- `schedule.shuttingDown`: 是否正在关闭
- `schedule.autoRestarting`: 是否自动重启

### 4.11 批量获取运行时信息

批量获取多个项目的运行时状态。

**请求**

```http
GET /api/projects/runtime?ids=project-1,project-2
```

**查询参数**
- `ids` (可选): 逗号分隔的项目 ID 列表，不指定则返回所有项目

**响应**

```json
{
  "runtimes": [
    {
      "id": "project-1",
      "enabled": true,
      "valid": true,
      "status": "running",
      "runningInstances": 1,
      "instances": [...],
      "schedule": {...}
    },
    {
      "id": "project-2",
      "enabled": false,
      "valid": true,
      "status": "disabled",
      "runningInstances": 0,
      "instances": [],
      "schedule": {...}
    }
  ]
}
```

### 4.12 设置项目启用状态

启用或禁用项目。

**请求**

```http
PATCH /api/projects/{projectId}/enabled
Content-Type: application/json

{
  "enabled": true
}
```

**请求参数**
- `enabled` (必填): `true` 启用，`false` 禁用

**响应**

```json
{
  "id": "my-project",
  "enabled": true,
  "updated": true
}
```

**注意事项**
- 禁用项目会停止所有实例
- 启用项目会根据调度类型自动启动

### 4.13 获取项目日志

获取项目的最新日志。

**请求**

```http
GET /api/projects/{projectId}/logs?lines=100
```

**查询参数**
- `lines` (可选): 返回的日志行数（默认 100，范围 1-5000）

**响应**

```json
{
  "projectId": "my-project",
  "lines": [
    "2024-01-01T12:00:00Z [INFO] Service started",
    "2024-01-01T12:00:01Z [DEBUG] Processing request..."
  ],
  "logPath": "/path/to/logs/my-project.log"
}
```

---

## 5. Instance API

实例（Instance）是项目的运行进程。

### 5.1 获取实例列表

获取所有或指定项目的实例。

**请求**

```http
GET /api/instances?projectId=my-project
```

**查询参数**
- `projectId` (可选): 项目 ID，不指定则返回所有实例

**响应**

```json
{
  "instances": [
    {
      "id": "inst-abc123",
      "projectId": "my-project",
      "serviceId": "my-service",
      "pid": 12345,
      "startedAt": "2024-01-01T12:00:00Z",
      "status": "running"
    }
  ]
}
```

### 5.2 获取实例详情

获取指定实例的详细信息。

**请求**

```http
GET /api/instances/{instanceId}
```

**响应**

```json
{
  "id": "inst-abc123",
  "projectId": "my-project",
  "serviceId": "my-service",
  "pid": 12345,
  "startedAt": "2024-01-01T12:00:00Z",
  "status": "running"
}
```

### 5.3 终止实例

终止指定的运行实例。

**请求**

```http
POST /api/instances/{instanceId}/terminate
```

**响应**

```json
{
  "terminated": true
}
```

**注意事项**
- 终止后进程会收到 SIGTERM 信号
- 如果项目是 `daemon` 类型，可能会自动重启新实例

### 5.4 获取实例日志

获取实例的日志输出。

**请求**

```http
GET /api/instances/{instanceId}/logs?lines=100
```

**查询参数**
- `lines` (可选): 返回的日志行数（默认 100，范围 1-5000）

**响应**

```json
{
  "projectId": "my-project",
  "lines": [
    "2024-01-01T12:00:00Z [INFO] Instance started",
    "2024-01-01T12:00:01Z [DEBUG] Processing..."
  ]
}
```

### 5.5 获取实例进程树

获取实例的进程树信息。

**请求**

```http
GET /api/instances/{instanceId}/process-tree
```

**响应**

```json
{
  "instanceId": "inst-abc123",
  "rootPid": 12345,
  "processTree": {
    "pid": 12345,
    "name": "node",
    "children": [
      {
        "pid": 12346,
        "name": "child-process",
        "children": []
      }
    ]
  }
}
```

### 5.6 获取实例资源使用情况

获取实例的 CPU 和内存使用情况。

**请求**

```http
GET /api/instances/{instanceId}/resources
```

**响应**

```json
{
  "instanceId": "inst-abc123",
  "pid": 12345,
  "resources": {
    "cpuPercent": 15.5,
    "memoryMB": 128.5,
    "memoryPercent": 1.2
  },
  "timestamp": "2024-01-01T12:00:00Z"
}
```

**字段说明**
- `cpuPercent`: CPU 使用率（百分比）
- `memoryMB`: 内存使用量（MB）
- `memoryPercent`: 内存使用率（百分比）

---

## 6. Driver API

驱动（Driver）是可执行的 stdiolink 兼容程序。

### 6.1 获取驱动列表

获取所有已注册的驱动。

**请求**

```http
GET /api/drivers
```

**响应**

```json
{
  "drivers": [
    {
      "id": "my-driver",
      "program": "/path/to/driver",
      "metaHash": "abc123...",
      "name": "My Driver",
      "version": "1.0.0"
    }
  ]
}
```

**字段说明**
- `id`: 驱动 ID
- `program`: 驱动程序路径
- `metaHash`: 元数据哈希值
- `name`: 驱动名称（来自元数据）
- `version`: 驱动版本（来自元数据）

### 6.2 获取驱动详情

获取指定驱动的详细信息。

**请求**

```http
GET /api/drivers/{driverId}
```

**响应**

```json
{
  "id": "my-driver",
  "program": "/path/to/driver",
  "metaHash": "abc123...",
  "meta": {
    "info": {
      "name": "My Driver",
      "version": "1.0.0",
      "description": "驱动描述"
    },
    "commands": [
      {
        "name": "command1",
        "description": "命令描述",
        "params": []
      }
    ]
  }
}
```

### 6.3 扫描驱动

扫描系统中的驱动程序。

**请求**

```http
POST /api/drivers/scan
Content-Type: application/json

{
  "refreshMeta": true
}
```

**请求参数**
- `refreshMeta` (可选): 是否刷新元数据（默认 `true`）

**响应**

```json
{
  "scanned": 10,
  "updated": 3,
  "newlyFailed": 1,
  "skippedFailed": 2
}
```

**字段说明**
- `scanned`: 扫描的驱动总数
- `updated`: 更新的驱动数量
- `newlyFailed`: 新失败的驱动数量
- `skippedFailed`: 跳过的失败驱动数量

---

## 7. Event Stream API

服务器端事件（SSE）推送。

### 7.1 订阅事件流

建立 SSE 连接接收服务器事件。

**请求**

```http
GET /api/events/stream
```

**响应格式**

```
Content-Type: text/event-stream

event: project.started
data: {"projectId":"my-project","instanceId":"inst-abc123"}

event: instance.terminated
data: {"instanceId":"inst-abc123","exitCode":0}

event: service.updated
data: {"serviceId":"my-service"}
```

**事件类型**
- `project.started`: 项目启动
- `project.stopped`: 项目停止
- `instance.started`: 实例启动
- `instance.terminated`: 实例终止
- `service.updated`: 服务更新
- `service.deleted`: 服务删除

---

## 附录

### A. 数据模型

#### Service（服务）

```typescript
interface Service {
  id: string;              // 服务 ID
  name: string;            // 服务名称
  version: string;         // 版本号
  serviceDir: string;      // 服务目录
  hasSchema: boolean;      // 是否有配置 Schema
  manifest: Manifest;      // 清单信息
  configSchema: object;    // 配置 Schema
}
```

#### Project（项目）

```typescript
interface Project {
  id: string;              // 项目 ID
  name: string;            // 项目名称
  serviceId: string;       // 关联的服务 ID
  enabled: boolean;        // 是否启用
  valid: boolean;          // 是否有效
  schedule: Schedule;      // 调度配置
  config: object;          // 项目配置
  error?: string;          // 错误信息（如果无效）
}

interface Schedule {
  type: 'manual' | 'fixed_rate' | 'daemon';
  intervalSeconds?: number;  // fixed_rate 类型需要
}
```

#### Instance（实例）

```typescript
interface Instance {
  id: string;              // 实例 ID
  projectId: string;       // 项目 ID
  serviceId: string;       // 服务 ID
  pid: number;             // 进程 ID
  startedAt: string;       // 启动时间（ISO 8601）
  status: string;          // 状态
}
```

### B. 错误码参考

| HTTP 状态码 | 错误场景 | 示例 |
|------------|---------|------|
| 400 | 请求参数错误 | 缺少必填字段、格式错误 |
| 404 | 资源不存在 | 服务/项目/实例不存在 |
| 409 | 资源冲突 | ID 已存在、存在关联资源 |
| 413 | 请求体过大 | 文件内容超过 1MB |
| 500 | 服务器错误 | 文件系统错误、内部异常 |

### C. 调度类型说明

| 类型 | 说明 | 启动方式 | 重启策略 |
|-----|------|---------|---------|
| `manual` | 手动模式 | 通过 API 手动启动 | 不自动重启 |
| `fixed_rate` | 定时任务 | 按固定间隔自动启动 | 执行完成后等待下次周期 |
| `daemon` | 守护进程 | 启用后立即启动 | 进程退出后自动重启 |

### D. 最佳实践

#### 1. 创建服务的完整流程

```bash
# 1. 创建服务
POST /api/services
{
  "id": "my-new-service",
  "name": "My New Service",
  "version": "1.0.0"
}

# 2. 更新 index.js
PUT /api/services/my-new-service/files/content?path=index.js
{
  "content": "// Service code..."
}

# 3. 更新配置 Schema
PUT /api/services/my-new-service/files/content?path=config.schema.json
{
  "content": "{...}"
}

# 4. 创建项目使用此服务
POST /api/projects
{
  "id": "my-project",
  "serviceId": "my-new-service",
  "name": "My Project",
  "schedule": {"type": "daemon"},
  "config": {}
}
```

#### 2. 监控项目运行状态

```bash
# 1. 获取运行时信息
GET /api/projects/my-project/runtime

# 2. 查看实例详情
GET /api/instances/{instanceId}

# 3. 查看资源使用
GET /api/instances/{instanceId}/resources

# 4. 查看日志
GET /api/instances/{instanceId}/logs?lines=100
```

#### 3. 配置文件大小限制

所有文件操作（读取、写入、创建）都有 **1MB** 的大小限制。超过此限制会返回 `413 Payload Too Large` 错误。

#### 4. 路径安全

所有文件操作都会进行路径安全检查：
- 禁止访问服务目录之外的文件
- 禁止使用 `..` 进行目录遍历
- 路径必须是相对路径

#### 5. 核心文件保护

以下文件受到保护，有特殊的操作限制：
- `manifest.json`: 可读可写，不可删除，写入时会验证格式
- `config.schema.json`: 可读可写，不可删除，写入时会验证格式
- `index.js`: 可读可写，不可删除

---

## 更新日志

### Version 1.0.0 (2024-01-01)
- 初始版本
- 完整的服务、项目、实例、驱动管理 API
- SSE 事件推送支持
- 文件系统管理 API
