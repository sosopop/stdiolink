# stdiolink 项目指南（CLAUDE）

基于 Qt 的跨平台 IPC 框架，使用 JSONL 协议通过 stdin/stdout 通信。当前代码已包含 `stdiolink_service` 与 `stdiolink_server` 两条运行时链路。

## 架构概览

三层架构，自底向上：

```
┌──────────────────────────────────────────────────┐
│  stdiolink_server  (管控面)                       │
│  项目管理 · 实例编排 · 调度引擎 · REST API         │
├──────────────────────────────────────────────────┤
│  stdiolink_service (JS 运行时)                    │
│  QuickJS 引擎 · ES Module · C++ 绑定             │
├──────────────────────────────────────────────────┤
│  stdiolink        (核心库)                        │
│  JSONL 协议 · Driver/Host · 元数据 · Console      │
└──────────────────────────────────────────────────┘
```

### 通信协议

进程间通过 stdin/stdout 传输 JSONL（每行一个 JSON）。三种消息语义：

- `ok` — 最终结果
- `event` — 中间流式事件（命令执行过程中的增量推送）
- `error` — 错误

请求格式：`{"cmd":"command_name","data":{...}}`

### 核心抽象

- `DriverCore`：Driver 端主类，两种生命周期（`OneShot` / `KeepAlive`），三种运行模式（Auto / Stdio / Console）
- `Driver`（Host 端）：管理 Driver 子进程，通过 `QProcess` 拉起、发命令、收响应
- `Task`：类 Future/Promise 异步句柄，支持流式读取中间事件；`waitAnyNext()` 实现多 Driver 并发等待
- `DriverMeta`：Driver 自描述元数据（命令定义、参数类型、约束、UI 提示），驱动配置校验与表单生成
- `DriverCatalog`：可用 Driver 注册表，带元数据缓存
- `ConfigInjector`：将配置注入 Driver 启动过程（startupArgs / env / command / file）

### Server 生命周期模型

```
Service (模板，扫描发现) → Project (实例化配置，持久化文件) → Instance (运行中进程)
```

调度策略：`manual`（手动触发）、`fixed_rate`（定时执行，带并发上限）、`daemon`（常驻守护，崩溃自动重启）

## 构建与测试

### Windows

```bash
build.bat [Release]                          # 配置 + 构建（Ninja）
cmake --build build --parallel 8             # 增量构建
./build/bin/stdiolink_tests.exe              # 运行测试
```

### macOS / Linux

```bash
./build.sh [Debug|Release]                   # 配置 + 构建（默认 Debug）
cmake --build build --parallel 8             # 增量构建
./build/bin/stdiolink_tests                  # 运行测试
```

常用可执行文件（默认位于 `build/bin/`）：

- `stdiolink_service`
- `stdiolink_server`
- `stdiolink_tests`
- `driverlab`
- `driver_3dvision` / `driver_modbusrtu` / `driver_modbustcp`

## 关键模块

### `src/stdiolink/`

核心库：协议、Driver 端、Host 端、Console、文档生成。

### `src/stdiolink_service/`

JS Service 运行时：QuickJS 引擎、模块加载、Driver/Task/Process/Config 绑定、配置校验、代理与调度。

### `src/stdiolink_server/`

服务管理器（M34-M38）：

- `config/`：`server_args`、`server_config`
- `scanner/`：`service_scanner`、`driver_manager_scanner`
- `manager/`：`project_manager`、`instance_manager`、`schedule_engine`
- `http/`：`api_router`、`http_helpers`
- `model/`：`project`、`schedule`、`instance`
- `server_manager.*`：编排层
- `main.cpp`：进程入口

### `src/demo/`

演示代码与资源目录。现有 `config_demo` / `js_runtime_demo` 资源会在构建后复制到 `build/bin/` 下。

## Server API 速览

`stdiolink_server` 当前注册 API（M38）：

- `GET /api/services`
- `GET /api/services/{id}`
- `GET /api/projects`
- `POST /api/projects`
- `GET /api/projects/{id}`
- `PUT /api/projects/{id}`
- `DELETE /api/projects/{id}`
- `POST /api/projects/{id}/validate`
- `POST /api/projects/{id}/start`
- `POST /api/projects/{id}/stop`
- `POST /api/projects/{id}/reload`
- `GET /api/instances`
- `POST /api/instances/{id}/terminate`
- `GET /api/instances/{id}/logs`
- `GET /api/drivers`
- `POST /api/drivers/scan`

## 发布与打包

已提供发布脚本：

- `tools/publish_release.sh`

示例：

```bash
# 默认输出到 release/<自动包名>
tools/publish_release.sh

# 指定构建目录、输出目录、包名
tools/publish_release.sh --build-dir build --output-dir release --name m39_preview

# 包含测试二进制
tools/publish_release.sh --with-tests
```

发布目录默认包含：

- `bin/`（主二进制）
- `demo/`（demo 资源）
- `data_root/`（标准目录模板）
- `doc/`（关键文档）
- `RELEASE_MANIFEST.txt`

## 开发约束（摘要）

- 文件 I/O 与 JSON 优先使用 Qt 类型：`QFile`、`QTextStream`、`QJsonObject` 等
- 命名：类 `CamelCase`、方法 `camelBack`、成员 `m_` 前缀
- Windows 管道读取避免 `fread()`，使用 `QTextStream::readLine()`
- 提交遵循 Conventional Commits：`feat:` / `fix:` / `docs:` / `test:` / `refactor:`

## 里程碑状态

- M1-M33：已落地（协议、元数据、Host/Driver、JS runtime 等）
- M34-M38：已落地（Server 扫描、Project/Instance/Schedule、HTTP API）
- M39：进行中（ServerManager 全链路 demo + 发布流程完善）
