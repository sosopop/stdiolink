# Instance 与调度

Instance 是一个正在运行的 `stdiolink_service` 子进程，由 `InstanceManager` 创建和管理。`ScheduleEngine` 根据 Project 的调度策略自动编排 Instance 的启停。

## Instance 生命周期

### 启动流程

```
1. 读取 Project 配置
2. 定位 stdiolink_service 可执行文件
3. 生成临时配置文件（写入 Project 的 config）
4. 启动子进程：
   stdiolink_service <service_dir> --config-file=<temp_config>
5. 重定向 stdout/stderr 到 logs/{projectId}.log
6. 记录 Instance 到内存（ID、PID、启动时间）
```

### Instance 状态

| 状态 | 说明 |
|------|------|
| `starting` | 进程正在启动 |
| `running` | 进程运行中 |
| `stopped` | 正常退出（exitCode = 0） |
| `failed` | 异常退出（崩溃或 exitCode ≠ 0） |

### 退出处理

子进程退出后，`InstanceManager` 自动执行：

1. 记录退出码和退出状态
2. 触发 `instanceFinished` 信号
3. 清理临时配置文件
4. 从内存移除 Instance 对象
5. 通知 `ScheduleEngine`（由调度策略决定是否重启）

## 日志

每个 Instance 的 stdout 和 stderr 都会重定向到 `logs/{projectId}.log`（追加模式）。同一个 Project 的多次运行共享同一个日志文件。

可通过 API 查看日志。路径参数支持 Instance ID 或 Project ID（Instance 退出后仍可通过 Project ID 查看历史日志）：

```bash
# 通过 Instance ID 查看
curl http://127.0.0.1:8080/api/instances/{instanceId}/logs

# 通过 Project ID 查看（Instance 退出后仍可查看历史日志）
curl http://127.0.0.1:8080/api/instances/silo-a/logs

# 指定返回行数（默认 100，最大 5000）
curl "http://127.0.0.1:8080/api/instances/silo-a/logs?lines=50"
```

## 调度引擎

`ScheduleEngine` 在 Server 启动时为所有 `enabled` 且 `valid` 的 Project 启动调度。

### manual 模式

- 不自动启动任何 Instance
- 仅响应 `POST /api/projects/{id}/start` 手动触发
- 同一时刻最多运行一个 Instance，已有运行中实例时返回 `409 Conflict`

### fixed_rate 模式

- 启动时创建定时器，按 `intervalMs` 间隔周期触发
- 每次触发时检查当前运行中的 Instance 数量
- 若 `运行中数量 >= maxConcurrent`，跳过本次触发
- Instance 退出后不重启，等待下次定时触发
- 也可通过 API 手动触发额外的 Instance

### daemon 模式

- 启动时立即创建一个 Instance
- 正常退出（exitCode = 0）：不重启，重置连续失败计数
- 异常退出（崩溃或 exitCode ≠ 0）：延迟 `restartDelayMs` 后自动重启
- 连续异常退出达到 `maxConsecutiveFailures` 次：停止自动重启（崩溃抑制）
- 崩溃抑制后需人工介入排查，修复后可通过 API 手动重新启动

## 优雅关闭

`stdiolink_server` 收到 `SIGTERM` 或 `SIGINT` 信号时执行优雅关闭流程：

```
1. 设置 shuttingDown 标记（停止接受新的调度触发）
2. 停止所有定时器（fixed_rate）和自动重启（daemon）
3. 向所有运行中的 Instance 发送 terminate()
4. 等待实例退出（默认 5 秒宽限期）
5. 超时未退出的实例执行 kill() 强制终止
6. 清理临时配置文件，退出进程
```

## 通过 API 操作

```bash
# 查看所有运行中的 Instance
curl http://127.0.0.1:8080/api/instances

# 按 Project 筛选
curl "http://127.0.0.1:8080/api/instances?projectId=silo-a"

# 终止指定 Instance
curl -X POST http://127.0.0.1:8080/api/instances/{instanceId}/terminate

# 启动 Project（创建新 Instance）
curl -X POST http://127.0.0.1:8080/api/projects/silo-a/start

# 停止 Project（终止该 Project 的所有 Instance）
curl -X POST http://127.0.0.1:8080/api/projects/silo-a/stop
```

详见 [HTTP API 参考](http-api.md)。
