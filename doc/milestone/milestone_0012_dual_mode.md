# 里程碑 12：DriverCore 双模式集成

## 1. 目标

让 DriverCore 自动解析命令行参数，同时支持 Console 模式和 Stdio 模式，实现"开箱即用"的双模式运行。

## 2. 问题分析

### 2.1 当前状态

- `ConsoleArgs` 已实现参数解析
- `ConsoleResponder` 已实现 Console 输出
- `DriverCore::run()` 只支持 Stdio 模式
- 用户需要手动编写模式切换逻辑

### 2.2 期望行为

```bash
# Stdio 模式（默认，无参数时）
echo '{"cmd":"scan"}' | ./driver.exe

# Console 模式（有 --cmd 参数时）
./driver.exe --cmd=scan --fps=30

# 显式指定模式
./driver.exe --mode=stdio
./driver.exe --mode=console --cmd=scan
```

## 3. 技术设计

### 3.1 运行模式枚举

```cpp
enum class RunMode {
    Auto,     // 自动检测
    Stdio,    // 强制 Stdio 模式
    Console   // 强制 Console 模式
};
```

### 3.2 自动检测规则

1. 如果有 `--export-meta` 或 `--export-doc` → Console 模式（导出后退出）
2. 如果有 `--help` 或 `--version` → Console 模式（显示信息后退出）
3. 如果有 `--mode=stdio` → Stdio 模式
4. 如果有 `--mode=console` → Console 模式
5. 如果有 `--cmd=xxx` → Console 模式
6. 默认 → Stdio 模式（**仍解析命令行参数用于合并**）

### 3.3 DriverCore 扩展

```cpp
class DriverCore {
public:
    // 新增：带命令行参数的运行入口
    int run(int argc, char* argv[]);

    // 原有：纯 Stdio 模式入口（保持兼容）
    int run();

private:
    int runStdioMode();
    int runConsoleMode(const ConsoleArgs& args);
    RunMode detectMode(const ConsoleArgs& args);
};
```

## 4. 实现步骤

### 4.1 扩展 DriverCore

```cpp
int DriverCore::run(int argc, char* argv[]) {
    ConsoleArgs args;
    if (!args.parse(argc, argv)) {
        // 解析失败，输出错误
        QFile out;
        out.open(stderr, QIODevice::WriteOnly);
        out.write(args.errorMessage.toUtf8());
        out.write("\n");
        return 1;
    }

    // 若无参数且 stdin 为交互终端，可选择输出帮助（可配置开关）
    if (argc == 1 && ConsoleArgs::isInteractiveStdin()) {
        printHelp();
        return 0;
    }

    // 处理 --help
    if (args.showHelp) {
        printHelp();
        return 0;
    }

    // 处理 --version
    if (args.showVersion) {
        printVersion();
        return 0;
    }

    // 检测运行模式
    RunMode mode = detectMode(args);

    if (mode == RunMode::Console) {
        return runConsoleMode(args);
    } else {
        return runStdioMode();
    }
}
```

### 4.4 参数合并策略（新增）

**目标**：允许命令行参数与 stdio 请求同时传递，并合并成最终 data。

合并规则：
1. CLI 解析出的 `args.data` 作为 **默认值**（base data）
2. 每条 stdio 请求的 `req.data` **覆盖** CLI 同名字段
3. 对象字段执行 **深度合并**（递归合并子字段）
4. 数组字段若同时存在，**请求端覆盖 CLI**（不合并数组元素）

示例：

```bash
# CLI 提供默认参数
./driver.exe --fps=30 --mode=fast

# stdio 请求覆盖 fps
{"cmd":"scan","data":{"fps":10}}
```

最终 data：`{"fps":10,"mode":"fast"}`

实现建议：
- 在 `runStdioMode()` 中为每条请求调用 `mergeData(cliData, req.data)`
- 若 `--cmd` 仅用于 console 模式，stdio 模式忽略 `--cmd`

### 4.2 Console 模式执行

```cpp
int DriverCore::runConsoleMode(const ConsoleArgs& args) {
    if (args.cmd.isEmpty()) {
        // 无命令，显示帮助
        printHelp();
        return 1;
    }

    ConsoleResponder responder;

    // 处理 meta 命令
    if (handleMetaCommand(args.cmd, args.data, responder)) {
        return responder.exitCode();
    }

    // 自动参数验证（如果启用）
    QJsonValue data = args.data;
    if (m_metaHandler && m_metaHandler->autoValidateParams()) {
        const auto* cmdMeta = m_metaHandler->driverMeta().findCommand(args.cmd);
        if (cmdMeta) {
            QJsonObject filledData = meta::DefaultFiller::fillDefaults(
                args.data, *cmdMeta);
            auto result = meta::MetaValidator::validateParams(filledData, *cmdMeta);
            if (!result.valid) {
                responder.error(400, QJsonObject{
                    {"name", "ValidationFailed"},
                    {"message", result.toString()}
                });
                return responder.exitCode();
            }
            data = filledData;
        }
    }

    // 执行命令
    m_handler->handle(args.cmd, data, responder);
    return responder.exitCode();
}
```

补充：若检测到 `--export-meta` 或 `--export-doc`，Console 模式应直接走导出逻辑并返回，不进入 `handle()`（由里程碑 13/14 实现）。

### 4.3 模式检测

```cpp
RunMode DriverCore::detectMode(const ConsoleArgs& args) {
    // 显式指定
    if (args.mode == "stdio") return RunMode::Stdio;
    if (args.mode == "console") return RunMode::Console;

    // 自动检测
    if (!args.cmd.isEmpty()) return RunMode::Console;
    if (args.showHelp || args.showVersion) return RunMode::Console;

    return RunMode::Stdio;
}
```

## 5. 验收标准

1. `./driver.exe` 进入 Stdio 模式
2. `./driver.exe --cmd=scan` 进入 Console 模式
3. `./driver.exe --mode=stdio` 强制 Stdio 模式
4. `./driver.exe --mode=console --cmd=scan` 强制 Console 模式
5. Console 模式下参数验证正常工作
6. Console 模式下默认值填充正常工作
7. 原有 `run()` 接口保持兼容

## 6. 单元测试用例

### 6.1 测试文件：tests/test_dual_mode.cpp

```cpp
#include <gtest/gtest.h>
#include "stdiolink/driver/driver_core.h"
#include "stdiolink/console/console_args.h"

using namespace stdiolink;

class DualModeTest : public ::testing::Test {};

// 测试默认 Stdio 模式检测
TEST_F(DualModeTest, DetectStdioByDefault) {
    ConsoleArgs args;
    char* argv[] = {const_cast<char*>("prog")};
    EXPECT_TRUE(args.parse(1, argv));
    EXPECT_TRUE(args.cmd.isEmpty());
    EXPECT_TRUE(args.mode.isEmpty());
}

// 测试 --cmd 触发 Console 模式
TEST_F(DualModeTest, DetectConsoleWithCmd) {
    ConsoleArgs args;
    char* argv[] = {const_cast<char*>("prog"), const_cast<char*>("--cmd=scan")};
    EXPECT_TRUE(args.parse(2, argv));
    EXPECT_EQ(args.cmd, "scan");
}

// 测试显式 --mode=stdio
TEST_F(DualModeTest, ExplicitStdioMode) {
    ConsoleArgs args;
    char* argv[] = {const_cast<char*>("prog"), const_cast<char*>("--mode=stdio")};
    EXPECT_TRUE(args.parse(2, argv));
    EXPECT_EQ(args.mode, "stdio");
}

// 测试显式 --mode=console
TEST_F(DualModeTest, ExplicitConsoleMode) {
    ConsoleArgs args;
    char* argv[] = {
        const_cast<char*>("prog"),
        const_cast<char*>("--mode=console"),
        const_cast<char*>("--cmd=scan")
    };
    EXPECT_TRUE(args.parse(3, argv));
    EXPECT_EQ(args.mode, "console");
    EXPECT_EQ(args.cmd, "scan");
}

// 测试 --help 标志
TEST_F(DualModeTest, HelpFlag) {
    ConsoleArgs args;
    char* argv[] = {const_cast<char*>("prog"), const_cast<char*>("--help")};
    EXPECT_TRUE(args.parse(2, argv));
    EXPECT_TRUE(args.showHelp);
}

// 测试 --version 标志
TEST_F(DualModeTest, VersionFlag) {
    ConsoleArgs args;
    char* argv[] = {const_cast<char*>("prog"), const_cast<char*>("--version")};
    EXPECT_TRUE(args.parse(2, argv));
    EXPECT_TRUE(args.showVersion);
}

// 测试 Console 模式缺少 --cmd 时的错误
TEST_F(DualModeTest, ConsoleModeRequiresCmd) {
    ConsoleArgs args;
    char* argv[] = {
        const_cast<char*>("prog"),
        const_cast<char*>("--fps=30")
    };
    EXPECT_FALSE(args.parse(2, argv));
    EXPECT_FALSE(args.errorMessage.isEmpty());
}

// 测试 Stdio 模式允许无参数
TEST_F(DualModeTest, StdioModeAllowsNoArgs) {
    ConsoleArgs args;
    char* argv[] = {const_cast<char*>("prog")};
    EXPECT_TRUE(args.parse(1, argv));
}

// 测试 --mode=stdio 时不需要 --cmd
TEST_F(DualModeTest, StdioModeNoCmdRequired) {
    ConsoleArgs args;
    char* argv[] = {const_cast<char*>("prog"), const_cast<char*>("--mode=stdio")};
    EXPECT_TRUE(args.parse(2, argv));
}
```

### 6.2 集成测试

```cpp
// 测试 DriverCore::run(argc, argv) 入口
TEST_F(DualModeTest, DriverCoreRunWithHelp) {
    // 验证 --help 返回 0
}

TEST_F(DualModeTest, DriverCoreRunWithVersion) {
    // 验证 --version 返回 0
}

TEST_F(DualModeTest, DriverCoreConsoleCommand) {
    // 验证 Console 模式命令执行
}
```

## 7. 依赖关系

- **前置依赖**：
  - 里程碑 6（Console 模式）：ConsoleArgs, ConsoleResponder
  - 里程碑 10（参数验证）：MetaValidator, DefaultFiller
- **后续依赖**：
  - 里程碑 13（自文档化 Help）
