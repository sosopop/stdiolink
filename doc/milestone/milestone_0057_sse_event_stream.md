# 里程碑 57：SSE 实时事件流

> **前置条件**: 里程碑 49–51 已完成（CORS、Dashboard、Project 操作已就绪）
> **优先级**: P1（所有 P0 里程碑完成后实施）
> **目标**: 实现 `GET /api/events/stream` SSE 端点，推送系统级实时事件，降低前端轮询频率

---

## 1. 目标

- 实现 `EventBus` 全局事件分发器，收集各 Manager 的信号
- 实现 `EventStreamHandler` 管理 SSE 连接和事件推送
- 实现 `GET /api/events/stream` — SSE（Server-Sent Events）端点
- 支持 `?filter=instance,project` 按事件类型过滤
- 连接断开时自动清理资源

---

## 2. 背景与问题

当前 WebUI 需要轮询 API 获取状态变更（Instance 启停、Project 状态变更等），实时性差且增加服务端负载。SSE 提供轻量级的服务端推送能力，浏览器原生支持 `EventSource` API，实现简单。

**技术前置**：基于 `QHttpServerResponder` 的 chunked 写入实现 SSE；本里程碑不引入独立 WebSocket 替代通道。

---

## 3. 技术要点

### 3.1 事件类型

| 事件名 | 触发条件 | 数据 |
|--------|----------|------|
| `instance.started` | Instance 启动 | `{ instanceId, projectId, pid }` |
| `instance.finished` | Instance 退出 | `{ instanceId, projectId, exitCode, status }` |
| `project.status_changed` | Project 状态变更 | `{ projectId, oldStatus, newStatus }` |
| `service.scanned` | Service 扫描完成 | `{ added, removed, updated }` |
| `driver.scanned` | Driver 扫描完成 | `{ scanned, updated }` |
| `schedule.triggered` | 调度触发 | `{ projectId, scheduleType }` |
| `schedule.suppressed` | 调度被抑制 | `{ projectId, reason, consecutiveFailures }` |

### 3.2 SSE 响应格式

```
HTTP/1.1 200 OK
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive

event: instance.started
data: {"instanceId":"inst_abc","projectId":"silo-a","pid":12345}

event: instance.finished
data: {"instanceId":"inst_abc","projectId":"silo-a","exitCode":0,"status":"stopped"}
```

### 3.3 QHttpServer SSE 支持

本里程碑采用 `QHttpServerResponder` 的 chunked 写入能力实现 SSE：

- 连接建立时调用 chunked 写入 API 发送 `text/event-stream` 响应头
- 事件到达时调用 chunk 写入持续推送 `event: ...\ndata: ...\n\n`
- 连接关闭时结束 chunked 传输并清理连接对象

> **实现前须验证**：`QHttpServerResponder` 的 chunked 写入 API 名称和签名需以 Qt 6.10.0 实际头文件为准。文档中使用的 `writeBeginChunked()`/`writeChunk()`/`writeEndChunked()` 为推测名称，实际可能不同。建议先编写最小 demo 验证 chunked 写入流程和 responder 生命周期管理。

不使用 `QTcpSocket*` 绕过 QHttpServer 的实现，避免引入额外生命周期管理风险。

### 3.3.1 心跳与断连检测

SSE 连接断开时，服务端可能无法立即感知（TCP 半开连接问题）。通过定期发送 SSE 注释行作为心跳：

```
: heartbeat\n\n
```

每 30 秒发送一次。如果 `writeChunk()` 返回写入失败，则认为连接已断开，触发清理。

### 3.3.2 连接数限制

SSE 连接数上限 `kMaxSseConnections = 32`。超出时返回 `429 Too Many Requests`。每次新连接建立前检查 `activeConnectionCount() >= kMaxSseConnections`。

### 3.3.3 已知局限：无事件 ID

SSE 标准支持 `id:` 字段用于断线重连（客户端通过 `Last-Event-Id` 请求头恢复丢失事件）。首版不实现事件 ID 机制——断线重连不保证不丢事件。客户端断线重连后应主动调用 REST API 刷新完整状态。后续可引入自增事件 ID + 有限环形缓冲区实现断线恢复。

### 3.4 过滤机制

```
GET /api/events/stream?filter=instance,project
```

`filter` 参数指定感兴趣的事件前缀。如 `instance` 匹配 `instance.started` 和 `instance.finished`。为空则接收所有事件。

---

## 4. 实现方案

### 4.1 EventBus

```cpp
// src/stdiolink_server/http/event_bus.h
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace stdiolink_server {

struct ServerEvent {
    QString type;       // "instance.started" etc
    QJsonObject data;
    QDateTime timestamp;
};

class EventBus : public QObject {
    Q_OBJECT
public:
    explicit EventBus(QObject* parent = nullptr);

    /// 发布事件
    void publish(const QString& type, const QJsonObject& data);

signals:
    /// 所有 SSE 连接监听此信号
    void eventPublished(const ServerEvent& event);
};

} // namespace stdiolink_server
```

### 4.2 EventStreamHandler

```cpp
// src/stdiolink_server/http/event_stream_handler.h
#pragma once

#include <QObject>
#include <QHttpServerResponder>
#include <QSet>
#include <QVector>

namespace stdiolink_server {

class EventBus;

class EventStreamConnection : public QObject {
    Q_OBJECT
public:
    EventStreamConnection(QHttpServerResponder&& responder,
                          const QSet<QString>& filters,
                          QObject* parent = nullptr);
    ~EventStreamConnection();

    void beginStream();
    void sendEvent(const ServerEvent& event);
    bool matchesFilter(const QString& eventType) const;
    bool isOpen() const;

private:
    QHttpServerResponder m_responder;
    QSet<QString> m_filters;
    bool m_open = true;
};

class EventStreamHandler : public QObject {
    Q_OBJECT
public:
    explicit EventStreamHandler(EventBus* bus, QObject* parent = nullptr);

    int activeConnectionCount() const;
    static constexpr int kMaxSseConnections = 32;

private slots:
    void onEventPublished(const ServerEvent& event);
    void onConnectionDisconnected(EventStreamConnection* conn);

private:
    EventBus* m_bus;
    QVector<EventStreamConnection*> m_connections;
};

} // namespace stdiolink_server
```

### 4.3 信号连接

在 `ServerManager` 初始化时，将各 Manager 的信号连接到 `EventBus`：

```cpp
// InstanceManager 信号（已有，参数需适配）
connect(m_instanceManager, &InstanceManager::instanceStarted,
        m_eventBus, [this](const QString& instanceId, const QString& projectId) {
    // pid 需从 Instance 对象获取（信号本身不携带 pid）
    qint64 pid = 0;
    if (auto* inst = m_instanceManager->getInstance(instanceId))
        pid = inst->pid;
    m_eventBus->publish("instance.started", QJsonObject{
        {"instanceId", instanceId},
        {"projectId", projectId},
        {"pid", pid}
    });
});

connect(m_instanceManager, &InstanceManager::instanceFinished,
        m_eventBus, [this](const QString& instanceId, const QString& projectId,
                           int exitCode, QProcess::ExitStatus exitStatus) {
    m_eventBus->publish("instance.finished", QJsonObject{
        {"instanceId", instanceId},
        {"projectId", projectId},
        {"exitCode", exitCode},
        {"status", exitStatus == QProcess::NormalExit ? "normal" : "crashed"}
    });
});

// ScheduleEngine 信号（需新增）类似连接...
```

### 4.4 所需的 Manager 信号补充

`InstanceManager` 已有 `instanceStarted` 和 `instanceFinished` 信号，但参数签名与 EventBus 所需不完全匹配：

| 类 | 现有信号 | 需要调整 |
|----|----------|----------|
| `InstanceManager` | `instanceStarted(instanceId, projectId)` | 缺少 `pid` 参数，需补充或在 EventBus 连接时从 Instance 对象获取 |
| `InstanceManager` | `instanceFinished(instanceId, projectId, exitCode, exitStatus)` | `exitStatus` 是 `QProcess::ExitStatus` 枚举，需转换为字符串 |

其他 Manager 可能缺少必要的信号：

| 类 | 信号 | 说明 |
|----|------|------|
| `ScheduleEngine` | `scheduleTriggered(projectId, scheduleType)` | 调度触发（需新增） |
| `ScheduleEngine` | `scheduleSuppressed(projectId, reason, failures)` | 调度抑制（需新增） |

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/stdiolink_server/http/event_bus.h`
- `src/stdiolink_server/http/event_bus.cpp`
- `src/stdiolink_server/http/event_stream_handler.h`
- `src/stdiolink_server/http/event_stream_handler.cpp`

### 5.2 修改文件

- `src/stdiolink_server/server_manager.h` — 新增 `EventBus*` 成员
- `src/stdiolink_server/server_manager.cpp` — 初始化 EventBus、连接信号
- `src/stdiolink_server/manager/instance_manager.h` — 补充信号声明
- `src/stdiolink_server/manager/instance_manager.cpp` — 在适当位置 emit 信号
- `src/stdiolink_server/manager/schedule_engine.h` — 补充信号声明
- `src/stdiolink_server/manager/schedule_engine.cpp` — 在适当位置 emit 信号
- `src/stdiolink_server/http/api_router.cpp` — 注册 SSE 路由
- `src/stdiolink_server/CMakeLists.txt` — 新增源文件

### 5.3 测试文件

- 新增 `src/tests/test_event_bus.cpp`
- 修改 `src/tests/test_api_router.cpp` — SSE 连接测试（如可行）

---

## 6. 测试与验收

### 6.1 单元测试场景

**EventBus（test_event_bus.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | publish 事件后信号触发 | eventPublished 信号参数正确 |
| 2 | 事件包含 timestamp | ServerEvent.timestamp 有效 |
| 3 | 多次 publish 多次触发 | 每次 publish 独立触发信号 |
| 4 | 连接过滤器 `instance` 匹配 `instance.started` | matchesFilter 返回 true |
| 5 | 过滤器 `instance` 不匹配 `project.status_changed` | matchesFilter 返回 false |
| 6 | 空过滤器匹配所有事件 | matchesFilter 始终返回 true |
| 7 | 多个过滤器 `instance,project` | 匹配两类事件 |

**Manager 信号触发**：

| # | 场景 | 验证点 |
|---|------|--------|
| 8 | Instance 启动时 emit instanceStarted | 信号参数含 instanceId/projectId/pid |
| 9 | Instance 退出时 emit instanceFinished | 信号参数含 exitCode/status |
| 10 | 调度触发时 emit scheduleTriggered | 信号参数含 projectId/scheduleType |
| 11 | 调度抑制时 emit scheduleSuppressed | 信号参数含 reason/failures |

**SSE API（test_api_router.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 12 | `GET /api/events/stream` | 响应 Content-Type 为 `text/event-stream` |
| 13 | 事件推送格式 | `event: type\ndata: json\n\n` |
| 14 | filter 参数过滤 | 仅收到匹配的事件 |
| 15 | 客户端断开后清理 | 连接数递减，无资源泄漏 |

### 6.2 验收标准

- EventBus 正确分发事件
- 各 Manager 在关键节点 emit 信号
- SSE 端点可接受连接并推送事件
- 过滤机制工作正常
- 连接断开后自动清理
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：SSE chunked 写入 API 名称/签名与实际不符
  - 控制：实现前先编写最小 demo 验证 `QHttpServerResponder` 的 chunked 写入流程；确认 responder 的所有权语义（move-only 还是可拷贝）
  - **降级方案**：如 `QHttpServerResponder` 不支持 chunked streaming，改用 M55 已验证的 WebSocket 通道推送事件（复用 `addWebSocketUpgradeVerifier` 基础设施），或退回到客户端短轮询（`GET /api/events/poll?since=<timestamp>`）
- **风险 2**：SSE 连接断开后服务端无法及时感知（TCP 半开连接）
  - 控制：每 30 秒发送心跳注释行（`: heartbeat\n\n`），写入失败即触发清理
- **风险 3**：Manager 信号补充影响已有逻辑
  - 控制：仅新增信号声明和 emit，不修改已有逻辑流程；信号是单向通知，不影响调用方

---

## 8. 里程碑完成定义（DoD）

- `EventBus` 实现并连接各 Manager 信号
- `EventStreamHandler` 实现
- SSE 端点可推送事件
- Manager 信号在关键节点正确触发
- 过滤机制可用
- 对应单元测试完成并通过
- 本里程碑文档入库
