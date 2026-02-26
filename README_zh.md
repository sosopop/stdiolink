<p align="right">
  <a href="README.md">English</a> | <a href="README_zh.md">中文</a>
</p>

<p align="center">
  <h1 align="center">stdiolink</h1>
  <p align="center">
    跨平台 IPC 框架 —— 通过 stdin/stdout 和 JSON，将任意可执行程序转化为可管理、可编排的服务。
  </p>
  <p align="center">
    <img src="assets/webui_dashboard_cn.png" width="100%" alt="stdiolink dashboard" />
  </p>
  <p align="center">
    <a href="#快速开始">快速开始</a> &middot;
    <a href="#架构设计">架构设计</a> &middot;
    <a href="#核心特性">核心特性</a> &middot;
    <a href="doc/manual/README.md">用户手册</a> &middot;
    <a href="doc/http_api.md">API 文档</a>
  </p>
</p>

---

## 为什么选择 stdiolink？

大多数 IPC 框架要求你绑定特定的传输协议（gRPC、REST、消息队列）和特定的语言生态。**stdiolink** 采用了一种更简洁的方式：

- **stdin/stdout 是通用接口。** 任何语言、任何运行时、任何平台 —— 只要能读写标准输入输出，就能成为 stdiolink 驱动。
- **JSONL 即协议。** 每行一个 JSON 对象。人类可读，用 `echo` 就能调试，无需编译 schema。
- **驱动自描述。** 每个驱动导出自己的元数据 —— 命令定义、参数 schema、校验规则 —— 自动生成 UI 表单、文档和配置校验，零额外代码。

这意味着你可以将一个 Python 脚本、Rust 程序、Node.js 工具或遗留 C++ 应用包装成一个具备健康监控、定时调度和 Web 管理面板的完整托管服务 —— 无需修改原程序的任何核心逻辑。

## 核心优势

| | 传统 IPC | stdiolink |
|---|---|---|
| **传输层** | TCP/gRPC/WebSocket 配置 | stdin/stdout（零配置） |
| **语言** | 每种语言需要 SDK | 任何能做 I/O 的语言 |
| **服务发现** | 需要注册中心 | 自动扫描 + 元数据导出 |
| **调试** | Wireshark / 专用工具 | `echo '{"cmd":"ping"}' \| ./driver` |
| **UI** | 从零开发 | 从元数据自动生成 |
| **编排** | Kubernetes / systemd | 内置调度器 + 进程守护 |

## 快速开始

### 从源码构建

**前置条件：** Qt 6.6+、CMake 3.20+、vcpkg、C++17 编译器

```bash
# Windows
build.bat Release

# macOS / Linux
./build.sh Release

# 运行测试
./build/bin/stdiolink_tests
```

### 第一个驱动调用（JavaScript）

```javascript
import { openDriver } from "stdiolink";

const calc = await openDriver("./stdio.drv.calculator");

// 代理语法 —— 像调用本地函数一样调用远程命令
const result = await calc.add({ a: 10, b: 20 });
console.log(result);  // { result: 30 }

calc.$close();
```

### 并行驱动

```javascript
import { openDriver } from "stdiolink";

const [drvA, drvB] = await Promise.all([
    openDriver("./stdio.drv.calculator"),
    openDriver("./stdio.drv.calculator"),
]);

const [a, b] = await Promise.all([
    drvA.add({ a: 10, b: 20 }),
    drvB.multiply({ a: 3, b: 4 }),
]);

console.log(a, b);  // { result: 30 } { result: 12 }
drvA.$close();
drvB.$close();
```

### JSONL 协议

通信极其简单 —— 通过 stdin/stdout 传输每行一个 JSON：

```
→  {"cmd":"add","data":{"a":10,"b":20}}
←  {"status":"done","code":0,"data":{"result":30}}
```

四种消息语义：
- `done` — 最终成功结果
- `event` — 中间流式事件
- `error` — 错误响应
- `meta.describe` — 元数据导出

## 架构设计

```
┌─────────────────────────────────────────────────────────┐
│  stdiolink_server          （管控面）                     │
│  REST API · SSE · WebSocket · 调度引擎 · 进程守护        │
├─────────────────────────────────────────────────────────┤
│  stdiolink_service         （JS 运行时）                  │
│  QuickJS 引擎 · ES Modules · C++ 绑定                   │
├─────────────────────────────────────────────────────────┤
│  stdiolink                 （核心库）                     │
│  JSONL 协议 · Driver/Host · 元数据 · 参数校验            │
└─────────────────────────────────────────────────────────┘
         │                          │
    ┌────┴────┐              ┌──────┴──────┐
    │ Driver  │  stdin/stdout │   Driver    │
    │ (C++)   │◄────────────►│ (任意语言)   │
    └─────────┘              └─────────────┘
```

### 三级服务模型

```
Service（模板） →  Project（配置） →  Instance（运行实例）
```

- **Service**：通过自动扫描发现的可复用驱动或 JS 脚本模板
- **Project**：基于 Service 的具体配置部署
- **Instance**：受生命周期管理、健康检查和日志采集管控的运行进程

## 核心特性

### 自描述元数据

驱动通过丰富的元数据系统声明自身能力：

```cpp
DriverMetaBuilder()
    .id("stdio.drv.modbus_tcp")
    .name("Modbus TCP Driver")
    .version("1.0.0")
    .addCommand(CommandBuilder("read_registers")
        .description("Read holding registers")
        .addParam(FieldBuilder("host").type(FieldType::String).required())
        .addParam(FieldBuilder("port").type(FieldType::Int).defaultValue(502))
        .addParam(FieldBuilder("count").type(FieldType::Int).range(1, 125)))
    .build();
```

元数据驱动的能力：
- 自动参数校验（类型检查 + 范围约束）
- UI 表单自动生成（WebUI 仅凭元数据即可渲染交互表单）
- OpenAPI 兼容的文档导出
- 配置 Schema 强制校验

![Schema Editor](assets/webui_schema_editor.png)

### 内置 JavaScript 运行时

使用 JavaScript 编写服务，通过 C++ 绑定获得完整的系统能力：

```javascript
import { openDriver, waitAny } from "stdiolink";
import { writeJson } from "stdiolink/fs";
import { createLogger } from "stdiolink/log";

const logger = createLogger({ service: "pipeline" });
const drv = await openDriver("./stdio.drv.sensor");

const task = drv.$rawRequest("read", { channel: 1 });
const result = await waitAny([task], 5000);

writeJson("./output/report.json", result.msg.data);
logger.info("done", { status: result.msg.status });
```

可用 JS 模块：`stdiolink`、`stdiolink/fs`、`stdiolink/path`、`stdiolink/log`、`stdiolink/time`、`stdiolink/constants`

### 服务管理 & REST API

完整的 HTTP API，支持程序化控制：

```bash
# 列出所有服务
curl http://localhost:6200/api/services

# 从服务创建项目
curl -X POST http://localhost:6200/api/projects \
  -d '{"serviceId":"modbus_tcp","name":"Factory Floor","config":{...}}'

# 启动实例
curl -X POST http://localhost:6200/api/projects/factory-floor/start

# 通过 SSE 接收实时事件
curl http://localhost:6200/api/events
```

### 调度引擎

三种内置调度策略：
- **Manual** — 按需启停
- **Interval** — 可配置间隔的周期执行
- **Cron** — 基于 Cron 表达式的定时调度

### 现代化 WebUI

基于 React 18 + TypeScript 构建的生产级管理面板：

![Console Dashboard](assets/webui_console_dark.png)

- **Mission Control 仪表盘** — 所有服务、项目和实例的实时总览
- **DriverLab** — 通过 WebSocket 代理与运行中驱动进程交互调试
- **Schema 编辑器** — 基于 Monaco 的可视化配置编辑器，从驱动元数据自动生成
- **9 种语言** — 通过 i18next 实现国际化
- **Glassmorphism 设计** — "Style 06" 高级视觉语言，Bento Grid 布局

![Driver Lab](assets/webui_driver_lab.png)

![Create Project](assets/webui_create_project.png)

### 进程守护

生产级进程管理：
- 自动崩溃检测与可配置重启策略
- 心跳健康监控
- 异步服务器操作的竞态条件防护
- 带清理保证的优雅关闭

![Service Logs](assets/webui_service_logs.png)

## 项目结构

```
stdiolink/
├── src/
│   ├── stdiolink/            # 核心库 — 协议、元数据、Driver/Host
│   ├── stdiolink_service/    # JS 运行时 — QuickJS 引擎 + C++ 绑定
│   ├── stdiolink_server/     # 管控面 — REST API、SSE、调度器
│   │   ├── manager/          #   项目、实例、调度管理器
│   │   ├── http/             #   HTTP 路由、SSE、WebSocket 代理
│   │   └── scanner/          #   服务与驱动自动发现
│   ├── webui/                # React 前端（Vite + Ant Design）
│   └── drivers/              # 示例驱动（ModbusTCP、ModbusRTU、3DVision）
├── examples/                 # JavaScript 使用示例
├── doc/
│   ├── manual/               # 完整用户手册
│   └── http_api.md           # REST API 参考
├── tools/                    # 构建与发布脚本
├── build.bat / build.sh      # 平台构建脚本
└── CMakeLists.txt
```

## 技术栈

| 层级 | 技术 |
|---|---|
| 核心库 | C++17, Qt 6.6+（Core, Network, WebSockets, HttpServer） |
| JS 运行时 | QuickJS-NG, ES Modules |
| 构建 | CMake 3.20+, vcpkg |
| 测试 | Google Test, Vitest, Playwright |
| 前端 | React 18, TypeScript, Vite, Ant Design, Monaco Editor |
| 状态管理 | Zustand |
| 可视化 | Recharts |

## 发布打包

```powershell
# Windows
.\tools\publish_release.ps1 --name stdiolink_v1.0

# macOS / Linux
./tools/publish_release.sh --name stdiolink_v1.0
```

发布包包含：
- 服务端和运行时二进制文件
- 自动发现的驱动（`data_root/drivers/`）
- 预置的演示服务和项目
- 打包的 WebUI
- 启动脚本（`start.bat` / `start.sh`）

## 文档

- [用户手册](doc/manual/README.md) — 涵盖协议、Driver/Host 开发、JS 运行时和服务管理的完整指南
- [HTTP API 参考](doc/http_api.md) — 完整的 REST API 文档
- [架构概述](doc/manual/03-architecture.md) — 系统设计与数据流

## 参与贡献

欢迎贡献！请遵循以下规范：

- **提交信息**：[Conventional Commits](https://www.conventionalcommits.org/)（`feat:`、`fix:`、`docs:` 等）
- **命名规范**：类 `CamelCase`、方法 `camelBack`、成员 `m_` 前缀
- **Qt 优先**：文件 I/O 使用 `QFile`，JSON 使用 `QJsonObject`，管道读取使用 `QTextStream`
- **无阻塞 I/O**：Windows 管道读取必须使用 `QTextStream::readLine()`

## 许可证

详见 [LICENSE](LICENSE)。
