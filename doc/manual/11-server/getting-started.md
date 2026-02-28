# Server 快速入门

本章介绍如何启动和配置 `stdiolink_server`。

## 数据根目录

`stdiolink_server` 的所有运行时数据集中在一个数据根目录（`data_root`）下，启动时自动创建缺失的子目录：

```
<data_root>/
├── config.json          # 服务器配置（可选）
├── services/            # Service 模板目录（自动创建）
├── projects/            # Project 配置文件（自动创建）
├── drivers/             # Driver 可执行文件目录
├── workspaces/          # 各 Project 的工作目录（自动创建）
├── logs/                # 日志目录（自动创建）
├── webui/               # WebUI 静态文件目录（可选）
└── shared/              # 全局共享目录
```

其中 `services/`、`projects/`、`workspaces/`、`logs/` 四个目录在启动时自动 `mkpath`，不存在则创建。`drivers/`、`webui/` 和 `shared/` 由用户按需建立。

## 命令行参数

```bash
stdiolink_server [options]
```

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--data-root=<path>` | 数据根目录路径 | `.`（当前目录） |
| `--port=<port>` | HTTP 监听端口 | `8080` |
| `--host=<addr>` | 监听地址 | `127.0.0.1` |
| `--webui-dir=<path>` | WebUI 静态目录（绝对路径，或相对 `data_root`） | `<data_root>/webui` |
| `--log-level=<level>` | 日志级别：`debug`/`info`/`warn`/`error` | `info` |
| `-h`, `--help` | 显示帮助信息 | — |
| `-v`, `--version` | 显示版本号 | — |

示例：

```bash
# 使用默认配置启动
stdiolink_server

# 指定数据目录和端口
stdiolink_server --data-root=/opt/stdiolink/data --port=9090

# 监听所有网卡（需配合外层鉴权）
stdiolink_server --host=0.0.0.0 --port=8080
```

## 配置文件（config.json）

数据根目录下可放置 `config.json` 提供持久化配置。该文件为可选项，不存在时使用默认值。

```json
{
  "port": 8080,
  "host": "127.0.0.1",
  "webuiDir": "webui",
  "logLevel": "info",
  "serviceProgram": ""
}
```

| 字段 | 类型 | 说明 | 默认值 |
|------|------|------|--------|
| `port` | int | HTTP 监听端口 | `8080` |
| `host` | string | 监听地址 | `127.0.0.1` |
| `webuiDir` | string | WebUI 静态目录（支持相对路径） | `webui` |
| `logLevel` | string | 日志级别 | `info` |
| `serviceProgram` | string | `stdiolink_service` 可执行文件路径 | 自动查找 |

配置优先级：**CLI 参数 > config.json > 内置默认值**。

### serviceProgram 查找规则

`stdiolink_server` 需要定位 `stdiolink_service` 可执行文件来启动 Instance。查找优先级：

1. `config.json` 中的 `serviceProgram` 字段（若非空且可执行）
2. 与 `stdiolink_server` 同目录下的 `stdiolink_service`
3. 系统 `PATH` 环境变量中的 `stdiolink_service`

### WebUI 静态托管规则

- 默认从 `<data_root>/webui` 提供静态文件
- 若配置了 `webuiDir`（或 `--webui-dir`），优先使用该目录
- 目录存在且包含 `index.html` 时，自动启用静态托管
- 对非 `/api/*` 的无扩展名路径启用 SPA 回退到 `index.html`

## 启动流程

`stdiolink_server` 启动时按以下顺序执行初始化：

```
1. 解析命令行参数
2. 加载 config.json（可选）
3. 创建标准目录结构（services/projects/workspaces/logs）
4. 扫描 services/ 目录 → 加载 Service 模板
5. 扫描 drivers/ 目录 → 导出/加载 Driver 元数据
6. 加载 projects/ 目录 → 验证 Project 配置
7. 启动调度引擎（daemon 立即启动，fixed_rate 启动定时器）
8. 启动 HTTP 服务器，开始监听
```

启动日志示例：

```
Services: 3 loaded, 0 failed
Drivers: 2 updated, 1 failed, 0 skipped
Projects: 5 loaded, 1 invalid
WebUI: serving from /opt/stdiolink/data/webui
HTTP server listening on 127.0.0.1:8080
```

## 安全说明

- 默认监听 `127.0.0.1`，仅本机可访问
- 当前版本不内置鉴权机制
- 若需对外暴露（`--host=0.0.0.0`），应在外层反向代理（如 Nginx）中配置鉴权
- 破坏性接口（start/stop/delete/terminate/scan）建议仅在受信网络中使用

## 构建

`stdiolink_server` 随项目一起构建，产物位于 `build/runtime_debug/bin/`（Debug）或 `build/runtime_release/bin/`（Release）：

```bash
# macOS / Linux
./build.sh
./build/runtime_debug/bin/stdiolink_server --help

# Windows
build.bat
build\runtime_debug\bin\stdiolink_server.exe --help
```

## 下一步

- [Service 扫描](service-scanner.md) — 了解 Service 目录约定
- [Project 管理](project-management.md) — 创建和管理 Project
- [HTTP API 参考](http-api.md) — 完整的 API 接口文档
