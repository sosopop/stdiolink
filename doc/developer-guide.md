# stdiolink 开发指南

> 快速上手指南，帮助新开发者理解项目架构并开始开发 Driver、Service 和 Project

## 1. 项目背景

stdiolink 是基于 Qt 的跨平台 IPC 框架，使用 JSONL 协议通过 stdin/stdout 实现进程间通信。

**核心特点**：
- **进程隔离**：Driver 作为独立进程运行，崩溃不影响主进程
- **协议简单**：JSONL（每行一个 JSON），易于调试和跨语言集成
- **自描述**：Driver 通过元数据自动生成文档、表单和 API
- **JS 运行时**：内置 QuickJS 引擎，支持 ES Module 编写业务逻辑
- **管控后端**：Server 层提供项目管理、实例编排、REST API 和 WebUI

## 2. 架构概览

### 2.1 三层架构

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

### 2.2 核心概念

- **Driver**：独立进程，执行具体任务（如 Modbus 通信、PLC 控制）
- **Service**：JS 脚本，编排多个 Driver 完成复杂业务流程
- **Project**：配置文件，定义 Service 实例及其参数
- **Host**：主控进程，启动并管理 Driver 子进程

### 2.3 核心抽象

- **DriverCore**：Driver 端主类，支持 `OneShot` / `KeepAlive` 生命周期
- **Driver**（Host 端）：管理 Driver 子进程，处理异步通讯与进程早退检测
- **Task**：Future/Promise 风格句柄，支持 `waitAnyNext()` 并发调度
- **DriverMeta**：自描述元数据，支持配置校验、自动表单生成与 OpenAPI 文档导出
- **ServerManager**：编排层，管理 `Service` (模板) → `Project` (配置) → `Instance` (进程) 的全生命周期

### 2.4 通信协议

进程间通过 stdin/stdout 传输 JSONL，四种消息语义：
- `done` — 最终执行成功结果（替代旧版 `ok`）
- `event` — 中间流式事件（命令执行过程中的增量推送）
- `error` — 错误响应
- `meta.describe` — 元数据导出

请求格式：`{"cmd":"command_name","data":{...}}`

### 2.5 目录布局

构建产物采用 **raw + runtime 两级布局**：

```
build/
├── debug/                  # CMake 原始输出（仅编译产物）
├── runtime_debug/          # 组装后的运行时目录
│   ├── bin/                # 核心二进制 + Qt 插件
│   ├── data_root/
│   │   ├── drivers/        # 驱动按子目录组织（stdio.drv.*）
│   │   ├── services/       # Service 模板
│   │   ├── projects/       # Project 配置
│   │   └── logs/           # 日志文件
│   ├── demos/
│   └── scripts/
└── runtime_release/        # Release 构建同构布局
```

**关键特性**：
- CMake `RUNTIME_OUTPUT_DIRECTORY` 统一指向 `build/<config>/`（raw dir）
- `assemble_runtime` target 自动组装 raw → runtime，驱动按 `data_root/drivers/<name>/` 子目录分发
- 开发环境与发布包目录结构同构（isomorphic layout）

### 2.6 关键模块

**`src/stdiolink/`** - 核心协议与基础库
- 元数据 Builder、Validator 及文档生成器
- JSONL 协议解析与序列化
- Driver/Host 通信框架

**`src/stdiolink_server/`** - 管控后端
- `manager/`：项目管理 (`ProjectManager`)、实例管理 (`InstanceManager`)、调度引擎 (`ScheduleEngine`)
- `http/`：REST API 路由、SSE 事件推送、DriverLab WebSocket 代理
- `scanner/`：Service 与 Driver 的自动扫描发现

**`src/webui/`** - React 18 + TypeScript + Vite 前端
- **设计规范**: "Style 06" (Premium Glassmorphism)，采用 Bento Grid 布局
- **核心组件**: Dashboard (Mission Control)、DriverLab (交互调试)、SchemaEditor (可视配置)
- **国际化**: 支持 9 种语言 (i18next)

## 3. 开发 Driver（C++）

Driver 是执行具体任务的工作进程，使用 C++ 开发。

### 3.1 最小示例

**步骤 1：实现命令处理器**

```cpp
// handler.h
#include "stdiolink/driver/icommand_handler.h"

class EchoHandler : public stdiolink::ICommandHandler {
public:
    void handle(const QString& cmd,
                const QJsonValue& data,
                stdiolink::IResponder& resp) override
    {
        if (cmd == "echo") {
            QString msg = data.toObject()["msg"].toString();
            resp.done(0, QJsonObject{{"echo", msg}});
        } else {
            resp.error(404, QJsonObject{{"message", "unknown command"}});
        }
    }
};
```

**步骤 2：创建 main 函数**

```cpp
// main.cpp
#include <QCoreApplication>
#include "stdiolink/driver/driver_core.h"
#include "handler.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    EchoHandler handler;
    stdiolink::DriverCore core;
    core.setHandler(&handler);

    return core.run(argc, argv);
}
```

**步骤 3：CMakeLists.txt**

```cmake
add_executable(stdio.drv.echo main.cpp handler.cpp)
target_link_libraries(stdio.drv.echo PRIVATE stdiolink)

# 设置输出名称（必须以 stdio.drv. 开头才能被 Server 自动扫描）
set_target_properties(stdio.drv.echo PROPERTIES OUTPUT_NAME "stdio.drv.echo")
```

### 3.2 添加元数据

使用 `MetaBuilder` 定义命令元数据，支持自动文档生成和参数校验：

```cpp
#include "stdiolink/meta/meta_builder.h"

stdiolink::meta::DriverMeta buildMeta() {
    using namespace stdiolink::meta;

    return DriverMetaBuilder()
        .info("echo", "1.0.0", "Echo Driver", "Vendor")
        .command("echo")
            .title("回显消息")
            .param("msg", FieldType::String)
                .required()
                .description("要回显的消息")
                .done()
            .returns()
                .field("echo", FieldType::String)
                    .description("回显结果")
                    .done()
                .done()
            .done()
        .build();
}

// 在 main 中设置元数据
core.setMeta(buildMeta());
```

### 3.3 目录结构

```
src/drivers/driver_echo/
├── CMakeLists.txt
├── main.cpp
├── handler.h
└── handler.cpp
```

### 3.4 Driver 单独运行

Driver 可执行文件依赖 `build/runtime_debug/bin` 下的运行时 DLL（Windows）或共享库（Linux/macOS）。

**Windows 示例**：
```powershell
# 添加 bin 目录到 PATH
$env:PATH = "$PWD\build\runtime_debug\bin;$env:PATH"

# Driver 实际路径
.\build\runtime_debug\data_root\drivers\stdio.drv.echo\stdio.drv.echo.exe --export-meta
```

**Linux/macOS 示例**：
```bash
# 设置 LD_LIBRARY_PATH (Linux) 或 DYLD_LIBRARY_PATH (macOS)
export LD_LIBRARY_PATH=$PWD/build/runtime_debug/bin:$LD_LIBRARY_PATH

# 运行 Driver
./build/runtime_debug/data_root/drivers/stdio.drv.echo/stdio.drv.echo --export-meta
```

**推荐方式**：使用 `dev.bat` / `dev.ps1` 自动配置环境（见第 7.3 节）。

## 4. 开发 Service（JavaScript）

Service 是 JS 脚本，用于编排多个 Driver 完成复杂业务流程。

### 4.1 最小示例

**目录结构**：
```
src/data_root/services/my_service/
├── index.js          # 服务主逻辑
└── meta.json         # 服务元数据（可选）
```

**index.js 示例**：
```js
import { openDriver } from "stdiolink";
import { getConfig } from "stdiolink";
import { createLogger } from "stdiolink/log";
import { resolveDriver } from "stdiolink/driver";

const cfg = getConfig();
const logger = createLogger({ service: "my_service" });

(async () => {
    logger.info("service starting", cfg);

    // 方式 1：通过路径打开 Driver
    const driverPath = resolveDriver("stdio.drv.echo");
    const drv = await openDriver(driverPath);

    // 方式 2：通过 Project 配置的驱动引用（推荐）
    // const drv = await resolveDriver(cfg.driver_ref);

    // 调用 Driver 命令
    const result = await drv.echo({ msg: "Hello from Service" });
    logger.info("driver response", result);

    // keepalive 模式：不调用 $close()，保持运行
    logger.info("service running...");
})();
```

### 4.2 常用 API

**Driver 操作**：
```js
import { openDriver, resolveDriver } from "stdiolink";

// 打开 Driver
const drv = await openDriver("/path/to/driver");

// 调用命令
const result = await drv.commandName({ param1: "value" });

// 关闭 Driver（oneshot 模式）
await drv.$close();
```

**配置读取**：
```js
import { getConfig } from "stdiolink";

const cfg = getConfig();
const port = Number(cfg.listen_port ?? 502);
```

**日志输出**：
```js
import { createLogger } from "stdiolink/log";

const logger = createLogger({ service: "my_service" });
logger.info("message", { key: "value" });
logger.warn("warning");
logger.error("error", new Error("details"));
```

**时间工具**：
```js
import { sleep } from "stdiolink/time";

await sleep(1000); // 延迟 1 秒
```

### 4.3 编排多个 Driver

```js
import { openDriver, resolveDriver } from "stdiolink";

(async () => {
    // 打开多个 Driver
    const crane = await openDriver(resolveDriver("stdio.drv.plc_crane"));
    const vision = await openDriver(resolveDriver("stdio.drv.3dvision"));

    // 步骤 1：Crane 移动到扫描位置
    await crane.set_mode({ mode: "auto" });
    const status = await crane.read_status({});
    logger.info("crane status", status);

    // 步骤 2：触发 3D 扫描
    const scanResult = await vision.vessel_command({
        cmd: "scan",
        id: 15
    });
    logger.info("scan result", scanResult);
})();
```

## 5. 配置 Project

Project 是 JSON 配置文件，定义 Service 实例及其运行参数。

### 5.1 配置格式

**位置**：`src/data_root/projects/my_project.json`

**示例**：
```json
{
  "name": "My Modbus Server",
  "serviceId": "modbustcp_server_service",
  "enabled": true,
  "schedule": {
    "type": "manual"
  },
  "config": {
    "listen_port": 502,
    "unit_ids": "1,2,3",
    "data_area_size": 10000,
    "event_mode": "write"
  }
}
```

### 5.2 配置字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | string | 项目显示名称 |
| `serviceId` | string | Service 目录名（对应 `services/` 下的目录） |
| `enabled` | boolean | 是否启用 |
| `schedule.type` | string | 调度类型：`manual`（手动）、`auto`（自动启动） |
| `config` | object | 传递给 Service 的配置参数（通过 `getConfig()` 读取） |

### 5.3 调度类型

- **manual**：手动启动，通过 WebUI 或 API 控制
- **auto**：Server 启动时自动启动 Service

## 6. 测试方法

### 6.1 单元测试（C++ Driver）

使用 GTest 框架：

```cpp
// src/tests/test_echo_driver.cpp
#include <gtest/gtest.h>
#include "handler.h"

TEST(EchoDriver, BasicEcho) {
    EchoHandler handler;
    MockResponder resp;

    handler.handle("echo", QJsonObject{{"msg", "test"}}, resp);

    EXPECT_EQ(resp.status, "done");
    EXPECT_EQ(resp.data["echo"].toString(), "test");
}
```

运行测试：
```bash
./build/runtime_debug/bin/stdiolink_tests
```

### 6.2 单元测试（JS Service）

使用 Vitest 框架（WebUI 测试）：

```bash
cd src/webui
npm run test
```

### 6.3 端到端测试（Playwright）

```bash
cd src/webui
npx playwright test
```

### 6.4 冒烟测试（Python）

冒烟测试验证端到端流程：

```bash
# 运行单个测试
python src/smoke_tests/m94_server_run_oneshot.py

# 运行所有测试
python src/smoke_tests/run_smoke.py --plan all

# 使用 CTest 统一入口
ctest --test-dir build --output-on-failure
ctest --test-dir build -L smoke --output-on-failure
```

**测试脚本工具**：
```bash
# 选择性运行测试套件
tools/run_tests.sh                # 全部执行（GTest + Vitest + Playwright）
tools/run_tests.sh --gtest        # 仅 C++ 单元测试
tools/run_tests.ps1 --vitest --playwright  # 仅 WebUI 测试
```

**新增功能测试要求**：
- 新增/更新 `src/smoke_tests/mXX_*.py`
- 在 `src/smoke_tests/run_smoke.py` 注册
- 在 `src/smoke_tests/CMakeLists.txt` 注册对应 CTest

### 6.5 手动测试 Driver

**Console 模式**（命令行交互）：
```bash
# 导出元数据
./stdio.drv.echo --export-meta

# 执行命令
./stdio.drv.echo --cmd=echo --msg="Hello"
```

**Stdio 模式**（JSONL 协议）：
```bash
echo '{"cmd":"echo","data":{"msg":"Hello"}}' | ./stdio.drv.echo --mode=stdio --profile=oneshot
```

## 7. 快速开始流程

### 7.1 构建项目

```bash
# Windows
build.bat Release

# Linux/macOS
./build.sh Release

# 运行测试
ctest --test-dir build --output-on-failure
```

### 7.2 发布打包

使用 `tools/publish_release.ps1` (Windows) 或 `tools/publish_release.sh` (Unix) 创建发布包。

**基本用法**：
```powershell
# Windows
.\tools\publish_release.ps1 --name stdiolink_v1.0

# 快速打包（跳过测试和 WebUI）
.\tools\publish_release.ps1 --skip-tests --skip-webui

# 自定义构建目录
.\tools\publish_release.ps1 --build-dir build_release --output-dir release
```

**发布流程**：
1. 执行 C++ 构建（Release 模式）
2. 构建 WebUI（npm ci + npm run build）
3. 运行测试套件（GTest + Vitest + Playwright + 冒烟测试）
4. 组装发布包（bin + data_root + 启动脚本）
5. 生成 RELEASE_MANIFEST.txt
6. 执行重复组件检查

**发布包结构**：
```
release/stdiolink_<timestamp>_<git>/
├── bin/                    # 可执行文件和 DLL
├── data_root/              # 数据目录
│   ├── drivers/            # Driver 子目录
│   ├── services/           # Service 模板
│   ├── projects/           # Project 配置
│   ├── webui/              # WebUI 静态文件
│   └── config.json         # 默认配置
├── start.bat / start.ps1   # 启动脚本
├── dev.bat / dev.ps1       # 开发环境脚本
└── RELEASE_MANIFEST.txt    # 发布清单
```

### 7.3 启动 Server

```bash
cd build/runtime_release
./start.bat  # Windows
./start.sh   # Linux/macOS

# 或指定端口
./start.bat --port=8080
```

访问 WebUI：`http://localhost:6200`

### 7.3 开发环境

使用 `dev.bat` / `dev.ps1` 启动开发环境，自动配置 PATH 和 Driver 别名：

```bash
# Windows (PowerShell)
cd build/runtime_debug
.\dev.ps1

# 列出所有 Driver
drivers

# 直接运行 Driver（已配置别名）
stdio.drv.echo --export-meta
```

### 7.4 典型开发流程

1. **开发 Driver**：
   - 在 `src/drivers/driver_xxx/` 创建目录
   - 实现 `ICommandHandler` 和 `main.cpp`
   - 添加 `CMakeLists.txt`
   - 构建并测试

2. **开发 Service**：
   - 在 `src/data_root/services/xxx_service/` 创建目录
   - 编写 `index.js` 使用 `openDriver` 编排 Driver
   - 本地测试

3. **配置 Project**：
   - 在 `src/data_root/projects/` 创建 JSON 配置
   - 通过 WebUI 或 API 启动实例

4. **测试验证**：
   - 编写单元测试（GTest / Vitest）
   - 编写冒烟测试（Python）
   - 端到端验证

## 8. WebUI 集成

### 8.1 REST API

Server 提供 REST API 管理 Service 实例：

```bash
# 列出所有 Service
GET http://localhost:6200/api/services

# 列出所有 Project
GET http://localhost:6200/api/projects

# 启动实例
POST http://localhost:6200/api/instances
{"projectId": "my_project"}

# 停止实例
DELETE http://localhost:6200/api/instances/{instanceId}

# 查询实例状态
GET http://localhost:6200/api/instances/{instanceId}
```

### 8.2 DriverLab（交互调试）

WebUI 提供 DriverLab 页面，可视化调试 Driver：
- 选择 Driver
- 填写命令参数（自动生成表单）
- 执行命令并查看响应
- 支持 WebSocket 实时事件流

### 8.3 前端 API 调用

```typescript
// src/webui/src/api/services.ts
import { apiClient } from './client';

export async function listServices() {
  return apiClient.get('/api/services');
}

export async function startInstance(projectId: string) {
  return apiClient.post('/api/instances', { projectId });
}
```

## 9. 开发最佳实践

### 9.1 Driver 开发

- **命名规范**：可执行文件必须以 `stdio.drv.` 开头（如 `stdio.drv.modbustcp`）
- **元数据优先**：使用 `MetaBuilder` 定义完整元数据，支持自动文档和表单生成
- **错误处理**：使用 `resp.error()` 返回结构化错误，包含错误码和消息
- **生命周期**：
  - `OneShot`：执行一次命令后退出
  - `KeepAlive`：持续运行，处理多个命令
- **Qt 优先**：文件 I/O 使用 `QFile`，JSON 使用 `QJsonObject`

### 9.2 Service 开发

- **配置驱动**：通过 `getConfig()` 读取配置，避免硬编码
- **日志规范**：使用 `createLogger()` 输出结构化日志
- **错误处理**：使用 try-catch 捕获异常，记录详细错误信息
- **资源清理**：oneshot 模式需调用 `drv.$close()` 关闭 Driver
- **异步编排**：使用 `async/await` 编排多个 Driver 调用

### 9.3 测试规范

- **单元测试覆盖**：核心决策路径 100% 覆盖
- **冒烟测试必备**：每个里程碑必须提供冒烟测试脚本
- **失败路径测试**：使用 Mock/Stub 控制失败场景，本地可复现
- **测试隔离**：每个测试用例独立，不依赖执行顺序

### 9.4 代码规范

**命名规范**：
- 类名：`CamelCase`（如 `DriverCore`、`MetaBuilder`）
- 方法名：`camelBack`（如 `setHandler`、`buildMeta`）
- 成员变量：`m_` 前缀（如 `m_handler`、`m_state`）
- 常量：`UPPER_SNAKE_CASE`（如 `MAX_RETRY_COUNT`）

**提交规范**：
遵循 Conventional Commits：
```
feat: 添加新功能
fix: 修复 bug
docs: 文档更新
refactor: 重构代码
test: 测试相关
chore: 构建/工具链相关
```

示例：
```
feat(driver): 添加 Modbus RTU 从站驱动
fix(service): 修复 WS 重连失败问题
docs: 更新开发指南
```

**技术约束**：
- **Qt 优先**：文件 I/O 使用 `QFile`，JSON 使用 `QJsonObject`
- **无阻塞 I/O**：Windows 管道读取必须使用 `QTextStream::readLine()`
- **资源管理**：使用 RAII 模式，避免手动 delete
- **错误处理**：使用结构化错误响应，包含错误码和消息

## 10. 常见问题

### 10.1 Driver 找不到

**问题**：Service 中 `resolveDriver("stdio.drv.xxx")` 失败

**解决**：
- 确认 Driver 可执行文件名以 `stdio.drv.` 开头
- 确认 Driver 位于 `data_root/drivers/` 目录下
- 检查 PATH 环境变量是否包含 `bin/` 目录

### 10.2 Windows 管道阻塞

**问题**：Driver 在 Windows 上读取 stdin 阻塞

**解决**：
- 使用 `QTextStream::readLine()` 而非 `std::cin`
- 避免使用阻塞 I/O，参考 `DriverCore` 实现

### 10.3 Service 启动失败

**问题**：Project 配置后无法启动

**解决**：
- 检查 `serviceId` 是否匹配 `services/` 目录名
- 检查 `config` 参数是否符合 Service 要求
- 查看 Server 日志：`data_root/logs/server.log`

### 10.4 测试失败

**问题**：冒烟测试找不到可执行文件

**解决**：
- 确认已执行构建：`build.bat Release`
- 检查产物路径：`build/runtime_release/bin/`
- 使用 `assemble_runtime` target 组装运行时目录

## 11. 参考资源

### 11.1 项目进展

**已完成里程碑**：
- **M1-M33**: 核心协议、元数据、JS Runtime 及其全量 C++ 绑定
- **M34-M48**: Server 架构、项目生命周期管理、SSE/WebSocket 通讯
- **M49-M69**: WebUI 全量实现、"Style 06" 视觉重构、E2E 测试、发布脚本完善
- **M70-M85**: Modbus 驱动族（TCP/RTU/Serial 主从站）、PLC 升降装置驱动、Service 模板
- **M86-M89**: resolveDriver 绑定、shared 目录移除、统一 runtime 布局与同构目录结构

### 11.2 文档资源

- **详细文档**：`doc/manual/` 目录包含完整手册
- **示例代码**：
  - Driver 示例：`src/drivers/driver_modbustcp_server/`
  - Service 示例：`src/data_root/services/modbustcp_server_service/`
  - Project 示例：`src/data_root/projects/manual_modbustcp_server.json`
- **测试示例**：
  - 单元测试：`src/tests/test_driver_core.cpp`
  - 冒烟测试：`src/smoke_tests/m94_server_run_oneshot.py`
- **架构文档**：`doc/manual/03-architecture.md`
- **JS Service API**：`doc/manual/10-js-service/`

## 12. 下一步

1. **阅读架构文档**：理解三层架构和通信协议
2. **运行示例**：启动 Server，在 WebUI 中运行示例 Project
3. **开发第一个 Driver**：参考 `driver_modbustcp_server` 实现简单 Driver
4. **编写 Service**：使用 JS 编排 Driver 完成业务流程
5. **编写测试**：为新功能添加单元测试和冒烟测试

---

**快速链接**：
- 构建：`build.bat Release` / `./build.sh Release`
- 启动：`cd build/runtime_release && ./start.bat`
- 测试：`ctest --test-dir build --output-on-failure`
- WebUI：`http://localhost:6200`
