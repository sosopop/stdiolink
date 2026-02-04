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

1. 如果有 `--mode=stdio` → Stdio 模式
2. 如果有 `--mode=console` → Console 模式
3. 如果有 `--cmd=xxx` → Console 模式
4. 如果有 `--help` 或 `--version` → Console 模式（显示信息后退出）
5. 否则 → Stdio 模式

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

```cpp
TEST(DriverCoreDualMode, DetectStdioByDefault) {
    ConsoleArgs args;
    args.parse(1, {"prog"});
    EXPECT_EQ(detectMode(args), RunMode::Stdio);
}

TEST(DriverCoreDualMode, DetectConsoleWithCmd) {
    ConsoleArgs args;
    args.parse(3, {"prog", "--cmd=scan"});
    EXPECT_EQ(detectMode(args), RunMode::Console);
}

TEST(DriverCoreDualMode, ExplicitStdioMode) {
    ConsoleArgs args;
    args.parse(2, {"prog", "--mode=stdio"});
    EXPECT_EQ(detectMode(args), RunMode::Stdio);
}

TEST(DriverCoreDualMode, ConsoleWithValidation) {
    // 测试 Console 模式下参数验证
}
```

## 7. 依赖关系

- **前置依赖**：
  - 里程碑 6（Console 模式）：ConsoleArgs, ConsoleResponder
  - 里程碑 10（参数验证）：MetaValidator, DefaultFiller
- **后续依赖**：
  - 里程碑 13（自文档化 Help）
