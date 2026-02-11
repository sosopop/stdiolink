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

### 设计目标
1. **标准化与简易性**: 利用标准流 (stdin/stdout) 和 JSONL 建立统一通信规范，降低接入成本。
2. **自描述与发现 (Self-Description)**: Driver 主动声明能力（命令、参数、事件），支持 Host 动态发现与校验。
3. **开发体验优先**: 提供自动文档生成、Console 调试模式及 UI 描述模型，减少重复劳动。
4. **灵活的运行模式**: 同时支持机器自动化的 StdIO 模式和人工交互的 Console 模式。

### 架构分层
- **协议层 (Protocol)**: 基于 stdin/stdout 的 JSONL 流，确保跨平台与无阻塞处理。
- **模型层 (Host-Driver)**: 
    - **Driver**: 独立进程，基于 `IMetaCommandHandler` 实现业务逻辑与元数据导出。
    - **Host**: 进程管理器，提供 `Task` 异步句柄与 `waitAnyNext` 并发调度。
- **元数据层 (Metadata)**: 定义命令 (`Command`)、参数 (`Field`) 与校验规则，驱动文档与 UI 生成。
- **适配层 (Adapter)**: `stdiolink/console` 模块负责将命令行参数转换为协议标准 JSON 请求。

## 2. 技术栈
- **语言**: C++17
- **框架**: Qt5 (Core, Widgets)
- **日志**: spdlog
- **测试**: Google Test (GTest)
- **构建**: CMake (>= 3.20) + Ninja
- **依赖管理**: vcpkg

## 3. 目录结构
```
src/
├── stdiolink/         # 基础库
│   ├── protocol/      # JSONL 协议层及元数据定义
│   ├── driver/        # Driver 端核心逻辑与元数据构建
│   ├── host/          # Host 端进程管理与任务调度
│   ├── console/       # Console 模式支持
│   └── doc/           # 文档生成器 (Markdown/OpenAPI/HTML)
├── tests/             # 单元测试 (GTest)
└── demo/              # 示例程序
    ├── host_demo/     # Host 端示例
    ├── echo_driver/   # Echo Driver
    └── progress_driver/ # Progress Driver
tools/                 # 辅助脚本 (clang-tidy, pack headers)
doc/                   # 设计文档与里程碑
```

## 4. 构建与运行

### 构建项目 (Windows)
推荐使用 `build.bat` 进行构建：

```powershell
# 首次配置和构建 (Debug)
.\build.bat

# 构建 Release 版本
.\build.bat Release

# 增量构建
cmake --build build --parallel 8
```
构建产物位于 `build/bin/` 下。

### 运行测试
```powershell
# 运行所有单元测试
.\build\bin\stdiolink_tests.exe

# 运行单个测试套件
.\build\bin\stdiolink_tests.exe --gtest_filter=MetaValidatorTest.*
```

### 静态分析
```powershell
# clang-tidy 检查
python ./tools/run-clang-tidy.py -p build -j 8 -quiet -config-file .clang-tidy
```

## 5. 开发规范与约定

### 代码风格 (Qt 优先)
- **文件 I/O**: 必须使用 `QFile`，严禁使用 `fopen`/`fread`/`fwrite`。
- **文本流**: 使用 `QTextStream`，严禁使用 `fgets`/`fprintf`。
- **字符串**: 使用 `QString` / `QByteArray`。
- **JSON**: 使用 `QJsonDocument` / `QJsonObject` / `QJsonArray`。

### 命名规范
- **命名空间**: `lower_case` (e.g., `stdiolink`)
- **类名**: `CamelCase` (e.g., `DriverCore`)
- **函数/方法**: `camelBack` (e.g., `parseRequest`)
- **成员变量**: `m_` 前缀 + `camelBack` (e.g., `m_driverPath`)
- **局部变量**: `camelBack` (e.g., `driverPath`)

### 已知问题
- **Windows 管道阻塞**: 在 Windows 上，`fread()` 与 `QProcess` 管道配合时会阻塞等待填满缓冲区。**必须**使用 `QTextStream::readLine()` 或 `fgets()` 逐行读取。

### 提交规范
遵循 Conventional Commits：
- `feat: ...` (新功能)
- `fix: ...` (修补 bug)
- `docs: ...` (文档)
- `test: ...` (测试)
- `refactor: ...` (重构)

## 6. 核心 API 示例

### Host 端调用 Driver
```cpp
#include "stdiolink/host/driver.h"
#include "stdiolink/host/wait_any.h"

Driver d;
if (d.start("path/to/driver.exe")) {
    // 发送请求
    Task t = d.request("echo", QJsonObject{{"msg", "hello"}});
    
    // 等待响应
    Message msg;
    while (t.waitNext(msg, 5000)) {
        if (msg.status == "done") {
            // 处理完成
            break;
        }
    }
}
```

### Driver 端定义元数据
```cpp
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"

using namespace stdiolink::meta;

class MyHandler : public IMetaCommandHandler {
public:
    const DriverMeta& driverMeta() const override {
        static DriverMeta meta = DriverMetaBuilder()
            .schemaVersion("1.0")
            .info("com.example.driver", "My Driver", "1.0.0")
            .command(CommandBuilder("echo")
                .description("Echo message")
                .param(FieldBuilder("msg", FieldType::String).required()))
            .build();
        return meta;
    }
    // ... handle() 实现 ...
};
```

## 7. 里程碑状态
- [x] M1-M6: 基础协议、Driver/Host 核心、Console 模式
- [x] M7-M11: 元数据类型、描述、Builder、校验、Host 查询

## 8. 关键文档索引
- `doc/stdiolink_ipc_design.md`: 核心 IPC 协议与传输设计。
- `doc/meta_system_design_v2.md`: 元数据自描述系统详细设计。
- `CLAUDE.md`: 项目速查手册（含 clang-tidy 配置详情）。