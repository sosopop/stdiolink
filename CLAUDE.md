# stdiolink 项目指南（CLAUDE）

基于 Qt 的跨平台 IPC 框架，使用 JSONL 协议通过 stdin/stdout 通信。当前代码已包含核心库、JS 运行时及管控后端服务。

## 架构概览

三层架构，自底向上：

```
┌──────────────────────────────────────────────────┐
│  stdiolink_server  (管控面)                       │
│  项目管理 · 实例编排 · 调度引擎 · REST API · SSE   │
├──────────────────────────────────────────────────┤
│  stdiolink_service (JS 运行时)                    │
│  QuickJS 引擎 · ES Module · C++ 绑定             │
├──────────────────────────────────────────────────┤
│  stdiolink        (核心库)                        │
│  JSONL 协议 · Driver/Host · 元数据 · Console      │
└──────────────────────────────────────────────────┘
```

### 通信协议

进程间通过 stdin/stdout 传输 JSONL（每行一个 JSON）。四种消息语义：

- `done` — 最终执行成功结果（替代旧版 `ok`）
- `event` — 中间流式事件（命令执行过程中的增量推送）
- `error` — 错误响应
- `meta.describe` — 元数据导出

请求格式：`{"cmd":"command_name","data":{...}}`

### 核心抽象

- `DriverCore`：Driver 端主类，支持 `OneShot` / `KeepAlive` 生命周期。
- `Driver`（Host 端）：管理 Driver 子进程，处理异步通讯与进程早退检测。
- `Task`：Future/Promise 风格句柄，支持 `waitAnyNext()` 并发调度。
- `DriverMeta`：自描述元数据，支持配置校验、自动表单生成与 OpenAPI 文档导出。
- `ServerManager`：编排层，管理 `Service` (模板) → `Project` (配置) → `Instance` (进程) 的全生命周期。

## 构建与发布

### 构建 (Windows/macOS/Linux)

- Windows: `build.bat [Release]`
- Unix: `./build.sh [Debug|Release]`
- 测试: `./build/bin/stdiolink_tests`

### 测试

独立测试脚本支持选择性运行三套测试（GTest / Vitest / Playwright），无参数时全部执行：
```bash
tools/run_tests.sh                # 全部执行
tools/run_tests.sh --gtest        # 仅 C++ 单元测试
tools/run_tests.ps1 --vitest --playwright  # 仅 WebUI 测试
```

### 发布打包

使用 `tools/publish_release.ps1` (Windows) 或 `tools/publish_release.sh` (Unix)。
发布前默认执行全部测试，可用 `--skip-tests` 跳过。
```powershell
.\tools\publish_release.ps1 --name stdiolink_v1.0
.\tools\publish_release.ps1 --skip-tests --skip-webui  # 快速打包
```

## 关键模块

### `src/stdiolink/`
核心协议与基础库。包含元数据 Builder、Validator 及文档生成器。

### `src/stdiolink_server/`
管控后端：
- `manager/`：项目管理 (`ProjectManager`)、实例管理 (`InstanceManager`)、调度引擎 (`ScheduleEngine`)
- `http/`：REST API 路由、SSE 事件推送、DriverLab WebSocket 代理
- `scanner/`：Service 与 Driver 的自动扫描发现

### `src/webui/`
React 18 + TypeScript + Vite 前端。
- **设计规范**: "Style 06" (Premium Glassmorphism)，采用 Bento Grid 布局。
- **核心组件**: Dashboard (Mission Control)、DriverLab (交互调试)、SchemaEditor (可视配置)。
- **国际化**: 支持 9 种语言 (i18next)。

## 里程碑状态

- [x] M1-M33: 核心协议、元数据、JS Runtime 及其全量 C++ 绑定。
- [x] M34-M48: Server 架构、项目生命周期管理、SSE/WebSocket 通讯。
- [x] M49-M69: WebUI 全量实现、"Style 06" 视觉重构、E2E 测试、发布脚本完善。

## 开发约束

- **Qt 优先**: 文件 I/O 使用 `QFile`，JSON 使用 `QJsonObject`。
- **无阻塞 I/O**: Windows 管道读取必须使用 `QTextStream::readLine()`。
- **命名规范**: 类 `CamelCase`、方法 `camelBack`、成员 `m_` 前缀。
- **提交规范**: 遵循 Conventional Commits。
