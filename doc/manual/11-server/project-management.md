# Project 管理

Project 是对某个 Service 的一次实例化配置，包含业务参数和调度策略。每个 Project 对应 `projects/` 目录下的一个 JSON 文件。

## 配置文件格式

```json
{
  "name": "料仓A数据采集",
  "serviceId": "data-collector",
  "enabled": true,
  "schedule": {
    "type": "fixed_rate",
    "intervalMs": 5000,
    "maxConcurrent": 1
  },
  "config": {
    "device": { "host": "192.168.1.100", "port": 502 },
    "polling": { "intervalMs": 1000, "registers": [0, 1, 2] }
  }
}
```

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `name` | string | 是 | Project 显示名称 |
| `serviceId` | string | 是 | 关联的 Service ID |
| `enabled` | bool | 否 | 是否启用，默认 `true` |
| `schedule` | object | 是 | 调度策略配置 |
| `config` | object | 是 | 业务配置参数（由 Service 的 Schema 校验） |

## Project ID 规则

- Project ID 取自文件名（去掉 `.json` 后缀），如 `silo-a.json` → ID 为 `silo-a`
- 必须匹配正则 `^[A-Za-z0-9_-]+$`
- 禁止包含 `/`、`\`、`..`、空白字符
- 文件路径固定为 `projects/{id}.json`

## 调度策略（Schedule）

`schedule` 字段定义 Project 的运行方式，支持三种类型：

### manual — 手动触发

```json
{ "type": "manual" }
```

不自动启动，仅通过 `POST /api/projects/{id}/start` 手动触发。适用于一次性任务或按需执行的场景。

### fixed_rate — 定时执行

```json
{
  "type": "fixed_rate",
  "intervalMs": 5000,
  "maxConcurrent": 1
}
```

| 参数 | 类型 | 默认值 | 约束 | 说明 |
|------|------|--------|------|------|
| `intervalMs` | int | `5000` | >= 100 | 执行间隔（毫秒） |
| `maxConcurrent` | int | `1` | >= 1 | 最大并发实例数 |

按固定间隔周期性触发新 Instance。当运行中的 Instance 数量达到 `maxConcurrent` 时，跳过本次触发。Instance 退出后不重启，等待下次定时触发。

### daemon — 守护进程

```json
{
  "type": "daemon",
  "restartDelayMs": 3000,
  "maxConsecutiveFailures": 5
}
```

| 参数 | 类型 | 默认值 | 约束 | 说明 |
|------|------|--------|------|------|
| `restartDelayMs` | int | `3000` | >= 0 | 异常退出后重启延迟（毫秒） |
| `maxConsecutiveFailures` | int | `5` | >= 1 | 连续失败上限，达到后停止自动重启 |

启动后常驻运行。异常退出（崩溃或退出码非 0）后延迟 `restartDelayMs` 毫秒自动重启。正常退出（退出码 0）不重启。连续异常退出达到 `maxConsecutiveFailures` 次时进入崩溃抑制，停止自动重启。

## 配置验证

Project 加载或创建时会经过以下验证流程：

```
1. 解析 JSON 文件，提取 serviceId
2. 查找对应的 ServiceInfo（来自 ServiceScanner）
3. 若 Service 不存在 → 标记为 invalid
4. 使用 ServiceConfigValidator 验证 config 字段
5. 解析 schedule 参数并校验约束
6. 全部通过 → valid，任一失败 → invalid + 记录 error
```

验证失败的 Project 会被标记为 `invalid`，不参与调度，但仍会出现在 Project 列表中（带有错误信息）。

可通过 API 单独触发验证（不保存）：

```bash
curl -X POST http://127.0.0.1:8080/api/projects/silo-a/validate
```

## CRUD 操作

### 通过文件系统

直接在 `projects/` 目录下创建、编辑、删除 JSON 文件。修改后需通过 API 触发重载：

```bash
curl -X POST http://127.0.0.1:8080/api/projects/silo-a/reload
```

### 通过 HTTP API

```bash
# 创建 Project
curl -X POST http://127.0.0.1:8080/api/projects \
  -H "Content-Type: application/json" \
  -d '{
    "id": "silo-a",
    "name": "料仓A数据采集",
    "serviceId": "data-collector",
    "enabled": true,
    "schedule": {"type": "manual"},
    "config": {"device": {"host": "192.168.1.100", "port": 502}}
  }'

# 查看 Project
curl http://127.0.0.1:8080/api/projects/silo-a

# 更新 Project
curl -X PUT http://127.0.0.1:8080/api/projects/silo-a \
  -H "Content-Type: application/json" \
  -d '{
    "id": "silo-a",
    "name": "料仓A数据采集（更新）",
    "serviceId": "data-collector",
    "enabled": true,
    "schedule": {"type": "daemon", "restartDelayMs": 5000},
    "config": {"device": {"host": "192.168.1.100", "port": 502}}
  }'

# 删除 Project
curl -X DELETE http://127.0.0.1:8080/api/projects/silo-a
```

### 启动与停止

```bash
# 启动 Project（创建 Instance）
curl -X POST http://127.0.0.1:8080/api/projects/silo-a/start

# 停止 Project（终止所有 Instance）
curl -X POST http://127.0.0.1:8080/api/projects/silo-a/stop
```

详见 [HTTP API 参考](http-api.md)。
