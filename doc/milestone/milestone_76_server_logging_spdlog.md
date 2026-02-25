# 里程碑 76：引入 spdlog 统一服务端日志

> **前置条件**: M34 (Server 基础架构)、M55 (DriverLab WebSocket)
> **目标**: 引入 spdlog 作为服务端日志后端，桥接 Qt 日志系统，实现级别过滤、时间戳、控制台+文件双输出与自动轮转

## 1. 目标

- 引入 spdlog 依赖，通过 vcpkg + `find_package` 集成
- 实现 Qt message handler → spdlog 桥接，现有 `qDebug`/`qInfo`/`qWarning`/`qCritical` 调用自动走 spdlog
- 让 `ServerConfig.logLevel`（`--log-level` CLI 参数）真正生效，按级别过滤日志输出
- Server 日志同时输出到 stderr（带颜色）和 `{dataRoot}/logs/server.log`（带轮转）
- 提供 `ServerLogger` 工具类封装初始化与关闭逻辑

## 2. 背景与问题

- `ServerConfig` 已解析 `logLevel` 字段（支持 debug/info/warn/error），CLI 也支持 `--log-level` 参数，但该值从未被任何代码消费——所有 Qt 日志无条件输出到 stderr
- 服务端共 23 处 Qt 日志调用（17 qWarning、6 qInfo、1 qCritical），全部走 Qt 默认 handler，无时间戳、无文件落盘
- 核心库已有 `installStderrLogger()`（`src/stdiolink/driver/log_redirector.cpp`），但仅用于 driver/service 进程的 stderr 重定向，不适用于 server 场景（无级别过滤、无文件输出）
- 生产环境中 server 日志只在控制台，进程重启后丢失，无法事后排查问题

**范围**:
- `ServerLogger` 工具类（初始化 spdlog、安装 Qt message handler）
- `main.cpp` 启动时调用 `ServerLogger::init()`
- `ServerConfig` 新增 `logMaxBytes` 和 `logMaxFiles` 配置项
- CMake 集成 spdlog 依赖

**非目标**:
- 不修改 Instance 进程日志（M77 范围）
- 不修改 EventBus 事件持久化（M78 范围）
- 不修改核心库 `installStderrLogger()`（driver/service 进程继续使用原有机制）
- 不修改现有 23 处 Qt 日志调用的代码（桥接层自动转发）

## 3. 技术要点

### 3.1 spdlog 集成方式

项目统一使用 vcpkg 管理第三方依赖（`CMakeLists.txt` 已配置 `CMAKE_TOOLCHAIN_FILE` 指向 vcpkg），spdlog 通过 vcpkg 安装并用 `find_package` 引入：

```cmake
# vcpkg.json 新增依赖
# { "name": "spdlog", "version>=": "1.15.1" }

# CMakeLists.txt
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(stdiolink_server PRIVATE spdlog::spdlog)
```

与项目现有的 `find_package(qjs CONFIG REQUIRED)` 和 `find_package(GTest CONFIG REQUIRED)` 保持一致。

### 3.2 Qt message handler 桥接架构

```
  现有业务代码                    桥接层                         spdlog
  ┌──────────┐              ┌──────────────┐              ┌──────────────┐
  │ qInfo()  │──►           │              │──► level     │ stderr sink  │──► 控制台
  │ qWarning │──► Qt msg ──►│ qtToSpdlog   │    filter    │ (带颜色)     │
  │ qCritical│──► handler   │ Handler()    │──► format ──►│              │
  │ qDebug() │──►           │              │──► route     │ rotating     │──► server.log
  └──────────┘              └──────────────┘              │ file sink    │    (带轮转)
                                                          └──────────────┘
```

桥接函数签名：

```cpp
static void qtToSpdlogHandler(QtMsgType type,
                               const QMessageLogContext& context,
                               const QString& msg);
```

级别映射：

| Qt 级别 | spdlog 级别 | config 字符串 |
|---------|------------|--------------|
| `QtDebugMsg` | `spdlog::level::debug` | `"debug"` |
| `QtInfoMsg` | `spdlog::level::info` | `"info"` |
| `QtWarningMsg` | `spdlog::level::warn` | `"warn"` |
| `QtCriticalMsg` | `spdlog::level::err` | `"error"` |
| `QtFatalMsg` | `spdlog::level::critical` | — (始终输出) |

### 3.3 日志输出格式

控制台和文件使用相同的 pattern：

```
2026-02-25T06:19:51.123Z [I] HTTP server listening on 127.0.0.1:8080
2026-02-25T06:19:52.456Z [W] ScheduleEngine: daemon start failed for proj1: timeout
```

spdlog pattern: `"%Y-%m-%dT%H:%M:%S.%eZ [%L] %v"`（需配合 `spdlog::pattern_time_type::utc`）

- `%Y-%m-%dT%H:%M:%S.%eZ` — ISO 8601 UTC 时间戳（毫秒精度）
- `%L` — 单字符级别标识（D/I/W/E/C）
- `%v` — 消息内容
- 注意：spdlog 默认使用本地时间，必须在 `set_pattern()` 第二参数传入 `spdlog::pattern_time_type::utc` 才能输出 UTC 时间戳

### 3.4 ServerConfig 新增字段

```cpp
struct ServerConfig {
    // ... 现有字段 ...
    qint64 logMaxBytes = 10 * 1024 * 1024;  // 10MB，日志文件轮转阈值
    int logMaxFiles = 3;                      // 保留历史文件数
};
```

config.json 示例：
```json
{
  "logLevel": "info",
  "logMaxBytes": 10485760,
  "logMaxFiles": 3
}
```

### 3.5 向后兼容

- 现有 23 处 `qWarning`/`qInfo`/`qCritical` 调用无需任何修改，桥接层自动转发
- `config.json` 新增字段为可选，缺省使用默认值
- 核心库 `installStderrLogger()` 不受影响，driver/service 进程独立使用
- `--log-level` CLI 参数行为不变，仅从"解析但不生效"变为"解析且生效"

## 4. 实现步骤

### 4.1 新增 `server_logger.h` — ServerLogger 工具类声明

- 新增 `src/stdiolink_server/utils/server_logger.h`：
  - 静态工具类 `ServerLogger`，封装 spdlog 初始化与关闭
  - 签名：
    ```cpp
    #pragma once
    #include <QString>

    namespace stdiolink_server {

    class ServerLogger {
    public:
        struct Config {
            QString logLevel = "info";
            QString logDir;
            qint64 maxFileBytes = 10 * 1024 * 1024;
            int maxFiles = 3;
        };

        static bool init(const Config& config, QString& error);
        static void shutdown();

    private:
        ServerLogger() = delete;
    };

    } // namespace stdiolink_server
    ```
  - 理由：静态类避免实例化，`init()` 在 `main()` 中调用一次，返回 `bool` + `error` 与项目 error-return 风格一致；`shutdown()` 在 `aboutToQuit` 中调用

### 4.2 新增 `server_logger.cpp` — spdlog 初始化与 Qt 桥接

- 新增 `src/stdiolink_server/utils/server_logger.cpp`：
  - 级别转换辅助函数：
    ```cpp
    static spdlog::level::level_enum toSpdlogLevel(const QString& level) {
        if (level == "debug") return spdlog::level::debug;
        if (level == "warn")  return spdlog::level::warn;
        if (level == "error") return spdlog::level::err;
        return spdlog::level::info;  // 默认
    }
    ```
  - Qt message handler 桥接函数：
    ```cpp
    static void qtToSpdlogHandler(QtMsgType type,
                                   const QMessageLogContext&,
                                   const QString& msg) {
        auto logger = spdlog::default_logger();
        switch (type) {
        case QtDebugMsg:    logger->debug("{}", msg.toStdString()); break;
        case QtInfoMsg:     logger->info("{}", msg.toStdString()); break;
        case QtWarningMsg:  logger->warn("{}", msg.toStdString()); break;
        case QtCriticalMsg: logger->error("{}", msg.toStdString()); break;
        case QtFatalMsg:    logger->critical("{}", msg.toStdString()); abort();
        }
    }
    ```
  - `init()` 实现：
    ```cpp
    static QtMessageHandler s_previousHandler = nullptr;

    bool ServerLogger::init(const Config& config, QString& error) {
        try {
            auto consoleSink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
            auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                (config.logDir + "/server.log").toStdString(),
                static_cast<size_t>(config.maxFileBytes),
                static_cast<size_t>(config.maxFiles));

            auto logger = std::make_shared<spdlog::logger>(
                "server", spdlog::sinks_init_list{consoleSink, fileSink});

            logger->set_level(toSpdlogLevel(config.logLevel));
            logger->set_pattern("%Y-%m-%dT%H:%M:%S.%eZ [%L] %v",
                                spdlog::pattern_time_type::utc);
            logger->flush_on(spdlog::level::warn);

            spdlog::set_default_logger(logger);
            s_previousHandler = qInstallMessageHandler(qtToSpdlogHandler);
            return true;
        } catch (const spdlog::spdlog_ex& ex) {
            error = QString("failed to initialize logger: %1").arg(ex.what());
            return false;
        }
    }
    ```
  - `shutdown()` 实现：
    ```cpp
    void ServerLogger::shutdown() {
        qInstallMessageHandler(s_previousHandler);  // nullptr 即恢复 Qt 默认 handler
        s_previousHandler = nullptr;
        spdlog::shutdown();
    }
    ```
  - 理由：
    - dual-sink（控制台+文件）确保日志既可实时查看又可事后追溯
    - `flush_on(warn)` 确保警告及以上级别立即刷盘
    - `spdlog::pattern_time_type::utc` 确保时间戳为 UTC（spdlog 默认使用本地时间）
    - `try-catch` 捕获 spdlog 异常（如文件创建失败），与项目 error-return 风格一致
    - `shutdown()` 恢复之前的 Qt message handler，避免 spdlog 关闭后 Qt 日志调用崩溃

### 4.3 修改 `server_config.h/.cpp` — 新增日志轮转配置

- 修改 `src/stdiolink_server/config/server_config.h`：
  - 在 `ServerConfig` 结构体中新增字段：
    ```cpp
    qint64 logMaxBytes = 10 * 1024 * 1024;  // 10MB
    int logMaxFiles = 3;
    ```

- 修改 `src/stdiolink_server/config/server_config.cpp`：
  - 在 `loadFromFile()` 的 `known` 字段集合中添加新字段名：
    ```cpp
    static const QSet<QString> known = {
        "port", "host", "logLevel", "serviceProgram", "corsOrigin", "webuiDir",
        "logMaxBytes", "logMaxFiles"  // ← 新增
    };
    ```
  - 在 `loadFromFile()` 中新增解析逻辑（含边界校验）：
    ```cpp
    if (obj.contains("logMaxBytes")) {
        if (!obj.value("logMaxBytes").isDouble()) {
            error = "config field 'logMaxBytes' must be a number";
            return cfg;
        }
        cfg.logMaxBytes = static_cast<qint64>(obj.value("logMaxBytes").toDouble());
        if (cfg.logMaxBytes < 1024 * 1024) {
            error = "config field 'logMaxBytes' must be >= 1048576 (1MB)";
            return cfg;
        }
    }
    if (obj.contains("logMaxFiles")) {
        if (!obj.value("logMaxFiles").isDouble()) {
            error = "config field 'logMaxFiles' must be a number";
            return cfg;
        }
        cfg.logMaxFiles = obj.value("logMaxFiles").toInt();
        if (cfg.logMaxFiles < 1 || cfg.logMaxFiles > 100) {
            error = "config field 'logMaxFiles' must be between 1 and 100";
            return cfg;
        }
    }
    ```
  - 理由：
    - 必须将新字段名加入 `known` 集合，否则 `loadFromFile()` 的未知字段校验会拒绝包含这些字段的 config.json
    - `logMaxBytes` 下限 1MB 防止过小导致频繁轮转；`logMaxFiles` 限制 1–100 防止无意义值

### 4.4 修改 `main.cpp` — 启动时初始化 ServerLogger

- 修改 `src/stdiolink_server/main.cpp`：
  - 在 `config.applyArgs(args)` 和 `ensureDirectories(dataRoot)` 之后，初始化 logger：
    ```cpp
    #include "utils/server_logger.h"

    // ... 在 ensureDirectories 之后 ...
    ServerLogger::Config logCfg;
    logCfg.logLevel = config.logLevel;
    logCfg.logDir = dataRoot + "/logs";
    logCfg.maxFileBytes = config.logMaxBytes;
    logCfg.maxFiles = config.logMaxFiles;
    QString logError;
    if (!ServerLogger::init(logCfg, logError)) {
        qWarning("Failed to initialize logger: %s", qPrintable(logError));
        // 继续运行，日志仅输出到 stderr
    }
    ```
  - 在 `aboutToQuit` 连接中追加 shutdown：
    ```cpp
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        manager.shutdown();
        ServerLogger::shutdown();  // ← 新增
    });
    ```
  - 理由：必须在 `ensureDirectories` 之后调用（确保 logs 目录存在）；在 `aboutToQuit` 中 shutdown 确保日志缓冲区刷盘

### 4.5 修改 CMakeLists.txt — 添加 spdlog 依赖与新源文件

- 修改 `vcpkg.json`：
  - 在 `dependencies` 数组中添加：
    ```json
    { "name": "spdlog", "version>=": "1.15.1" }
    ```

- 修改 `src/stdiolink_server/CMakeLists.txt`：
  - 添加 `find_package`：
    ```cmake
    find_package(spdlog CONFIG REQUIRED)
    ```
  - 在 SOURCES 中添加 `utils/server_logger.cpp`
  - 在 `target_link_libraries` 中添加 `spdlog::spdlog`

- 修改 `src/tests/CMakeLists.txt`：
  - 添加 `test_server_logger.cpp`
  - 链接 `spdlog::spdlog`

## 5. 文件变更清单

### 5.1 新增文件
- `src/stdiolink_server/utils/server_logger.h` — ServerLogger 工具类声明
- `src/stdiolink_server/utils/server_logger.cpp` — spdlog 初始化、Qt 桥接、shutdown

### 5.2 修改文件
- `src/stdiolink_server/main.cpp` — 启动时调用 `ServerLogger::init()`，退出时调用 `shutdown()`
- `src/stdiolink_server/config/server_config.h` — 新增 `logMaxBytes`、`logMaxFiles` 字段
- `src/stdiolink_server/config/server_config.cpp` — 解析新增配置字段 + 更新 known 字段集合
- `src/stdiolink_server/CMakeLists.txt` — find_package spdlog + 新源文件 + 链接
- `src/tests/CMakeLists.txt` — 添加测试文件 + 链接 spdlog
- `vcpkg.json` — 新增 spdlog 依赖声明

### 5.3 测试文件
- `src/tests/test_server_logger.cpp` — ServerLogger 单元测试

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: `ServerLogger` 初始化、级别过滤、文件写入、Qt 桥接
- 用例分层: 正常路径（T01–T03）、边界值（T04）、异常输入（T05）、兼容回归（T06）
- 断言要点: 日志文件存在性、文件内容格式、级别过滤行为、Fatal 不被过滤
- 桩替身策略: 使用 `QTemporaryDir` 创建临时日志目录，避免污染真实环境；通过读取日志文件内容验证写入行为
- 测试文件: `src/tests/test_server_logger.cpp`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `init`: logLevel = "info" | debug 被过滤，info/warn/error 输出 | T01 |
| `init`: logLevel = "warn" | debug/info 被过滤，warn/error 输出 | T02 |
| `init`: logLevel = "debug" | 所有级别均输出 | T03 |
| `init`: logLevel = "error" | 仅 error 输出 | T04 |
| `init`: 时间戳格式 | 日志行以 ISO 8601 UTC 时间戳开头 | T05 |
| `init`: 文件写入 | server.log 文件被创建且包含日志内容 | T01 |
| config 解析: logMaxBytes/logMaxFiles 缺省 | 使用默认值 10MB/3 | T06 |

#### 用例详情

**T01 — 默认级别 info 过滤 debug，输出 info/warn/error 到文件**
- 前置条件: `QTemporaryDir` 创建临时目录，`ServerLogger::init({logLevel="info", logDir=tmpDir})`
- 输入: 依次调用 `qDebug("d")`, `qInfo("i")`, `qWarning("w")`, `qCritical("e")`
- 预期: `server.log` 文件存在，包含 `[I] i`、`[W] w`、`[E] e` 三行，不包含 `[D] d`
- 断言: 文件行数 == 3；每行匹配 `\d{4}-\d{2}-\d{2}T.*\[.\] .*` 格式；不含 `[D]`

**T02 — warn 级别过滤 debug 和 info**
- 前置条件: 同 T01，`logLevel="warn"`
- 输入: 依次调用 `qDebug("d")`, `qInfo("i")`, `qWarning("w")`, `qCritical("e")`
- 预期: 文件仅包含 `[W] w` 和 `[E] e` 两行
- 断言: 文件行数 == 2；不含 `[D]` 和 `[I]`

**T03 — debug 级别输出所有日志**
- 前置条件: 同 T01，`logLevel="debug"`
- 输入: 依次调用 `qDebug("d")`, `qInfo("i")`, `qWarning("w")`, `qCritical("e")`
- 预期: 文件包含全部 4 行
- 断言: 文件行数 == 4；包含 `[D]`、`[I]`、`[W]`、`[E]`

**T04 — error 级别仅输出 error**
- 前置条件: 同 T01，`logLevel="error"`
- 输入: 依次调用 `qDebug("d")`, `qInfo("i")`, `qWarning("w")`, `qCritical("e")`
- 预期: 文件仅包含 `[E] e` 一行
- 断言: 文件行数 == 1；仅含 `[E]`

**T05 — 时间戳格式符合 ISO 8601**
- 前置条件: 同 T01，`logLevel="info"`
- 输入: `qInfo("test_timestamp")`
- 预期: 日志行以 ISO 8601 时间戳开头
- 断言: 行匹配正则 `^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z \[I\] test_timestamp$`

**T06 — config 新增字段缺省值正确**
- 前置条件: 无
- 输入: 默认构造 `ServerConfig`
- 预期: `logMaxBytes == 10 * 1024 * 1024`，`logMaxFiles == 3`
- 断言: `EXPECT_EQ(config.logMaxBytes, 10 * 1024 * 1024)`，`EXPECT_EQ(config.logMaxFiles, 3)`

#### 测试代码

```cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include "stdiolink_server/utils/server_logger.h"
#include "stdiolink_server/config/server_config.h"

using namespace stdiolink_server;

namespace {

QStringList readLogLines(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QStringList lines;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (!line.isEmpty()) lines.append(line);
    }
    return lines;
}

} // namespace

TEST(ServerLoggerTest, InfoLevelFiltersDebug) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    ServerLogger::Config cfg;
    cfg.logLevel = "info";
    cfg.logDir = tmpDir.path();
    QString error;
    ASSERT_TRUE(ServerLogger::init(cfg, error)) << qPrintable(error);
    qDebug("d");
    qInfo("i");
    qWarning("w");
    qCritical("e");
    ServerLogger::shutdown();

    const auto lines = readLogLines(tmpDir.path() + "/server.log");
    EXPECT_EQ(lines.size(), 3);
    for (const auto& line : lines) {
        EXPECT_FALSE(line.contains("[D]"));
    }
}

TEST(ServerLoggerTest, WarnLevelFiltersDebugAndInfo) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    ServerLogger::Config cfg;
    cfg.logLevel = "warn";
    cfg.logDir = tmpDir.path();
    QString error;
    ASSERT_TRUE(ServerLogger::init(cfg, error)) << qPrintable(error);
    qDebug("d");
    qInfo("i");
    qWarning("w");
    qCritical("e");
    ServerLogger::shutdown();

    const auto lines = readLogLines(tmpDir.path() + "/server.log");
    EXPECT_EQ(lines.size(), 2);
}

TEST(ServerLoggerTest, DebugLevelOutputsAll) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    ServerLogger::Config cfg;
    cfg.logLevel = "debug";
    cfg.logDir = tmpDir.path();
    QString error;
    ASSERT_TRUE(ServerLogger::init(cfg, error)) << qPrintable(error);
    qDebug("d");
    qInfo("i");
    qWarning("w");
    qCritical("e");
    ServerLogger::shutdown();

    const auto lines = readLogLines(tmpDir.path() + "/server.log");
    EXPECT_EQ(lines.size(), 4);
}

TEST(ServerLoggerTest, ErrorLevelOnlyError) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    ServerLogger::Config cfg;
    cfg.logLevel = "error";
    cfg.logDir = tmpDir.path();
    QString error;
    ASSERT_TRUE(ServerLogger::init(cfg, error)) << qPrintable(error);
    qDebug("d");
    qInfo("i");
    qWarning("w");
    qCritical("e");
    ServerLogger::shutdown();

    const auto lines = readLogLines(tmpDir.path() + "/server.log");
    EXPECT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines[0].contains("[E]"));
}

TEST(ServerLoggerTest, TimestampFormatISO8601) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    ServerLogger::Config cfg;
    cfg.logLevel = "info";
    cfg.logDir = tmpDir.path();
    QString error;
    ASSERT_TRUE(ServerLogger::init(cfg, error)) << qPrintable(error);
    qInfo("test_timestamp");
    ServerLogger::shutdown();

    const auto lines = readLogLines(tmpDir.path() + "/server.log");
    ASSERT_EQ(lines.size(), 1);
    // ISO 8601: YYYY-MM-DDTHH:MM:SS.mmmZ [L] msg
    QRegularExpression re(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z \[I\] test_timestamp$)");
    EXPECT_TRUE(re.match(lines[0]).hasMatch());
}

TEST(ServerLoggerTest, ConfigDefaultValues) {
    ServerConfig config;
    EXPECT_EQ(config.logMaxBytes, 10 * 1024 * 1024);
    EXPECT_EQ(config.logMaxFiles, 3);
}
```

### 6.2 集成测试

- 启动 server 进程指定 `--log-level=warn`，确认控制台仅输出 warn 及以上级别
- 检查 `{dataRoot}/logs/server.log` 文件存在且内容与控制台一致

### 6.3 验收标准

- [ ] `--log-level=warn` 启动后，qDebug/qInfo 不输出，qWarning 带时间戳输出（T01, T02）
- [ ] 日志同时写入控制台和 `server.log` 文件（T01）
- [ ] 时间戳格式为 ISO 8601 UTC 毫秒精度（T05）
- [ ] `config.json` 新增字段缺省值正确（T06）
- [ ] 现有 23 处 Qt 日志调用无需修改即可自动走 spdlog（T01–T04）

## 7. 风险与控制

- 风险: vcpkg 安装 spdlog 失败（网络不可达或版本不可用）
  - 控制: vcpkg.json 锁定 `version>= 1.15.1`，CI 环境可预缓存 vcpkg binary cache
  - 控制: 备选方案为将 spdlog 源码作为 third_party 子目录提交

- 风险: spdlog 构造函数抛出异常（如日志目录不存在或无写入权限）
  - 控制: `ServerLogger::init()` 使用 try-catch 捕获 `spdlog::spdlog_ex`，返回 error 信息
  - 控制: `main.cpp` 中 init 失败时降级为纯 stderr 输出，不阻塞启动

- 风险: Qt message handler 全局唯一，`qInstallMessageHandler` 覆盖后核心库 `installStderrLogger` 失效
  - 控制: `installStderrLogger` 仅在 driver/service 子进程中调用，server 进程使用 `ServerLogger`，两者不在同一进程中运行，无冲突
  - 测试覆盖: T01–T04

- 风险: spdlog 与 Qt 事件循环线程模型冲突
  - 控制: spdlog `_mt` 后缀 sink 为线程安全版本；server 主线程调用 Qt 日志，spdlog 内部处理线程安全

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] spdlog 依赖通过 vcpkg + find_package 集成，编译通过
- [ ] `ServerLogger::init()` 正确安装 Qt message handler 并创建 dual-sink logger
- [ ] `--log-level` 配置真正生效，按级别过滤日志
- [ ] `server.log` 文件自动创建并带轮转
- [ ] 全量既有测试无回归
- [ ] 文档同步完成
- [ ] 向后兼容确认（现有 Qt 日志调用无需修改，config.json 新字段可选）
