# stdiolink 项目指南 (GEMINI.md)

本文件为 Gemini CLI 提供项目上下文、架构说明及开发指南。

## 1. 项目概览

`stdiolink` 是一个基于 Qt 的跨平台 IPC 框架，使用 **JSONL (Line-delimited JSON)** 作为协议载体，通过 stdin/stdout 进行进程间通信。它旨在实现轻量级、自描述的 Host-Driver 通讯模型。

### 核心特性
- **单一协议**: 始终使用 JSONL，每行一个完整的 JSON 对象。
- **自描述元数据**: Driver 可以导出元数据 (`meta.describe`)，声明支持的命令、参数约束及事件流。
- **双模式运行**:
    - **StdIO 模式**: 适用于 Host 自动化控制，通过管道进行异步双向通讯。
    - **Console 模式**: 适用于命令行直接调用和调试，支持扁平化参数映射。
- **异步任务模型**: Host 侧提供 Future/Promise 风格的 `Task` 句柄，支持 `waitAnyNext` 并发等待多个 Driver。
- **自动文档与 UI**: 基于元数据自动生成 Markdown/OpenAPI 文档以及 UI 描述模型。
- **管理中枢 (Server & WebUI)**: 提供基于 Web 的管理界面，支持项目编排、实例监控、DriverLab 交互调试及实时遥测。

### 设计目标
1. **标准化与简易性**: 利用标准流 (stdin/stdout) 和 JSONL 建立统一通信规范，降低接入成本。
2. **自描述与发现 (Self-Description)**: Driver 主动声明能力（命令、参数、事件），支持 Host 动态发现与校验。
3. **开发体验优先**: 提供自动文档生成、Console 调试模式及 UI 描述模型，减少重复劳动。
4. **可视化与可观测性**: 通过 WebUI 实现对复杂 Driver 集群的直观管理与实时状态监控。

### 架构分层
- **协议层 (Protocol)**: 基于 stdin/stdout 的 JSONL 流，确保跨平台与无阻塞处理。
- **模型层 (Host-Driver)**: 
    - **Driver**: 独立进程，基于 `IMetaCommandHandler` 实现业务逻辑与元数据导出。
    - **Host**: 进程管理器，提供 `Task` 异步句柄与 `waitAnyNext` 并发调度。
- **元数据层 (Metadata)**: 定义命令 (`Command`)、参数 (`Field`) 与校验规则，驱动文档与 UI 生成。
- **应用层 (Server/WebUI)**: 
    - **Server**: C++ 编写的后端服务，管理项目配置、生命周期及 SSE 实时事件推送。
    - **WebUI**: 基于 React + Ant Design 的前端界面，采用 "Style 06" (Premium Glassmorphism) 设计风格。

## 2. 技术栈
- **后端 (C++)**:
    - 语言: C++17
    - 框架: Qt6 (Core, Network, WebSockets, HttpServer)
    - 日志: spdlog
    - 测试: Google Test (GTest)
- **前端 (WebUI)**:
    - 框架: React 18 + TypeScript + Vite
    - UI 库: Ant Design 5 (Premium Custom Theme)
    - 状态管理: Zustand
    - 通讯: Axios + Server-Sent Events (SSE)
- **构建/部署**:
    - 构建: CMake (>= 3.20) + Ninja
    - 依赖管理: vcpkg (C++), npm (WebUI)
    - 部署: 自研 `publish_release.ps1` 脚本，支持 Qt 插件自动打包与 demo 数据预设。

## 3. 目录结构
```
src/
├── stdiolink/         # 核心基础库 (Protocol, Driver, Host, Console, Doc)
├── stdiolink_server/  # 管理后端服务 (HTTP API, Project Manager, SSE)
├── webui/             # 管理前端应用 (Dashboard, Drivers, DriverLab)
├── tests/             # 单元测试与集成测试 (GTest)
└── demo/              # 示例程序与预设数据 (Calculator, Device Simulator)
tools/                 # 辅助脚本 (Clang-tidy, Publish script, Bridges)
doc/                   # 设计文档、API 参考与里程碑
```

## 4. 构建与运行

### 构建项目 (Windows)
推荐使用 `build.bat` 进行构建：

```powershell
# 首次配置和构建 (Debug)
.\build.bat

# 构建 Release 版本
.\build.bat Release
```

### 完整发布打包
使用发布脚本进行一站式打包（含 WebUI 构建）：
```powershell
.\tools\publish_release.ps1 --demo
```
产物将生成在 `release/stdiolink_<timestamp>_<git>/` 下。

### 运行测试
```powershell
# 运行 C++ 单元测试
.\build\bin\stdiolink_tests.exe
```

## 5. 开发规范与约定

### 代码风格 (Qt 优先)
- **文件 I/O**: 必须使用 `QFile`。
- **文本流**: 使用 `QTextStream` 逐行读取以避免 Windows 管道阻塞。
- **JSON**: 使用 `QJsonDocument` / `QJsonObject`。
- **WebUI 风格**: 遵循 "Style 06" 规范，使用 Glassmorphism (Bento Grid) 布局。

### 提交规范
遵循 Conventional Commits (`feat:`, `fix:`, `docs:`, `style:`, `refactor:`, `test:`, `chore:`)。

## 6. 里程碑状态
- [x] M1-M6: 基础协议、Driver/Host 核心、Console 模式
- [x] M7-M11: 元数据系统、校验、Host 查询
- [x] M12-M33: JS 绑定、异步调度、JS 引擎集成
- [x] M34-M57: Server 架构、项目管理、SSE 事件流、WebSocket 调试
- [x] M58-M69: WebUI 全栈实现、"Style 06" UI 重构、E2E 测试与发布自动化

## 7. 关键文档索引
- `doc/stdiolink_ipc_design.md`: 核心 IPC 协议与传输设计。
- `doc/stdiolink_webui_design.md`: WebUI 视觉与交互设计规范。
- `doc/http_api.md`: Server HTTP API 参考文档。
- `GEMINI.md`: 本项目指南文档。
