# 里程碑 55：DriverLab WebSocket 测试会话

> **前置条件**: 里程碑 50 已完成（Driver 详情 API 已就绪），里程碑 49 已完成（CORS 已就绪）
> **目标**: 实现 WebSocket 端点 `WS /api/driverlab/{driverId}`，用连接生命周期绑定 Driver 进程生命周期，支持 OneShot 和 KeepAlive 两种模式

---

## 1. 目标

- 实现 `DriverLabWsHandler` — 注册 WebSocket 升级验证器，管理全局连接
- 实现 `DriverLabWsConnection` — 单个 WebSocket 连接的 Driver 进程管理与消息转发
- 核心原则：**WebSocket 连接 = DriverLab 测试会话**，连接断开即结束会话并清理进程
- 支持 `oneshot` 和 `keepalive` 两种运行模式
- 定义完整的上下行消息协议
- 全局连接数限制（默认 10）

---

## 2. 背景与问题

桌面端 DriverLab 直接拉起 Driver 子进程进行测试。Web 版需要服务端代理整个 Driver 进程交互。与 REST Session 方案（需 idle timeout、session 表、轮询）相比，WebSocket 方案利用连接状态作为"用户是否在场"的天然信号，代码更简洁，资源泄漏风险更低。

Qt 6.8+ 的 `QAbstractHttpServer::addWebSocketUpgradeVerifier()` 原生支持 WebSocket 升级，HTTP 和 WebSocket 共享同一端口，无需独立 `QWebSocketServer`。

---

## 3. 技术要点

### 3.1 连接生命周期

```
客户端 WebSocket 握手
  → 服务端 verifyUpgrade（校验 driverId、连接数）
  → 握手成功，创建 DriverLabWsConnection
  → 拉起 Driver 子进程（QProcess）
  → queryMeta → 推送 meta 消息给客户端
  → 双向通信（exec 命令 → stdin，stdout → stdout 消息）
  → WebSocket 断开 → terminate + kill Driver 进程
  → 析构 DriverLabWsConnection
```

### 3.2 生命周期绑定规则

| 事件 | KeepAlive 模式 | OneShot 模式 |
|------|---------------|-------------|
| WebSocket 断开 | kill Driver | kill Driver |
| Driver 正常退出 | 推送 `driver.exited`，关闭 WebSocket | 推送 `driver.exited`，**不关闭 WebSocket**（保留会话） |
| Driver 崩溃 | 推送 `driver.exited`，关闭 WebSocket | 推送 `driver.exited`，**不关闭 WebSocket**（等待下一条命令） |
| 新命令（Driver 已退出） | 不支持（连接已关闭） | 自动重启 Driver + 推送 `driver.restarted` |
| 服务端 shutdown | kill 所有 Driver，关闭所有 WebSocket | 同左 |

### 3.3 下行消息协议（服务端 → 客户端）

所有下行消息为 JSON，含 `type` 字段：

| type | 说明 | 字段 |
|------|------|------|
| `meta` | 连接建立后首条消息 | `driverId`/`pid`/`runMode`/`meta` |
| `stdout` | Driver stdout 转发 | `message`（原样 JSONL 行） |
| `driver.started` | Driver 进程启动 | `pid` |
| `driver.exited` | Driver 进程退出 | `exitCode`/`exitStatus`/`reason` |
| `driver.restarted` | OneShot 自动重启 | `pid`/`reason` |
| `error` | 错误通知 | `message` |

### 3.4 上行消息协议（客户端 → 服务端）

| type | 说明 | 字段 |
|------|------|------|
| `exec` | 执行命令 | `cmd`/`data` |
| `cancel` | 终止当前命令（可选） | — |

`exec` 消息转发逻辑：将 `{"cmd":"read_register","data":{"address":100}}` 写入 Driver stdin。

`cancel` 语义：关闭 Driver 进程的 stdin（`QProcess::closeWriteChannel()`），使 Driver 感知到输入结束并自行退出。不发送 SIGINT/SIGTERM，因为 Driver 协议层没有"取消"语义，关闭 stdin 是最安全的中断方式。KeepAlive 模式下 cancel 会导致 Driver 退出并关闭 WebSocket；OneShot 模式下 Driver 退出后等待下一条 exec 自动重启。

### 3.5 连接参数

```
ws://127.0.0.1:8080/api/driverlab/driver_modbustcp?runMode=keepalive&args=--verbose
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `runMode` | string | ❌ | `oneshot`（默认）或 `keepalive` |
| `args` | string | ❌ | 额外启动参数，逗号分隔 |

### 3.6 错误处理

| 场景 | 行为 |
|------|------|
| driverId 不存在 | 拒绝握手（verifier 返回 deny + 404） |
| 连接数已满 | 拒绝握手（verifier 返回 deny + 429） |
| Driver 启动失败 | 握手成功后推送 `error`，关闭 WebSocket |
| Meta 查询失败 | 推送 `error`（不中断连接） |
| 客户端发送非法 JSON | 推送 `error`（不中断连接） |
| 客户端发送未知 type | 推送 `error`（不中断连接） |

---

## 4. 实现方案

### 4.1 DriverLabWsHandler

```cpp
// src/stdiolink_server/http/driverlab_ws_handler.h
#pragma once

#include <QHttpServer>
#include <QObject>
#include <QVector>

namespace stdiolink {
class DriverCatalog;
}

namespace stdiolink_server {

class DriverLabWsConnection;

class DriverLabWsHandler : public QObject {
    Q_OBJECT
public:
    explicit DriverLabWsHandler(stdiolink::DriverCatalog* catalog,
                                QObject* parent = nullptr);
    ~DriverLabWsHandler();

    void registerVerifier(QHttpServer& server);
    int activeConnectionCount() const;
    static constexpr int kMaxConnections = 10;

private slots:
    void onNewWebSocketConnection();
    void onConnectionClosed(DriverLabWsConnection* conn);

private:
    QHttpServerWebSocketUpgradeResponse
    verifyUpgrade(const QHttpServerRequest& request);

    stdiolink::DriverCatalog* m_catalog;
    QHttpServer* m_server = nullptr;
    QVector<DriverLabWsConnection*> m_connections;

    // verifier 回调中暂存参数，onNewWebSocketConnection 中取用
    // 注意：依赖 verifier 回调和 newWebSocketConnection 信号的 1:1 顺序对应。
    // Qt 文档未明确保证此顺序，实现时需验证。如不可靠，
    // 可改用 QMap<QWebSocket*, PendingInfo> 在 onNewWebSocketConnection 中
    // 通过 socket 的 requestUrl() 重新解析参数（更健壮但有少量重复解析）。
    struct PendingInfo {
        QString driverId;
        QString runMode;
        QStringList extraArgs;
    };
    QQueue<PendingInfo> m_pendingQueue;
};

} // namespace stdiolink_server
```

### 4.2 DriverLabWsConnection

```cpp
// src/stdiolink_server/http/driverlab_ws_connection.h
#pragma once

#include <QObject>
#include <QProcess>
#include <QWebSocket>
#include <memory>

namespace stdiolink_server {

class DriverLabWsConnection : public QObject {
    Q_OBJECT
public:
    DriverLabWsConnection(std::unique_ptr<QWebSocket> socket,
                          const QString& driverId,
                          const QString& program,
                          const QString& runMode,
                          const QStringList& extraArgs,
                          QObject* parent = nullptr);
    ~DriverLabWsConnection();

    QString driverId() const { return m_driverId; }

signals:
    void closed(DriverLabWsConnection* conn);

private slots:
    void onTextMessageReceived(const QString& message);
    void onSocketDisconnected();
    void onDriverStdoutReady();
    void onDriverFinished(int exitCode, QProcess::ExitStatus status);

private:
    void startDriver();
    void stopDriver();
    void sendJson(const QJsonObject& msg);
    void forwardStdoutLine(const QByteArray& line);
    void handleExecMessage(const QJsonObject& msg);
    void handleCancelMessage();
    void restartDriverForOneShot();

    std::unique_ptr<QWebSocket> m_socket;
    std::unique_ptr<QProcess> m_process;
    QString m_driverId;
    QString m_program;
    QString m_runMode;  // "oneshot" | "keepalive"
    QStringList m_extraArgs;
    QByteArray m_stdoutBuffer;  // readyRead 可能不是完整行
    bool m_metaSent = false;
};

} // namespace stdiolink_server
```

### 4.3 注册流程

在 `ServerManager::initialize()` 或 `main.cpp` 中：

```cpp
m_driverLabWsHandler = new DriverLabWsHandler(driverCatalog(), this);
m_driverLabWsHandler->registerVerifier(server);
```

### 4.4 verifyUpgrade 实现

```cpp
QHttpServerWebSocketUpgradeResponse
DriverLabWsHandler::verifyUpgrade(const QHttpServerRequest& request) {
    const QString path = request.url().path();
    if (!path.startsWith("/api/driverlab/"))
        return QHttpServerWebSocketUpgradeResponse::passToNext();

    const QString driverId = path.mid(QString("/api/driverlab/").size());
    if (driverId.isEmpty() || !m_catalog->hasDriver(driverId))
        return QHttpServerWebSocketUpgradeResponse::deny(404, "driver not found");

    if (m_connections.size() >= kMaxConnections)
        return QHttpServerWebSocketUpgradeResponse::deny(429, "too many connections");

    // 解析查询参数
    QUrlQuery query(request.url());
    PendingInfo info;
    info.driverId = driverId;
    info.runMode = query.queryItemValue("runMode");
    if (info.runMode.isEmpty()) info.runMode = "oneshot";
    if (info.runMode != "oneshot" && info.runMode != "keepalive")
        return QHttpServerWebSocketUpgradeResponse::deny(400, "invalid runMode");

    QString argsStr = query.queryItemValue("args");
    if (!argsStr.isEmpty())
        info.extraArgs = argsStr.split(",", Qt::SkipEmptyParts);

    m_pendingQueue.enqueue(info);
    return QHttpServerWebSocketUpgradeResponse::accept();
}
```

### 4.5 stdout 转发

Driver stdout 采用 JSONL（每行一个 JSON）。`readyRead` 可能返回不完整的行，需要行缓冲：

```cpp
void DriverLabWsConnection::onDriverStdoutReady() {
    m_stdoutBuffer.append(m_process->readAllStandardOutput());

    while (true) {
        int nlIndex = m_stdoutBuffer.indexOf('\n');
        if (nlIndex < 0) break;

        QByteArray line = m_stdoutBuffer.left(nlIndex).trimmed();
        m_stdoutBuffer.remove(0, nlIndex + 1);

        if (!line.isEmpty())
            forwardStdoutLine(line);
    }
}

void DriverLabWsConnection::forwardStdoutLine(const QByteArray& line) {
    auto doc = QJsonDocument::fromJson(line);
    QJsonObject msg;
    msg["type"] = "stdout";
    msg["message"] = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(QString(line));
    sendJson(msg);
}
```

### 4.6 Meta 查询

连接建立后自动查询 Meta：向 Driver stdin 写入 `{"cmd":"meta.describe","data":{}}\n`，等待 stdout 返回 ok 消息。首条 stdout ok 消息作为 meta 推送给客户端。

实现方式：使用 `m_metaSent` 标志位 + `QTimer::singleShot()` 实现异步超时。`onDriverStdoutReady` 中检查 `!m_metaSent` 时将首条 ok 响应作为 meta 推送，并设置 `m_metaSent = true`。超时回调中检查 `!m_metaSent` 则推送 error 消息。不使用阻塞等待，不阻塞 Qt 事件循环。

如果 Meta 查询超时（5 秒），推送 error 消息但不关闭连接（Driver 可能不支持 meta.describe 但仍可正常工作）。

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/stdiolink_server/http/driverlab_ws_handler.h`
- `src/stdiolink_server/http/driverlab_ws_handler.cpp`
- `src/stdiolink_server/http/driverlab_ws_connection.h`
- `src/stdiolink_server/http/driverlab_ws_connection.cpp`

### 5.2 修改文件

- `src/stdiolink_server/server_manager.h` — 新增 `DriverLabWsHandler*` 成员和 getter
- `src/stdiolink_server/server_manager.cpp` — 初始化和 shutdown 时管理 WebSocket handler
- `src/stdiolink_server/CMakeLists.txt` — 新增源文件，链接 `Qt6::WebSockets`
- `src/stdiolink_server/http/api_router.cpp` — 在 `registerRoutes` 中调用 `registerVerifier`（或在 main.cpp 中单独注册）

### 5.3 测试文件

- 新增 `src/tests/test_driverlab_ws_handler.cpp`

---

## 6. 测试与验收

### 6.1 单元测试场景

**DriverLabWsHandler**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | verifyUpgrade：合法 driverId | 返回 accept |
| 2 | verifyUpgrade：不存在 driverId | 返回 deny + 404 |
| 3 | verifyUpgrade：非 `/api/driverlab/` 路径 | 返回 passToNext |
| 4 | verifyUpgrade：连接数已满 | 返回 deny + 429 |
| 5 | verifyUpgrade：无效 runMode | 返回 deny + 400 |
| 6 | verifyUpgrade：默认 runMode 为 oneshot | PendingInfo.runMode == "oneshot" |
| 7 | activeConnectionCount 正确 | 新建连接后递增，断开后递减 |

**DriverLabWsConnection**：

| # | 场景 | 验证点 |
|---|------|--------|
| 8 | 连接建立后拉起 Driver 进程 | m_process 状态为 Running |
| 9 | Meta 推送作为首条消息 | type == "meta" + 含 driverId/pid/runMode |
| 10 | exec 消息转发到 stdin | Driver stdin 收到 JSON 命令 |
| 11 | Driver stdout 转发 | 客户端收到 type == "stdout" |
| 12 | stdout 行缓冲正确 | 不完整行不会提前发送 |
| 13 | WebSocket 断开 → Driver killed | m_process 已终止 |
| 14 | Driver 退出 → 推送 driver.exited | 客户端收到 exitCode + exitStatus |
| 15 | KeepAlive: Driver 退出 → 关闭 WebSocket | WebSocket 状态为 closed |
| 16 | OneShot: Driver 退出 → **不关闭** WebSocket | WebSocket 仍为 open |
| 17 | OneShot: 退出后发 exec → 自动重启 | 推送 driver.restarted + 新 pid |
| 18 | Driver 启动失败 → 推送 error | type == "error" + 关闭 WebSocket |
| 19 | 客户端发送非法 JSON | 推送 error（不关闭连接） |
| 20 | 客户端发送未知 type | 推送 error（不关闭连接） |
| 21 | 析构时清理 Driver 进程 | ~DriverLabWsConnection 后进程不遗留 |
| 22 | cancel 消息处理 | Driver stdin 关闭或发送中断信号 |

**注意**：WebSocket 测试需要使用 `QWebSocket` 客户端连接到 QHttpServer。测试需启动完整的 HTTP server + 注册 verifier。如果 QHttpServer 的 WebSocket 在单元测试中不易搭建，可使用 mock 验证 Handler/Connection 的逻辑，集成测试验证完整流程。

### 6.2 验收标准

- WebSocket 握手成功后 Driver 进程自动拉起
- Meta 作为首条消息推送
- exec 命令正确转发到 Driver stdin
- stdout 实时转发到客户端
- 连接断开后 Driver 进程被终止
- OneShot 模式下 Driver 退出后自动重启
- 连接数限制生效
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：`QHttpServer::addWebSocketUpgradeVerifier` API 行为与预期不符
  - 控制：Qt 6.10.0 文档已确认此 API；编写最小 demo 先行验证 verifier + `nextPendingWebSocketConnection` 流程
- **风险 2**：verifier 回调中无法传递 driverId 到 `onNewWebSocketConnection`
  - 控制：使用 `m_pendingQueue` 暂存 verifier 中解析的参数；`newWebSocketConnection` 信号触发时从队列取出
- **风险 3**：Driver 进程退出信号和 WebSocket 断开信号的竞态
  - 控制：在 `onSocketDisconnected` 中检查进程是否已退出再决定是否 terminate；在 `onDriverFinished` 中检查 socket 是否已断开再决定是否发送消息。OneShot 模式下自动重启时，需加 `m_restarting` 标志位防止 `onDriverFinished` 和 `handleExecMessage` 中的重启逻辑重入
- **风险 4**：Meta 查询超时阻塞事件循环
  - 控制：使用异步等待（QTimer + 信号槽），不阻塞 Qt 事件循环

---

## 8. 里程碑完成定义（DoD）

- `DriverLabWsHandler` 和 `DriverLabWsConnection` 实现
- WebSocket 端点注册并可接受连接
- 上下行消息协议完整实现
- OneShot 和 KeepAlive 两种模式行为正确
- 连接数限制生效
- 对应单元测试完成并通过
- 本里程碑文档入库
