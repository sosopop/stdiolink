# 里程碑 77：Instance 日志结构化与轮转

> **前置条件**: M76 (spdlog 集成)、M34 (Server 基础架构)
> **目标**: 替换 Instance 进程日志的原始文件重定向为 spdlog 驱动的结构化写入，实现时间戳注入、stdout/stderr 来源标记与自动轮转

## 1. 目标

- 替换 `QProcess::setStandardOutputFile/setStandardErrorFile` 为信号驱动的日志写入
- 每行日志自动注入 ISO 8601 UTC 时间戳
- stderr 输出添加 `[stderr]` 前缀，与 stdout 区分来源
- 基于 spdlog `rotating_file_sink` 实现自动轮转（默认 10MB，保留 3 个历史文件）
- 日志路径不变（`{dataRoot}/logs/{projectId}.log`），现有 API 端点无需修改

## 2. 背景与问题

- 当前 `InstanceManager::startInstance()` 通过 `QProcess::setStandardOutputFile()` 和 `setStandardErrorFile()` 将子进程 stdout/stderr 直接重定向到 `{dataRoot}/logs/{projectId}.log`
- 日志文件中 stdout（JSONL 协议数据）和 stderr（JS 运行时 Qt 消息）混合写入，无法区分来源
- 无时间戳——无法判断每行日志的产生时间
- 无文件大小限制——长期运行的 keepalive 实例日志文件无限增长
- 无轮转机制——磁盘空间可能被耗尽

**范围**:
- `InstanceLogWriter` 类：基于 spdlog rotating logger 的日志写入器
- `InstanceManager::startInstance()` 改造：从文件重定向切换为信号驱动
- `Instance` 模型扩展：持有 `InstanceLogWriter` 实例

**非目标**:
- 不修改 DriverLab WebSocket 的 driver 输出处理（stdout 转发到 WebSocket 的逻辑不变）
- 不修改 Host-side Driver 类的 stdout 解析逻辑
- 不修改日志 API 端点（`GET /api/projects/{id}/logs`、`GET /api/instances/{id}/logs`）
- 不修改 `readTailLines()` 实现（时间戳前缀不影响尾部读取）

## 3. 技术要点

### 3.1 信号驱动 vs 文件重定向

```
  Before (当前):
  ┌──────────┐    setStandardOutputFile     ┌──────────┐
  │ QProcess │ ──────────────────────────► │ .log 文件 │  原始 stdout+stderr 混合
  │ (Service)│    setStandardErrorFile      │ (无时间戳) │
  └──────────┘                              └──────────┘

  After (本次改造):
  ┌──────────┐  readyReadStandardOutput   ┌──────────────────┐  spdlog rotating  ┌──────────┐
  │ QProcess │ ─────────────────────────► │ InstanceLogWriter │ ───────────────► │ .log 文件 │
  │ (Service)│  readyReadStandardError    │ (时间戳+前缀)     │   file sink      │ (带轮转)  │
  └──────────┘                            └──────────────────┘                   └──────────┘
```

选择信号驱动的原因：`QProcess::setStandardOutputFile()` 是内核级文件重定向，数据不经过父进程内存，无法注入时间戳。改用 `readyReadStandardOutput` 信号后，数据经过 server 进程，可以逐行添加时间戳和来源标记。

代价：数据经过 server 进程内存。但 Instance 输出通常是低频 JSONL（每秒几行到几十行），内存开销可忽略。

### 3.2 日志输出格式

stdout 行：
```
2026-02-25T06:19:51.123Z | {"status":"done","data":{"result":42}}
```

stderr 行：
```
2026-02-25T06:19:51.456Z | [stderr] [WARN] {"ts":"...","level":"warn","msg":"timeout"}
```

spdlog pattern: `"%Y-%m-%dT%H:%M:%S.%eZ | %v"`（需配合 `spdlog::pattern_time_type::utc`）

- 时间戳由 spdlog 自动生成（UTC 毫秒精度，需在 `set_pattern()` 第二参数传入 `spdlog::pattern_time_type::utc`）
- `|` 分隔符便于解析
- stderr 行的 `[stderr]` 前缀由 `InstanceLogWriter::appendStderr()` 注入

### 3.3 InstanceLogWriter 数据结构

```cpp
class InstanceLogWriter {
public:
    InstanceLogWriter(const QString& logPath, qint64 maxBytes, int maxFiles);
    ~InstanceLogWriter();

    void appendStdout(const QByteArray& data);
    void appendStderr(const QByteArray& data);
    QString logPath() const;

private:
    std::shared_ptr<spdlog::logger> m_logger;
    QByteArray m_stdoutBuf;  // 未完成行缓冲
    QByteArray m_stderrBuf;  // 未完成行缓冲
    QString m_logPath;
};
```

每个 `InstanceLogWriter` 持有独立的 spdlog named logger（名称为 `"inst_{logPath}"`），使用独立的 `rotating_file_sink_mt`。多个 Instance 的 logger 互不干扰。

### 3.4 行缓冲策略

`QProcess::readyReadStandardOutput` 信号可能在行中间触发（TCP 管道不保证按行分割）。因此需要行缓冲：

```
收到数据: "hello\nwor"
  → 写入 "hello" 行
  → "wor" 留在缓冲区

下次收到: "ld\nbye\n"
  → 拼接为 "world\nbye\n"
  → 写入 "world" 和 "bye" 两行
  → 缓冲区清空
```

### 3.5 向后兼容

- 日志文件路径不变：`{dataRoot}/logs/{projectId}.log`
- API 端点 `GET /api/projects/{id}/logs` 和 `GET /api/instances/{id}/logs` 无需修改
- `readTailLines()` 按行读取，时间戳前缀不影响其逻辑
- 轮转文件命名：`{projectId}.1.log`、`{projectId}.2.log`、`{projectId}.3.log`（spdlog 默认命名，索引插入扩展名之前）
- `config.json` 的 `logMaxBytes` 和 `logMaxFiles` 字段在 M76 中已添加，本里程碑复用

## 4. 实现步骤

### 4.1 新增 `instance_log_writer.h` — 类声明

- 新增 `src/stdiolink_server/manager/instance_log_writer.h`：
  ```cpp
  #pragma once
  #include <QByteArray>
  #include <QString>
  #include <memory>
  #include <spdlog/spdlog.h>

  namespace stdiolink_server {

  class InstanceLogWriter {
  public:
      InstanceLogWriter(const QString& logPath,
                        qint64 maxBytes = 10 * 1024 * 1024,
                        int maxFiles = 3);
      ~InstanceLogWriter();

      void appendStdout(const QByteArray& data);
      void appendStderr(const QByteArray& data);
      QString logPath() const { return m_logPath; }

  private:
      void processBuffer(QByteArray& buf, const char* prefix);

      std::shared_ptr<spdlog::logger> m_logger;
      QByteArray m_stdoutBuf;
      QByteArray m_stderrBuf;
      QString m_logPath;

      // 缓冲区上限，防止子进程输出不含换行时内存无限增长
      static constexpr qint64 kMaxBufferBytes = 1 * 1024 * 1024;  // 1MB
  };

  } // namespace stdiolink_server
  ```
  - 理由：`processBuffer` 提取公共的行缓冲+写入逻辑，`appendStdout` 和 `appendStderr` 仅在前缀上有差异

### 4.2 新增 `instance_log_writer.cpp` — spdlog rotating logger 与行缓冲

- 新增 `src/stdiolink_server/manager/instance_log_writer.cpp`：
  - 构造函数：创建独立的 spdlog named logger + rotating_file_sink：
    ```cpp
    #include "instance_log_writer.h"
    #include <spdlog/sinks/rotating_file_sink.h>

    namespace stdiolink_server {

    InstanceLogWriter::InstanceLogWriter(const QString& logPath,
                                         qint64 maxBytes, int maxFiles)
        : m_logPath(logPath)
    {
        try {
            auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                logPath.toStdString(),
                static_cast<size_t>(maxBytes),
                static_cast<size_t>(maxFiles));

            const std::string loggerName = "inst_" + logPath.toStdString();
            m_logger = std::make_shared<spdlog::logger>(loggerName, sink);
            m_logger->set_pattern("%Y-%m-%dT%H:%M:%S.%eZ | %v",
                                  spdlog::pattern_time_type::utc);
            m_logger->set_level(spdlog::level::trace);
            m_logger->flush_on(spdlog::level::trace);
        } catch (const spdlog::spdlog_ex& ex) {
            qWarning("InstanceLogWriter: failed to create logger for %s: %s",
                     qPrintable(logPath), ex.what());
            // m_logger 为空，append* 方法中需检查
        }
    }

    InstanceLogWriter::~InstanceLogWriter() {
        if (!m_logger) return;
        // flush 残留缓冲（不完整行也写出）
        if (!m_stdoutBuf.isEmpty()) {
            m_logger->info("{}", m_stdoutBuf.constData());
        }
        if (!m_stderrBuf.isEmpty()) {
            m_logger->info("[stderr] {}", m_stderrBuf.constData());
        }
        spdlog::drop(m_logger->name());
    }
    ```
  - `processBuffer` 公共行缓冲逻辑：
    ```cpp
    void InstanceLogWriter::processBuffer(QByteArray& buf,
                                           const char* prefix) {
        if (!m_logger) { buf.clear(); return; }
        while (true) {
            const int nl = buf.indexOf('\n');
            if (nl < 0) break;
            const QByteArray line = buf.left(nl);
            buf.remove(0, nl + 1);
            if (line.isEmpty()) continue;
            if (prefix) {
                m_logger->info("{} {}", prefix, line.constData());
            } else {
                m_logger->info("{}", line.constData());
            }
        }
    }

    void InstanceLogWriter::appendStdout(const QByteArray& data) {
        m_stdoutBuf.append(data);
        processBuffer(m_stdoutBuf, nullptr);
        // 缓冲区超限时强制刷出（防止子进程输出不含换行导致内存无限增长）
        if (m_stdoutBuf.size() > kMaxBufferBytes) {
            m_logger->info("{}", m_stdoutBuf.constData());
            m_stdoutBuf.clear();
        }
    }

    void InstanceLogWriter::appendStderr(const QByteArray& data) {
        m_stderrBuf.append(data);
        processBuffer(m_stderrBuf, "[stderr]");
        if (m_stderrBuf.size() > kMaxBufferBytes) {
            m_logger->info("[stderr] {}", m_stderrBuf.constData());
            m_stderrBuf.clear();
        }
    }

    } // namespace stdiolink_server
    ```
  - 理由：
    - 每个 InstanceLogWriter 持有独立 named logger，避免多 Instance 日志串扰
    - `flush_on(trace)` 确保每行立即刷盘，不丢日志
    - 析构时 flush 残留缓冲，防止进程退出时丢失未完成行
    - `spdlog::drop()` 释放 logger 注册，避免 logger 名称泄漏

### 4.3 修改 `instance_manager.cpp` — 信号驱动替换文件重定向

- 修改 `src/stdiolink_server/manager/instance_manager.cpp`：
  - 新增头文件引用：
    ```cpp
    #include "instance_log_writer.h"
    ```
  - 在 `startInstance()` 中，删除文件重定向：
    ```cpp
    // 删除以下两行：
    // proc->setStandardOutputFile(logPath, QIODevice::Append);
    // proc->setStandardErrorFile(logPath, QIODevice::Append);
    ```
  - 替换为 InstanceLogWriter + 信号连接：
    ```cpp
    auto logWriter = std::make_unique<InstanceLogWriter>(
        logPath, m_config.logMaxBytes, m_config.logMaxFiles);

    connect(proc, &QProcess::readyReadStandardOutput, this,
        [proc, w = logWriter.get()]() {
            w->appendStdout(proc->readAllStandardOutput());
        });
    connect(proc, &QProcess::readyReadStandardError, this,
        [proc, w = logWriter.get()]() {
            w->appendStderr(proc->readAllStandardError());
        });

    inst->logWriter = std::move(logWriter);
    ```
  - 理由：
    - `logWriter.get()` 裸指针传入 lambda 安全：`logWriter` 生命周期与 `Instance` 一致，`Instance` 在 `onProcessFinished` 中与 `QProcess` 同步销毁
    - `readAllStandardOutput()` 在信号回调中调用，确保数据不丢失

### 4.4 修改 `instance.h` — Instance 持有 LogWriter

- 修改 `src/stdiolink_server/model/instance.h`：
  - 新增前置声明和头文件：
    ```cpp
    #include <memory>
    // 前置声明（避免头文件循环依赖）
    namespace stdiolink_server { class InstanceLogWriter; }
    ```
  - 在 `Instance` 结构体中新增字段：
    ```cpp
    std::unique_ptr<InstanceLogWriter> logWriter;
    ```
  - 由于 `Instance` 使用前置声明的不完整类型 `InstanceLogWriter` 作为 `unique_ptr` 模板参数，必须确保 `Instance` 的析构函数在 `InstanceLogWriter` 完整定义可见的翻译单元中实现。当前 `Instance` 是 POD-like 结构体，编译器生成的析构函数在 `instance.h` 中内联，此时 `InstanceLogWriter` 不完整会导致编译错误。解决方案：
    - 在 `instance.h` 中声明析构函数：`~Instance();`
    - 在 `instance.cpp`（或包含 `instance_log_writer.h` 的 `.cpp` 文件）中定义：`Instance::~Instance() = default;`
  - 理由：`unique_ptr` 确保 Instance 销毁时自动析构 LogWriter，触发残留缓冲 flush 和 spdlog logger 注销

### 4.5 修改 CMakeLists.txt — 添加新源文件

- 修改 `src/stdiolink_server/CMakeLists.txt`：
  - 在 `STDIOLINK_SERVER_SOURCES` 中添加：
    ```cmake
    manager/instance_log_writer.cpp
    model/instance.cpp
    ```
  - 在 `STDIOLINK_SERVER_HEADERS` 中添加：
    ```cmake
    manager/instance_log_writer.h
    ```
  - 注意：spdlog 依赖已在 M76 中通过 vcpkg + find_package 引入并链接，本里程碑无需重复添加

- 修改 `src/tests/CMakeLists.txt`：
  - 在 `TEST_SOURCES` 中添加：
    ```cmake
    test_instance_log_writer.cpp
    ```
  - 在 `target_sources` 中添加：
    ```cmake
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/manager/instance_log_writer.cpp
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/model/instance.cpp
    ```

## 5. 文件变更清单

### 5.1 新增文件
- `src/stdiolink_server/manager/instance_log_writer.h` — InstanceLogWriter 类声明
- `src/stdiolink_server/manager/instance_log_writer.cpp` — spdlog rotating logger 写入器实现

### 5.2 修改文件
- `src/stdiolink_server/manager/instance_manager.cpp` — 替换文件重定向为信号驱动 + LogWriter
- `src/stdiolink_server/model/instance.h` — Instance 新增 `logWriter` 字段 + 析构函数声明
- `src/stdiolink_server/model/instance.cpp` — Instance 析构函数定义（`= default`，确保 `InstanceLogWriter` 完整类型可见）
- `src/stdiolink_server/CMakeLists.txt` — 添加新源文件
- `src/tests/CMakeLists.txt` — 添加测试文件 + 新源文件

### 5.3 测试文件
- `src/tests/test_instance_log_writer.cpp` — InstanceLogWriter 单元测试

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: `InstanceLogWriter` 的行缓冲、时间戳注入、stderr 前缀、轮转行为
- 用例分层: 正常路径（T01–T03）、行缓冲边界（T04–T05）、轮转验证（T06）、析构 flush（T07）
- 断言要点: 日志文件存在性、行内容格式、时间戳正则匹配、stderr 前缀存在性、轮转文件生成
- 桩替身策略: 使用 `QTemporaryDir` 创建临时日志目录，避免污染真实环境
- 测试文件: `src/tests/test_instance_log_writer.cpp`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `appendStdout`: 完整行 | 写入带时间戳的行 | T01 |
| `appendStderr`: 完整行 | 写入带时间戳和 `[stderr]` 前缀的行 | T02 |
| `appendStdout` + `appendStderr` 混合 | 两种来源交替写入，格式各自正确 | T03 |
| `appendStdout`: 不完整行（无 `\n`） | 缓冲不写入，下次补齐后写入 | T04 |
| `appendStdout`: 多行一次到达 | 每行独立写入，各有独立时间戳 | T05 |
| 文件超过 maxBytes | 轮转文件 `.log.1` 生成 | T06 |
| 析构时缓冲区有残留 | 残留内容被 flush 写入 | T07 |

#### 用例详情

**T01 — stdout 完整行写入带时间戳**
- 前置条件: `QTemporaryDir` 创建临时目录，构造 `InstanceLogWriter(tmpDir + "/test.log")`
- 输入: `appendStdout("hello world\n")`
- 预期: `test.log` 包含 1 行，格式为 `YYYY-MM-DDTHH:MM:SS.mmmZ | hello world`
- 断言: 文件行数 == 1；行匹配正则 `^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z \| hello world$`

**T02 — stderr 行带 `[stderr]` 前缀**
- 前置条件: 同 T01
- 输入: `appendStderr("some warning\n")`
- 预期: `test.log` 包含 1 行，格式为 `YYYY-MM-DDTHH:MM:SS.mmmZ | [stderr] some warning`
- 断言: 文件行数 == 1；行包含 `[stderr] some warning`

**T03 — stdout 与 stderr 混合写入**
- 前置条件: 同 T01
- 输入: `appendStdout("out1\n")`, `appendStderr("err1\n")`, `appendStdout("out2\n")`
- 预期: 文件包含 3 行，第 1 行含 `out1`（无 `[stderr]`），第 2 行含 `[stderr] err1`，第 3 行含 `out2`（无 `[stderr]`）
- 断言: 文件行数 == 3；各行前缀正确

**T04 — 不完整行缓冲，补齐后写入**
- 前置条件: 同 T01
- 输入: `appendStdout("hel")`，然后 `appendStdout("lo\n")`
- 预期: 第一次调用后文件为空（或不存在），第二次调用后文件包含 1 行 `hello`
- 断言: 最终文件行数 == 1；行包含 `hello`

**T05 — 多行一次到达，各行独立写入**
- 前置条件: 同 T01
- 输入: `appendStdout("line1\nline2\nline3\n")`
- 预期: 文件包含 3 行，每行各有独立时间戳
- 断言: 文件行数 == 3；每行匹配时间戳格式

**T06 — 文件轮转**
- 前置条件: 构造 `InstanceLogWriter(tmpDir + "/test.log", 1024, 2)`（1KB 轮转阈值）
- 输入: 循环写入足够多的 stdout 行，总量超过 1KB
- 预期: `test.log.1` 文件存在
- 断言: `QFileInfo(tmpDir + "/test.log.1").exists()` 为 true

**T07 — 析构时 flush 残留缓冲**
- 前置条件: 同 T01
- 输入: `appendStdout("incomplete")`（无换行），然后析构 LogWriter
- 预期: 文件包含 1 行，内容含 `incomplete`
- 断言: 文件行数 == 1；行包含 `incomplete`

#### 测试代码

```cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include "stdiolink_server/manager/instance_log_writer.h"

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

const QRegularExpression kTimestampRe(
    R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z \| .+$)");

} // namespace

TEST(InstanceLogWriterTest, StdoutLineWithTimestamp) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath);
        writer.appendStdout("hello world\n");
    }

    const auto lines = readLogLines(logPath);
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(kTimestampRe.match(lines[0]).hasMatch());
    EXPECT_TRUE(lines[0].contains("hello world"));
    EXPECT_FALSE(lines[0].contains("[stderr]"));
}

TEST(InstanceLogWriterTest, StderrLineWithPrefix) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath);
        writer.appendStderr("some warning\n");
    }

    const auto lines = readLogLines(logPath);
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines[0].contains("[stderr] some warning"));
}

TEST(InstanceLogWriterTest, MixedStdoutStderr) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath);
        writer.appendStdout("out1\n");
        writer.appendStderr("err1\n");
        writer.appendStdout("out2\n");
    }

    const auto lines = readLogLines(logPath);
    ASSERT_EQ(lines.size(), 3);
    EXPECT_TRUE(lines[0].contains("out1"));
    EXPECT_FALSE(lines[0].contains("[stderr]"));
    EXPECT_TRUE(lines[1].contains("[stderr] err1"));
    EXPECT_TRUE(lines[2].contains("out2"));
    EXPECT_FALSE(lines[2].contains("[stderr]"));
}

TEST(InstanceLogWriterTest, IncompleteLineBuffered) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath);
        writer.appendStdout("hel");
        // 文件应为空或不存在（数据在缓冲区）
        EXPECT_TRUE(readLogLines(logPath).isEmpty());
        writer.appendStdout("lo\n");
    }

    const auto lines = readLogLines(logPath);
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines[0].contains("hello"));
}

TEST(InstanceLogWriterTest, MultipleLinesSingleChunk) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath);
        writer.appendStdout("line1\nline2\nline3\n");
    }

    const auto lines = readLogLines(logPath);
    ASSERT_EQ(lines.size(), 3);
    for (const auto& line : lines) {
        EXPECT_TRUE(kTimestampRe.match(line).hasMatch());
    }
}

TEST(InstanceLogWriterTest, FileRotation) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath, 1024, 2);
        // 写入超过 1KB 的数据触发轮转
        const QByteArray chunk = QByteArray(80, 'x') + "\n";
        for (int i = 0; i < 20; ++i) {
            writer.appendStdout(chunk);
        }
    }

    EXPECT_TRUE(QFileInfo::exists(logPath + ".1"));
}

TEST(InstanceLogWriterTest, DestructorFlushesIncompleteBuffer) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/test.log";

    {
        InstanceLogWriter writer(logPath);
        writer.appendStdout("incomplete");  // 无换行
    }  // 析构触发 flush

    const auto lines = readLogLines(logPath);
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines[0].contains("incomplete"));
}
```

### 6.2 集成测试

- 启动 Instance（通过 API 或 ScheduleEngine），确认 `{dataRoot}/logs/{projectId}.log` 文件每行带 ISO 8601 时间戳
- 触发 stderr 输出（如 JS 运行时 `console.warn`），确认日志行包含 `[stderr]` 前缀
- 长时间运行 keepalive 实例，写入超过 10MB 日志，确认 `.log.1` 轮转文件生成
- 通过 `GET /api/projects/{id}/logs` 和 `GET /api/instances/{id}/logs` 读取日志，确认 API 正常返回（时间戳前缀不影响 `readTailLines`）

建议在 `test_instance_manager.cpp` 中补充自动化集成用例：构造真实 `QProcess` + `InstanceLogWriter`，验证信号驱动写入的端到端行为（stdout/stderr 分别触发 `readyReadStandardOutput/Error` 信号后日志文件内容正确）。

### 6.3 验收标准

- [ ] Instance stdout 每行日志带 ISO 8601 UTC 毫秒时间戳（T01, T05）
- [ ] Instance stderr 每行日志带 `[stderr]` 前缀（T02）
- [ ] stdout 与 stderr 混合写入时格式各自正确（T03）
- [ ] 不完整行正确缓冲，补齐后写入（T04）
- [ ] 日志文件超过 10MB 后自动轮转（T06）
- [ ] 进程退出时残留缓冲被 flush（T07）
- [ ] 日志路径不变，现有 API 端点无需修改
- [ ] 全量既有测试无回归

## 7. 风险与控制

- 风险: 信号驱动方式下数据经过 server 进程内存，高频输出可能增加内存压力
  - 控制: Instance 输出通常是低频 JSONL（每秒几行到几十行），内存开销可忽略
  - 控制: `readAllStandardOutput()` 在信号回调中立即消费，不会累积
  - 控制: 行缓冲区设有 1MB 上限（`kMaxBufferBytes`），超限时强制刷出，防止子进程输出不含换行导致内存无限增长

- 风险: `QProcess::readyReadStandardOutput` 信号在行中间触发，导致日志行被截断
  - 控制: 行缓冲机制（`m_stdoutBuf`/`m_stderrBuf`）确保只写入完整行
  - 测试覆盖: T04

- 风险: 多个 Instance 使用相同 projectId 时 spdlog logger 名称冲突
  - 控制: logger 名称使用完整路径 `"inst_{logPath}"`，同一 projectId 的多个 Instance 共享同一日志文件路径，spdlog `rotating_file_sink_mt` 线程安全
  - 控制: 同一 projectId 同时只有一个 Instance 运行（ScheduleEngine 保证）

- 风险: 析构时 flush 残留缓冲可能写入不完整的 JSON 行
  - 控制: 这是预期行为——不完整行本身就是异常退出的证据，保留比丢弃更有诊断价值
  - 测试覆盖: T07

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] `InstanceLogWriter` 正确创建 spdlog rotating logger 并写入带时间戳的日志
- [ ] `InstanceManager::startInstance()` 从文件重定向切换为信号驱动
- [ ] Instance stdout 每行带 ISO 8601 UTC 时间戳
- [ ] Instance stderr 每行带 `[stderr]` 前缀
- [ ] 行缓冲正确处理不完整行
- [ ] 日志文件自动轮转（默认 10MB，保留 3 个历史文件）
- [ ] 全量既有测试无回归
- [ ] 文档同步完成
- [ ] 向后兼容确认（日志路径不变，API 端点无需修改）
