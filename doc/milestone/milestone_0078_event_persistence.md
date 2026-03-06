# 里程碑 78：事件持久化与历史查询

> **前置条件**: M76 (spdlog 集成)、M34 (Server 基础架构)、M48 (SSE 事件推送)
> **目标**: 将 EventBus 事件持久化到 JSONL 文件，新增历史事件查询 API，SSE 实时推送不受影响

## 1. 目标

- EventBus 事件自动持久化到 `{dataRoot}/logs/events.jsonl` 文件
- 每行一个完整 JSON 对象，包含事件类型、数据和时间戳
- 基于 spdlog `rotating_file_sink` 实现自动轮转（默认 5MB，保留 2 个历史文件）
- 新增 `GET /api/events` 端点，支持按类型前缀和 projectId 过滤的历史查询
- SSE 实时推送逻辑不受影响（EventLog 是 EventBus 的并行消费者）

## 2. 背景与问题

- 当前 `EventBus` 通过信号 `eventPublished` 将事件推送给 `EventStreamHandler`（SSE），事件仅在内存中流转
- SSE 连接断开后，客户端无法获取断开期间的历史事件
- 服务重启后所有事件丢失，无法事后排查问题时间线
- 当前 5 种事件类型：`instance.started`、`instance.finished`、`instance.startFailed`、`project.updated`、`project.deleted`
- WebUI Dashboard 需要展示最近事件列表，当前只能依赖 SSE 实时流，刷新页面后丢失

**范围**:
- `EventLog` 类：基于 spdlog 的事件持久化写入器 + JSONL 查询
- `ApiRouter` 扩展：新增 `GET /api/events` 端点
- `ServerManager` 扩展：创建 EventLog 实例并连接 EventBus

**非目标**:
- 不修改 EventBus 发布逻辑
- 不修改 SSE EventStreamHandler（实时推送不受影响）
- 不修改现有 5 种事件的数据结构
- 不实现事件回放（replay）机制

## 3. 技术要点

### 3.1 EventLog 作为并行消费者

```
  EventBus::publish()
       │
       ▼
  emit eventPublished(event)
       │
       ├──────────────────────► EventStreamHandler (SSE 实时推送，不变)
       │
       └──────────────────────► EventLog (新增，JSONL 持久化)
```

EventLog 通过 `connect(bus, &EventBus::eventPublished, this, &EventLog::onEventPublished)` 订阅事件。与 EventStreamHandler 并行消费，互不干扰。

### 3.2 JSONL 存储格式

每行一个完整 JSON 对象：

```json
{"type":"instance.started","data":{"instanceId":"inst_abc12345","projectId":"proj1","pid":1234},"ts":"2026-02-25T06:19:51.123Z"}
```

spdlog pattern 设为 `"%v"`（纯内容，无额外前缀），确保文件中每行都是合法 JSON，便于 `query()` 解析。

选择 JSONL 而非 SQLite 的理由：
- 无额外依赖（SQLite 需要引入新库）
- grep 友好，运维人员可直接用命令行工具查看
- 与项目 JSONL 协议哲学一致
- 当前 5 种事件类型的量级下，尾部读取+内存过滤的性能完全足够

### 3.3 EventLog 数据结构

```cpp
class EventLog : public QObject {
    Q_OBJECT
public:
    explicit EventLog(const QString& logPath, EventBus* bus,
                      qint64 maxBytes = 5 * 1024 * 1024,
                      int maxFiles = 2,
                      QObject* parent = nullptr);
    ~EventLog() override;

    QJsonArray query(int limit = 100,
                     const QString& typePrefix = QString(),
                     const QString& projectId = QString()) const;
    QString logPath() const;

private slots:
    void onEventPublished(const ServerEvent& event);

private:
    std::shared_ptr<spdlog::logger> m_logger;
    QString m_logPath;
};
```

- 默认轮转阈值 5MB（事件数据量远小于 Instance 日志），保留 2 个历史文件
- `query()` 从文件尾部读取，解析 JSONL，按条件过滤，返回 newest-first 结果

### 3.4 查询 API 设计

```
GET /api/events?limit=100&type=instance&projectId=proj1
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `limit` | int | 100 | 最多返回条数（上限 1000） |
| `type` | string | — | 按事件类型前缀过滤（如 `instance` 匹配 `instance.started` 和 `instance.finished`） |
| `projectId` | string | — | 按 `data.projectId` 字段过滤 |

响应格式：
```json
{
  "events": [
    {"type":"instance.started","data":{...},"ts":"2026-02-25T06:19:51.123Z"},
    {"type":"instance.finished","data":{...},"ts":"2026-02-25T06:19:50.000Z"}
  ],
  "count": 2
}
```

### 3.5 向后兼容

- EventBus 发布逻辑不变，现有 SSE 推送不受影响
- 新增 API 端点 `GET /api/events` 不与现有端点冲突
- `config.json` 无需新增字段（EventLog 复用 M76 的 `logMaxBytes`/`logMaxFiles` 或使用独立默认值）
- 事件数据结构（`ServerEvent`）不变

## 4. 实现步骤

### 4.1 新增 `event_log.h` — EventLog 类声明

- 新增 `src/stdiolink_server/http/event_log.h`：
  ```cpp
  #pragma once
  #include <QJsonArray>
  #include <QObject>
  #include <QString>
  #include <memory>
  #include <spdlog/spdlog.h>

  #include "event_bus.h"

  namespace stdiolink_server {

  class EventLog : public QObject {
      Q_OBJECT
  public:
      explicit EventLog(const QString& logPath, EventBus* bus,
                        qint64 maxBytes = 5 * 1024 * 1024,
                        int maxFiles = 2,
                        QObject* parent = nullptr);
      ~EventLog() override;

      QJsonArray query(int limit = 100,
                       const QString& typePrefix = QString(),
                       const QString& projectId = QString()) const;
      QString logPath() const { return m_logPath; }

  private slots:
      void onEventPublished(const ServerEvent& event);

  private:
      std::shared_ptr<spdlog::logger> m_logger;
      QString m_logPath;
  };

  } // namespace stdiolink_server
  ```
  - 理由：放在 `http/` 目录下与 `event_bus.h`、`event_stream_handler.h` 同级，保持事件相关代码的内聚性

### 4.2 新增 `event_log.cpp` — 事件持久化与 JSONL 查询

- 新增 `src/stdiolink_server/http/event_log.cpp`：
  - 构造函数：创建 spdlog rotating logger，连接 EventBus 信号：
    ```cpp
    #include "event_log.h"
    #include <QFile>
    #include <QJsonDocument>
    #include <QJsonObject>
    #include <spdlog/sinks/rotating_file_sink.h>

    namespace stdiolink_server {

    EventLog::EventLog(const QString& logPath, EventBus* bus,
                       qint64 maxBytes, int maxFiles, QObject* parent)
        : QObject(parent), m_logPath(logPath)
    {
        try {
            auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                logPath.toStdString(),
                static_cast<size_t>(maxBytes),
                static_cast<size_t>(maxFiles));

            m_logger = std::make_shared<spdlog::logger>("event_log", sink);
            m_logger->set_pattern("%v");  // 纯内容，无前缀
            m_logger->set_level(spdlog::level::info);
            m_logger->flush_on(spdlog::level::info);
        } catch (const spdlog::spdlog_ex& ex) {
            qWarning("EventLog: failed to create logger: %s", ex.what());
            // m_logger 为空，onEventPublished 中需检查
        }

        connect(bus, &EventBus::eventPublished,
                this, &EventLog::onEventPublished);
    }

    EventLog::~EventLog() {
        if (m_logger) {
            spdlog::drop(m_logger->name());
        }
    }
    ```
  - 事件序列化写入：
    ```cpp
    void EventLog::onEventPublished(const ServerEvent& event) {
        if (!m_logger) return;  // logger 创建失败时静默跳过
        QJsonObject record;
        record["type"] = event.type;
        record["data"] = event.data;
        record["ts"] = event.timestamp.toString(Qt::ISODateWithMs);
        const QByteArray json = QJsonDocument(record).toJson(QJsonDocument::Compact);
        m_logger->info("{}", json.constData());
    }
    ```
  - JSONL 查询实现（从文件尾部读取，newest-first）：
    ```cpp
    QJsonArray EventLog::query(int limit,
                                const QString& typePrefix,
                                const QString& projectId) const {
        QFile file(m_logPath);
        if (!file.open(QIODevice::ReadOnly)) return {};

        // 从文件尾部读取最多 4MB 窗口
        constexpr qint64 kMaxReadBytes = 4 * 1024 * 1024;
        const qint64 fileSize = file.size();
        const qint64 startPos = qMax(qint64(0), fileSize - kMaxReadBytes);
        file.seek(startPos);
        const QByteArray data = file.readAll();

        // 按行分割，逆序遍历
        QJsonArray results;
        int pos = data.size();
        while (pos > 0 && results.size() < limit) {
            int lineEnd = pos;
            pos--;
            while (pos > 0 && data[pos - 1] != '\n') pos--;
            const QByteArray line = data.mid(pos, lineEnd - pos).trimmed();
            if (line.isEmpty()) continue;

            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isObject()) continue;
            const QJsonObject obj = doc.object();

            // 类型前缀过滤
            if (!typePrefix.isEmpty()) {
                if (!obj.value("type").toString().startsWith(typePrefix)) continue;
            }
            // projectId 过滤
            if (!projectId.isEmpty()) {
                if (obj.value("data").toObject().value("projectId").toString()
                    != projectId) continue;
            }
            results.append(obj);
        }
        return results;
    }

    } // namespace stdiolink_server
    ```
  - 理由：
    - `%v` pattern 确保每行是纯 JSON，无需剥离时间戳前缀
    - 从文件尾部读取 4MB 窗口，与 `readTailLines()` 策略一致
    - 逆序遍历实现 newest-first 排序，无需全量加载+排序

### 4.3 修改 `server_manager.h/.cpp` — 创建 EventLog 实例

- 修改 `src/stdiolink_server/server_manager.h`：
  - 新增前置声明：`class EventLog;`
  - 新增成员：`EventLog* m_eventLog = nullptr;`
  - 新增访问器：`EventLog* eventLog() { return m_eventLog; }`

- 修改 `src/stdiolink_server/server_manager.cpp`：
  - 在 `initialize()` 中创建 EventLog 实例：
    ```cpp
    #include "http/event_log.h"

    // 在 EventBus 创建之后
    const QString eventsPath = m_dataRoot + "/logs/events.jsonl";
    m_eventLog = new EventLog(eventsPath, m_eventBus, 5 * 1024 * 1024, 2, this);
    ```
  - 理由：EventLog 作为 ServerManager 的子对象，生命周期由 Qt 父子关系管理

### 4.4 修改 `api_router.cpp` — 新增 `GET /api/events` 端点

- 修改 `src/stdiolink_server/http/api_router.h`：
  - 新增私有方法声明：
    ```cpp
    QHttpServerResponse handleEventList(const QHttpServerRequest& req);
    ```

- 修改 `src/stdiolink_server/http/api_router.cpp`：
  - 新增头文件引用：
    ```cpp
    #include "event_log.h"
    ```
  - 在 `registerRoutes()` 中注册路由：
    ```cpp
    server.route("/api/events", Method::Get,
        [this](const QHttpServerRequest& req) {
            return handleEventList(req);
        });
    ```
  - 实现 `handleEventList()`：
    ```cpp
    QHttpServerResponse ApiRouter::handleEventList(
            const QHttpServerRequest& req) {
        const QUrlQuery query(req.url());
        const int rawLimit = query.queryItemValue("limit").toInt();
        const int limit = rawLimit > 0 ? qBound(1, rawLimit, 1000) : 100;
        const QString typePrefix = query.queryItemValue("type");
        const QString projectId = query.queryItemValue("projectId");

        auto* eventLog = m_manager->eventLog();
        if (!eventLog) {
            return errorResponse(
                QHttpServerResponse::StatusCode::InternalServerError,
                "event log not initialized");
        }

        const QJsonArray events = eventLog->query(
            limit, typePrefix, projectId);

        QJsonObject result;
        result["events"] = events;
        result["count"] = events.size();
        return jsonResponse(result);
    }
    ```
  - 理由：复用现有 `errorResponse`/`jsonResponse` 辅助函数，与其他 API 端点风格一致

### 4.5 修改 CMakeLists.txt — 添加新源文件

- 修改 `src/stdiolink_server/CMakeLists.txt`：
  - 在 `STDIOLINK_SERVER_SOURCES` 中添加：
    ```cmake
    http/event_log.cpp
    ```
  - 在 `STDIOLINK_SERVER_HEADERS` 中添加：
    ```cmake
    http/event_log.h
    ```
  - 注意：spdlog 依赖已在 M76 中通过 vcpkg + find_package 引入并链接，本里程碑无需重复添加

- 修改 `src/tests/CMakeLists.txt`：
  - 在 `TEST_SOURCES` 中添加：
    ```cmake
    test_event_log.cpp
    ```
  - 在 `target_sources` 中添加：
    ```cmake
    ${CMAKE_SOURCE_DIR}/src/stdiolink_server/http/event_log.cpp
    ```

## 5. 文件变更清单

### 5.1 新增文件
- `src/stdiolink_server/http/event_log.h` — EventLog 类声明
- `src/stdiolink_server/http/event_log.cpp` — spdlog JSONL 写入 + 查询实现

### 5.2 修改文件
- `src/stdiolink_server/server_manager.h` — 新增 `EventLog*` 成员和访问器
- `src/stdiolink_server/server_manager.cpp` — 创建 EventLog 实例并连接 EventBus
- `src/stdiolink_server/http/api_router.h` — 新增 `handleEventList` 方法声明
- `src/stdiolink_server/http/api_router.cpp` — 注册 `GET /api/events` 路由 + 实现
- `src/stdiolink_server/CMakeLists.txt` — 添加新源文件
- `src/tests/CMakeLists.txt` — 添加测试文件 + 新源文件

### 5.3 测试文件
- `src/tests/test_event_log.cpp` — EventLog 单元测试

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: `EventLog` 的事件持久化、JSONL 格式、查询过滤、轮转行为
- 用例分层: 正常路径（T01–T03）、过滤查询（T04–T05）、边界值（T06）、轮转（T07）
- 断言要点: JSONL 文件存在性、每行 JSON 合法性、查询结果正确性、轮转文件生成
- 桩替身策略: 使用 `QTemporaryDir` 创建临时目录；直接构造 `EventBus` + `EventLog` 实例
- 测试文件: `src/tests/test_event_log.cpp`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `onEventPublished`: 事件写入 | JSONL 文件包含合法 JSON 行 | T01 |
| `onEventPublished`: 多事件写入 | 每行独立 JSON，包含 type/data/ts | T02 |
| `query`: 无过滤 | 返回所有事件，newest-first | T03 |
| `query`: type 前缀过滤 | 仅返回匹配类型的事件 | T04 |
| `query`: projectId 过滤 | 仅返回匹配 projectId 的事件 | T05 |
| `query`: limit 限制 | 返回条数不超过 limit | T06 |
| 文件超过 maxBytes | 轮转文件 `.jsonl.1` 生成 | T07 |

#### 用例详情

**T01 — 单事件写入 JSONL 文件**
- 前置条件: `QTemporaryDir` 创建临时目录，构造 `EventBus` + `EventLog(tmpDir + "/events.jsonl", &bus)`
- 输入: `bus.publish("instance.started", {{"instanceId", "inst_1"}, {"projectId", "proj1"}, {"pid", 1234}})`
- 预期: `events.jsonl` 包含 1 行合法 JSON，含 `type`、`data`、`ts` 字段
- 断言: 文件行数 == 1；JSON 解析成功；`type == "instance.started"`；`data.instanceId == "inst_1"`

**T02 — 多事件写入，每行独立 JSON**
- 前置条件: 同 T01
- 输入: 依次发布 3 个不同类型的事件
- 预期: 文件包含 3 行，每行独立解析为合法 JSON
- 断言: 文件行数 == 3；每行 JSON 解析成功；`ts` 字段为 ISO 8601 格式

**T03 — query 无过滤返回所有事件（newest-first）**
- 前置条件: 同 T01，发布 3 个事件（type 分别为 `a`、`b`、`c`）
- 输入: `eventLog.query(100)`
- 预期: 返回 3 条，顺序为 `c`、`b`、`a`（newest-first）
- 断言: 结果数组大小 == 3；`results[0].type == "c"`；`results[2].type == "a"`

**T04 — query 按 type 前缀过滤**
- 前置条件: 发布 `instance.started`、`instance.finished`、`project.updated` 三个事件
- 输入: `eventLog.query(100, "instance")`
- 预期: 仅返回 2 条 `instance.*` 事件
- 断言: 结果数组大小 == 2；每条 `type` 均以 `"instance"` 开头

**T05 — query 按 projectId 过滤**
- 前置条件: 发布 2 个事件，data.projectId 分别为 `"proj1"` 和 `"proj2"`
- 输入: `eventLog.query(100, QString(), "proj1")`
- 预期: 仅返回 projectId == "proj1" 的 1 条事件
- 断言: 结果数组大小 == 1；`results[0].data.projectId == "proj1"`

**T06 — query limit 限制返回条数**
- 前置条件: 发布 5 个事件
- 输入: `eventLog.query(2)`
- 预期: 仅返回最新的 2 条事件
- 断言: 结果数组大小 == 2

**T07 — 文件轮转**
- 前置条件: 构造 `EventLog(tmpDir + "/events.jsonl", &bus, 1024, 2)`（1KB 轮转阈值）
- 输入: 循环发布足够多的事件，总量超过 1KB
- 预期: `events.jsonl.1` 文件存在
- 断言: `QFileInfo(tmpDir + "/events.jsonl.1").exists()` 为 true

#### 测试代码

```cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include "stdiolink_server/http/event_bus.h"
#include "stdiolink_server/http/event_log.h"

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

TEST(EventLogTest, SingleEventWrittenToFile) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/events.jsonl";

    EventBus bus;
    {
        EventLog log(logPath, &bus);
        bus.publish("instance.started",
                    {{"instanceId", "inst_1"}, {"projectId", "proj1"}, {"pid", 1234}});
    }

    const auto lines = readLogLines(logPath);
    ASSERT_EQ(lines.size(), 1);
    const QJsonDocument doc = QJsonDocument::fromJson(lines[0].toUtf8());
    ASSERT_TRUE(doc.isObject());
    const QJsonObject obj = doc.object();
    EXPECT_EQ(obj.value("type").toString(), "instance.started");
    EXPECT_EQ(obj.value("data").toObject().value("instanceId").toString(), "inst_1");
    EXPECT_TRUE(obj.contains("ts"));
    // 验证 ts 为 ISO 8601 格式
    const QString ts = obj.value("ts").toString();
    QRegularExpression tsRe(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3})");
    EXPECT_TRUE(tsRe.match(ts).hasMatch()) << "ts not ISO 8601: " << qPrintable(ts);
}

TEST(EventLogTest, MultipleEventsEachLineValidJson) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/events.jsonl";

    EventBus bus;
    {
        EventLog log(logPath, &bus);
        bus.publish("instance.started", {{"projectId", "p1"}});
        bus.publish("instance.finished", {{"projectId", "p1"}});
        bus.publish("project.updated", {{"projectId", "p2"}});
    }

    const auto lines = readLogLines(logPath);
    ASSERT_EQ(lines.size(), 3);
    for (const auto& line : lines) {
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        ASSERT_TRUE(doc.isObject());
        EXPECT_TRUE(doc.object().contains("type"));
        EXPECT_TRUE(doc.object().contains("data"));
        EXPECT_TRUE(doc.object().contains("ts"));
    }
}

TEST(EventLogTest, QueryNoFilterNewestFirst) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/events.jsonl";

    EventBus bus;
    EventLog log(logPath, &bus);
    bus.publish("a", {});
    bus.publish("b", {});
    bus.publish("c", {});

    const QJsonArray results = log.query(100);
    ASSERT_EQ(results.size(), 3);
    EXPECT_EQ(results[0].toObject().value("type").toString(), "c");
    EXPECT_EQ(results[2].toObject().value("type").toString(), "a");
}

TEST(EventLogTest, QueryFilterByTypePrefix) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/events.jsonl";

    EventBus bus;
    EventLog log(logPath, &bus);
    bus.publish("instance.started", {{"projectId", "p1"}});
    bus.publish("instance.finished", {{"projectId", "p1"}});
    bus.publish("project.updated", {{"projectId", "p2"}});

    const QJsonArray results = log.query(100, "instance");
    ASSERT_EQ(results.size(), 2);
    for (int i = 0; i < results.size(); ++i) {
        EXPECT_TRUE(results[i].toObject().value("type")
                        .toString().startsWith("instance"));
    }
}

TEST(EventLogTest, QueryFilterByProjectId) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/events.jsonl";

    EventBus bus;
    EventLog log(logPath, &bus);
    bus.publish("instance.started", {{"projectId", "proj1"}});
    bus.publish("instance.started", {{"projectId", "proj2"}});

    const QJsonArray results = log.query(100, QString(), "proj1");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].toObject().value("data").toObject()
                  .value("projectId").toString(), "proj1");
}

TEST(EventLogTest, QueryLimitReturnsAtMostN) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/events.jsonl";

    EventBus bus;
    EventLog log(logPath, &bus);
    for (int i = 0; i < 5; ++i) {
        bus.publish("event", {{"index", i}});
    }

    const QJsonArray results = log.query(2);
    EXPECT_EQ(results.size(), 2);
}

TEST(EventLogTest, FileRotation) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = tmpDir.path() + "/events.jsonl";

    EventBus bus;
    {
        EventLog log(logPath, &bus, 1024, 2);
        // 发布足够多事件超过 1KB 触发轮转
        for (int i = 0; i < 30; ++i) {
            bus.publish("instance.started",
                        {{"projectId", "proj1"}, {"index", i},
                         {"padding", "xxxxxxxxxxxxxxxxxxxx"}});
        }
    }

    EXPECT_TRUE(QFileInfo::exists(logPath + ".1"));
}
```

### 6.2 集成测试

- 启动 server，触发实例启动/停止事件，检查 `{dataRoot}/logs/events.jsonl` 文件存在且每行为合法 JSON
- 调用 `GET /api/events` 确认返回历史事件数据
- 带 `type=instance` 参数调用，确认仅返回 `instance.*` 类型事件
- 带 `projectId=proj1` 参数调用，确认仅返回对应项目的事件
- 确认 SSE 实时推送不受影响（EventStreamHandler 仍正常工作）

建议在 `test_api_router.cpp` 中补充自动化 API 端点用例：构造 `ServerManager` + `EventLog`，通过 `QHttpServer` 发送 `GET /api/events` 请求，验证响应 JSON 格式、过滤参数和 limit 边界行为。

### 6.3 验收标准

- [ ] EventBus 事件自动持久化到 `events.jsonl` 文件（T01, T02）
- [ ] 每行为合法 JSON，包含 type、data、ts 字段（T02）
- [ ] `GET /api/events` 返回历史事件，newest-first 排序（T03）
- [ ] type 前缀过滤正确（T04）
- [ ] projectId 过滤正确（T05）
- [ ] limit 参数限制返回条数（T06）
- [ ] 文件超过阈值后自动轮转（T07）
- [ ] SSE 实时推送不受影响
- [ ] 全量既有测试无回归

## 7. 风险与控制

- 风险: 高频事件发布导致 JSONL 文件快速增长
  - 控制: 当前仅 5 种事件类型，频率与实例启停相关（每分钟最多几次），远低于轮转阈值
  - 控制: 默认 5MB 轮转 + 保留 2 个历史文件，最大磁盘占用 15MB

- 风险: `query()` 从文件尾部读取 4MB 窗口，大文件时内存开销
  - 控制: 4MB 窗口与 `readTailLines()` 策略一致，已在生产中验证
  - 控制: 事件 JSON 单行约 200 字节，4MB 窗口可容纳约 2 万条事件，远超实际需求

- 风险: spdlog logger 名称 `"event_log"` 与 M76/M77 的 logger 冲突
  - 控制: spdlog named logger 按名称隔离，`"event_log"` 与 `"server"`（M76）和 `"inst_*"`（M77）不重名
  - 测试覆盖: T01

- 风险: 轮转后 `query()` 只读取当前文件，历史轮转文件中的事件丢失
  - 控制: 这是预期行为——`query()` 定位为"最近事件查询"，不是全量历史归档
  - 控制: 5MB 文件可存储约 2.5 万条事件，覆盖数周的正常使用量

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] EventLog 正确订阅 EventBus 并将事件持久化到 JSONL 文件
- [ ] 每行为合法 JSON，包含 type、data、ts 字段
- [ ] `GET /api/events` 端点正常工作，支持 limit、type、projectId 过滤
- [ ] 查询结果 newest-first 排序
- [ ] JSONL 文件自动轮转（默认 5MB，保留 2 个历史文件）
- [ ] SSE 实时推送不受影响
- [ ] 全量既有测试无回归
- [ ] 文档同步完成
- [ ] 向后兼容确认（EventBus 不变，现有 API 端点不变）
