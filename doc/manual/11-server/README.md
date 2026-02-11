# Server 管理器（stdiolink_server）

`stdiolink_server` 是 stdiolink 框架的管控面，提供 Service 编排、Project 配置管理、Instance 生命周期管理、调度引擎和 HTTP API。它将底层的 `stdiolink_service` JS 运行时作为子进程管理，实现从"单个脚本手动运行"到"多服务自动调度与远程管控"的能力跃迁。

## 核心概念

### 三级生命周期模型

```
Service (模板)  →  Project (实例化配置)  →  Instance (运行中进程)
```

- **Service**：一个 JS Service 目录，包含 `manifest.json`、`config.schema.json`、`index.js`。由 `ServiceScanner` 在启动时自动发现。Service 本身是只读模板，不可修改。
- **Project**：对某个 Service 的一次实例化配置。包含业务参数（`config`）和调度策略（`schedule`）。存储为 `projects/{id}.json` 文件，支持 CRUD 操作。
- **Instance**：一个正在运行的 `stdiolink_service` 子进程。由 `InstanceManager` 创建和监控，日志重定向到 `logs/{projectId}.log`。

### 调度策略

| 策略 | 说明 | 退出后行为 |
|------|------|-----------|
| `manual` | 仅通过 API 手动触发 | 不重启 |
| `fixed_rate` | 按固定间隔定时触发 | 不重启，等待下次定时 |
| `daemon` | 启动后常驻运行 | 异常退出自动重启，正常退出不重启 |

### 子系统总览

| 子系统 | 职责 |
|--------|------|
| `ServiceScanner` | 扫描 `services/` 目录，加载 Service 模板 |
| `DriverManagerScanner` | 扫描 `drivers/` 目录，自动导出 Driver 元数据 |
| `ProjectManager` | 管理 Project 配置文件的 CRUD 和 Schema 验证 |
| `InstanceManager` | 管理 `stdiolink_service` 子进程的创建、监控、终止 |
| `ScheduleEngine` | 根据 Project 的调度策略自动编排 Instance |
| `ServerManager` | 编排层，统一持有所有子系统引用 |
| `ApiRouter` | 基于 `QHttpServer` 的 RESTful API 路由 |

## 架构图

```
┌──────────────────────────────────────────────────────┐
│                    HTTP Client                        │
│              (curl / 前端 / 第三方系统)                 │
└────────────────────────┬─────────────────────────────┘
                         │ REST API
┌────────────────────────▼─────────────────────────────┐
│                   ApiRouter                           │
│         Service / Project / Instance / Driver API     │
├──────────────────────────────────────────────────────┤
│                  ServerManager                        │
│    ┌──────────────┬──────────────┬──────────────┐    │
│    │ ServiceScanner│ProjectManager│ScheduleEngine│    │
│    └──────────────┴──────────────┴──────┬───────┘    │
│    ┌──────────────┐              ┌──────▼───────┐    │
│    │DriverManager │              │InstanceManager│    │
│    │   Scanner    │              │              │    │
│    └──────────────┘              └──────┬───────┘    │
├──────────────────────────────────────────┼───────────┤
│                                    QProcess          │
│                              stdiolink_service       │
└──────────────────────────────────────────────────────┘
```

## 本章目录

- [快速入门](getting-started.md) — 启动配置、目录结构、第一次运行
- [Service 扫描](service-scanner.md) — Service 目录约定与扫描机制
- [Driver 扫描](driver-scanner.md) — Driver 元数据导出与失败隔离
- [Project 管理](project-management.md) — Project 配置格式、验证、调度参数
- [Instance 与调度](instance-and-schedule.md) — 实例生命周期与三种调度策略
- [HTTP API 参考](http-api.md) — 完整的 REST API 接口文档
