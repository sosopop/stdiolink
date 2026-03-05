# 里程碑 96：driver_3dvision WebSocket 接口修复（TDD）

> **前置条件**: M94（DriverCore 异步事件循环改造）已完成
> **目标**: 以 TDD 方式修复 driver_3dvision 的 WebSocket 命令路径中全部已知缺陷，使 `ws.connect`/`ws.subscribe`/`ws.unsubscribe`/`ws.disconnect` 在 DriverLab 中可正常测试

---

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| `src/drivers/driver_3dvision` | 修复 WebSocket 命令实现（悬空指针、done 时机、事件包装、地址切换、订阅恢复、格式校验） |
| `src/tests` | 新增 `test_3dvision_websocket.cpp` 覆盖全部修复路径 |
| `src/smoke_tests` | 新增 `m96_3dvision_ws.py` 冒烟脚本 |

- 消除 `m_wsResponder` 悬空指针，异步信号回调使用临时 `StdioResponder` 而非保存 `IResponder*`
- `ws.connect` 在 WebSocket 握手成功后才返回 `done`，超时/失败返回 `error`
- 连接 `WebSocketClient::connected` 信号，输出连接状态日志
- 修正事件 payload 映射，消除 `data.data` 双层包装
- `connectToServer()` 支持 URL 变更检测，地址切换时自动重连
- 重连成功后自动恢复已有订阅；仅 `ws.disconnect` 清空订阅集合
- 空事件名防御：丢弃并输出 warning
- 全部修复通过 TDD 验证，现有测试套件无回归

---

## 2. 背景与问题

M94 修复了 `DriverCore` 的事件循环阻塞问题后，WebSocket 信号可以正常 dispatch。但 `driver_3dvision` 的 WebSocket 命令路径仍有 7 个已确认缺陷（详见 `doc/driver_3dvision_websocket_bug_analysis.md` 修订版），核心问题是：

1. **悬空指针（P0）**：`m_wsResponder` 保存了栈对象 `StdioResponder` 的地址，handle 返回后指针悬空，异步信号触发时访问已销毁对象（M94 前不触发，M94 后必然触发）
2. **done 时机错误（P0）**：`ws.connect` 在异步连接发起后立即返回 done，握手尚未完成
3. **connected 信号未消费（P1）**：构造函数未连接 `WebSocketClient::connected`
4. **事件双层包装（P1）**：`eventObj` 整体作为 `data` 传递，`StdioResponder::event()` 再次封装为 `{event, data}`
5. **地址切换不重连（P2）**：已连接时直接返回 true，不检查 URL 是否变化
6. **订阅恢复缺失（P2）**：重连后不恢复订阅，`disconnect()` 清空订阅集合
7. **事件格式校验缺失（P2）**：空事件名直接 emit 无防御

**范围**:
- `src/drivers/driver_3dvision/main.cpp` — 修复 `Vision3DHandler` 的 WebSocket 相关逻辑
- `src/drivers/driver_3dvision/websocket_client.h` — 新增 `m_currentUrl` 成员，修改接口
- `src/drivers/driver_3dvision/websocket_client.cpp` — 修复 `connectToServer()`、`onConnected()`、`disconnect()`、`onTextMessageReceived()`
- `src/tests/test_3dvision_websocket.cpp` — 新增测试文件
- `src/tests/CMakeLists.txt` — 注册新测试
- `src/smoke_tests/m96_3dvision_ws.py` — 新增冒烟脚本
- `src/smoke_tests/run_smoke.py` — 注册新 plan
- `src/smoke_tests/CMakeLists.txt` — CTest 接入

**非目标**:
- 不修改 `DriverCore` 或 `StdioResponder` 核心实现
- 不修改 JSONL 协议格式
- 不实现 token 认证（需与 3DVision 服务端联调确认后独立实施）
- 不新增 WebSocket 命令（如 `ws.status`）
- 不改变 HTTP API 命令路径的行为
- 不修改 `WebSocketClient` 的心跳机制

---

## 3. 技术要点

### 3.1 悬空指针消除策略

**Before**（当前）：

```cpp
// main.cpp — 成员变量
IResponder* m_wsResponder = nullptr;

// handleWsConnect()
m_wsResponder = &resp;  // resp 是栈对象，handle 返回后销毁

// 构造函数信号回调
if (m_wsResponder) {
    m_wsResponder->event(eventName, 0, data);  // UB：访问已销毁对象
}
```

**After**：

```cpp
// 删除 m_wsResponder 成员变量

// 构造函数信号回调：直接构造临时 StdioResponder
QObject::connect(&m_wsClient, &WebSocketClient::eventReceived,
    [](const QString& eventName, const QJsonObject& eventObj) {
        // 提取业务 payload，避免双层包装（同时修复 Bug 5）
        const QJsonValue payload = eventObj.contains("data")
            ? eventObj.value("data")
            : QJsonValue(eventObj);
        StdioResponder().event(eventName, 0, payload);
    });
```

**关键依据**：`StdioResponder::writeResponse()` 内部使用 `static QFile output` 打开 stdout，与实例无关，构造临时对象等价于使用进程级 stdout。

### 3.2 ws.connect 同步等待握手（QEventLoop）

**Before**：
```
connectToServer() → 立即 resp.done()
```

**After**：
```
connectToServer() → QEventLoop 等待 connected/error/timeout → resp.done() 或 resp.error()
```

```cpp
void Vision3DHandler::handleWsConnect(const QJsonObject& params, IResponder& resp)
{
    QString addr = params["addr"].toString(DEFAULT_ADDR);
    QString wsUrl = QString("ws://%1/ws").arg(addr);

    QEventLoop loop;
    bool success = false;
    QString errorMsg;
    QTimer timer;
    timer.setSingleShot(true);

    auto connOk = QObject::connect(&m_wsClient, &WebSocketClient::connected,
        [&]() { success = true; loop.quit(); });
    auto connErr = QObject::connect(&m_wsClient, &WebSocketClient::error,
        [&](const QString& err) { errorMsg = err; loop.quit(); });
    QObject::connect(&timer, &QTimer::timeout,
        [&]() { errorMsg = "WebSocket connect timeout (5s)"; loop.quit(); });

    m_wsClient.connectToServer(wsUrl);
    timer.start(5000);
    loop.exec();

    QObject::disconnect(connOk);
    QObject::disconnect(connErr);

    if (success) {
        resp.done(0, QJsonObject{{"connected", true}, {"url", wsUrl}});
    } else {
        resp.error(1, QJsonObject{{"message", errorMsg}});
    }
}
```

**超时选择**：固定 5000ms。WebSocket 握手通常 <1s，5s 足够覆盖网络抖动场景。

### 3.3 事件 payload 映射修正

**Before**（双层包装路径）：
```
WebSocketClient emit eventReceived(eventName, eventObj)
  ↓ eventObj = {"event":"scanner.ready","data":{...}}
Vision3DHandler → m_wsResponder->event(eventName, 0, eventObj)
  ↓
StdioResponder::event(eventName, code, data)
  → 输出 {"status":"event","event":"scanner.ready","data":{"event":"scanner.ready","data":{...}}}
  ❌ data.data 双层嵌套
```

**After**（正确路径）：
```
WebSocketClient emit eventReceived(eventName, eventObj)
  ↓
Vision3DHandler → StdioResponder().event(eventName, 0, eventObj["data"])
  ↓
输出 {"status":"event","event":"scanner.ready","data":{...}}
  ✅ 扁平结构
```

### 3.4 WebSocketClient 地址切换与订阅恢复

新增 `m_currentUrl` 成员，`connectToServer()` 变更检测：

```cpp
// websocket_client.h — 新增成员
QString m_currentUrl;

// websocket_client.cpp
bool WebSocketClient::connectToServer(const QString& url)
{
    if (m_connected && m_currentUrl == url) {
        return true;  // 相同地址已连接，无需操作
    }

    // 地址切换：关闭旧连接（不清空订阅）
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_heartbeatTimer->stop();
        m_socket->close();
        m_connected = false;
    }

    m_currentUrl = url;
    m_socket->open(QUrl(url));
    return true;
}

// onConnected() 增加订阅恢复
void WebSocketClient::onConnected()
{
    m_connected = true;
    m_heartbeatTimer->start(10000);
    emit connected();

    // 恢复已有订阅
    for (const QString& topic : m_subscriptions) {
        QJsonObject msg;
        msg["type"] = "sub";
        msg["topic"] = topic;
        send(msg);
    }
}

// disconnect() 显式清空（行为不变，仅确认语义）
void WebSocketClient::disconnect()
{
    m_heartbeatTimer->stop();
    m_subscriptions.clear();  // 仅显式 disconnect 清空
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }
    m_connected = false;
    m_currentUrl.clear();
}
```

### 3.5 事件格式校验

```cpp
// websocket_client.cpp — onTextMessageReceived()
if (type == "pub") {
    QString msgContent = json["message"].toString();
    QJsonDocument eventDoc = QJsonDocument::fromJson(msgContent.toUtf8());
    if (eventDoc.isObject()) {
        QJsonObject eventObj = eventDoc.object();
        QString eventName = eventObj["event"].toString();
        if (eventName.isEmpty()) {
            qWarning() << "WebSocketClient: received event with empty name, discarding:"
                        << eventObj;
            return;
        }
        emit eventReceived(eventName, eventObj);
    }
}
```

### 3.6 错误处理策略表

| 场景 | 行为 | 错误码 |
|------|------|--------|
| `ws.connect` 握手成功 | `done(0, {"connected":true, "url":"..."})` | 0 |
| `ws.connect` 握手失败（服务端拒绝） | `error(1, {"message":"..."})` | 1 |
| `ws.connect` 超时（5s） | `error(1, {"message":"WebSocket connect timeout (5s)"})` | 1 |
| `ws.subscribe` 未连接 | `error(1, {"message":"WebSocket not connected"})` | 1 |
| `ws.unsubscribe` 未连接 | `error(1, {"message":"WebSocket not connected"})` | 1 |
| `ws.disconnect` 未连接 | `done(0, {"disconnected":true})`（幂等） | 0 |
| 空事件名 | 丢弃，qWarning 输出 | — |
| 异步事件到达 | `StdioResponder().event(eventName, 0, payload)` | — |
| 异步断开事件 | `StdioResponder().event("ws.disconnected", 0, {})` | — |
| 异步错误事件 | `StdioResponder().event("ws.error", 1, {"message":"..."})` | — |

### 3.7 向后兼容

- **不变**:
  - `ws.connect`/`ws.subscribe`/`ws.unsubscribe`/`ws.disconnect` 命令名称不变
  - Meta 描述中的事件列表不变
  - HTTP API 命令（login、vessel.*、etc.）行为不变
  - 构建配置和链接依赖不变

- **变化（目标行为）**:
  - `ws.connect` 的 `done` 从"发起连接即成功"改为"握手完成才成功"（语义修正，非破坏性变更）
  - 事件输出从 `data.data` 嵌套改为 `data` 扁平（结构修正，Host/WebUI 需确认无依赖旧格式的硬编码）
  - 地址切换时会自动断开旧连接（新能力，不影响单地址使用场景）

---

## 4. 实现步骤（TDD Red-Green-Refactor）

### 4.1 Red — 编写失败测试

#### 4.1.1 测试基础设施

由于 `driver_3dvision` 依赖真实的 3DVision 服务端（外部硬件系统），单元测试不能依赖外部服务。

`Vision3DHandler` 定义在 `main.cpp` 内部且同文件含 `main()`，无法直接编译进测试工程做白盒单测。因此采用**双层测试策略**：

- **白盒层（WebSocketClient 单测）**：直接引入 `websocket_client.cpp` 编译，用本地 `QWebSocketServer` 作 mock，验证 `connectToServer` 地址切换/订阅恢复/事件格式校验等 `WebSocketClient` 自身逻辑。
- **黑盒层（子进程集成测试）**：通过 `QProcess` 启动 `driver_3dvision` 可执行文件，经 stdin/stdout JSONL 协议驱动 `ws.connect`/`ws.subscribe`/`ws.disconnect` 命令，验证 handler 层的悬空指针消除、done 时机修正、事件 payload 扁平化、`QEventLoop` 超时等 **handler 真路径**。此层依赖本地 mock WebSocket server 辅助进程（或在测试内启动 `QWebSocketServer` 后再启 driver 进程连接）。

**构建依赖补充**：测试使用 `QSignalSpy`（白盒层），需在 `src/tests/CMakeLists.txt` 中新增 `find_package(Qt6 COMPONENTS Test REQUIRED)` 和 `target_link_libraries(stdiolink_tests PRIVATE ... Qt6::Test)`。

#### 4.1.2 失败测试用例

**白盒层（WebSocketClient 单测）**：

| 测试 ID | 缺陷 | 输入 | 当前错误行为 | 期望正确行为 |
|---------|------|------|-------------|-------------|
| T02 | Bug A: done 时机 | `connectToServer` 到 mock server | — | connected 信号触发 |
| T03 | Bug A: 连接失败 | `connectToServer` 到不可达端口 | — | error 信号触发 |
| T05 | Bug 5: 事件结构 | mock server 推送 pub 消息 | — | eventReceived 信号携带正确结构 |
| T06 | Bug 6: 地址切换 | 先连 addr1 再连 addr2 | 仍连 addr1 | 断开 addr1 连上 addr2 |
| T07 | Bug E: 订阅恢复 | 连接→订阅→地址切换重连 | 订阅丢失 | 自动恢复订阅 |
| T08 | Bug E: disconnect 清空 | 连接→订阅→disconnect | — | 订阅集合为空 |
| T09 | Bug 7: 空事件名 | mock server 推送空事件名 | 空名事件上抛 | 被丢弃，不上抛 |
| T10 | 回归: disconnect 幂等 | 未连接时调 disconnect | — | 不崩溃 |

**黑盒层（子进程 handler 集成测试）**：

| 测试 ID | 缺陷 | 输入 | 当前错误行为 | 期望正确行为 |
|---------|------|------|-------------|-------------|
| H01 | Bug 1+5: 悬空指针+事件扁平 | stdin 发 `ws.connect` 到本地 mock server，mock 推送事件 | 悬空指针 UB 或 `data.data` 嵌套 | stdout 事件行的 `data` 为扁平结构，进程不崩溃 |
| H02 | Bug A: done 时机 | stdin 发 `ws.connect` 到本地 mock server | 立即返回 `done` | 握手完成后才返回 `done`（`connected:true`） |
| H03a | Bug A: handler 超时 | stdin 发 `ws.connect` 到不可达地址（TCP SYN 无应答） | 立即返回 `done` | 超时后返回 `error`（含 timeout 消息） |
| H03b | Bug A: 连接被拒 | stdin 发 `ws.connect` 到本地不监听端口 | 立即返回 `done` | 立即返回 `error` |
| H04 | 回归: ws.disconnect 幂等 | stdin 发 `ws.disconnect`（未连接状态） | — | 返回 `done({disconnected:true})`，不崩溃 |
| H05 | 重入防护 | stdin 快速连续发两条 `ws.connect`（首条停留在 QEventLoop） | 重入导致状态混乱 | 第二条被拦截返回 error("already in progress") |

```cpp
// src/tests/test_3dvision_websocket.cpp

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QTimer>

// 直接引用被测源码（因 WebSocketClient 未做库导出，需编译源文件）
#include "drivers/driver_3dvision/websocket_client.h"

class WebSocketClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 启动本地 mock WebSocket server
        m_server = new QWebSocketServer(
            "MockServer", QWebSocketServer::NonSecureMode);
        ASSERT_TRUE(m_server->listen(QHostAddress::LocalHost, 0));
        m_port = m_server->serverPort();
        m_url = QString("ws://127.0.0.1:%1").arg(m_port);
    }

    void TearDown() override {
        m_server->close();
        delete m_server;
    }

    QWebSocketServer* m_server = nullptr;
    quint16 m_port = 0;
    QString m_url;
};

// T02 — ws.connect 同步等待握手成功
// 缺陷：当前 connectToServer() 后立即返回，不等待 connected 信号
TEST_F(WebSocketClientTest, T02_ConnectWaitsForHandshake) {
    // 接受连接
    QObject::connect(m_server, &QWebSocketServer::newConnection, [this]() {
        auto* sock = m_server->nextPendingConnection();
        Q_UNUSED(sock)  // 仅接受，不关闭
    });

    WebSocketClient client;
    EXPECT_FALSE(client.isConnected());

    client.connectToServer(m_url);

    // 等待连接建立
    QSignalSpy connSpy(&client, &WebSocketClient::connected);
    EXPECT_TRUE(connSpy.wait(3000));
    EXPECT_TRUE(client.isConnected());
}

// T03 — ws.connect 超时返回 error（连接到不监听的端口）
TEST_F(WebSocketClientTest, T03_ConnectTimeout) {
    m_server->close();  // 关闭 server 使连接必然失败

    WebSocketClient client;
    client.connectToServer(m_url);

    QSignalSpy errSpy(&client, &WebSocketClient::error);
    EXPECT_TRUE(errSpy.wait(5000));
    EXPECT_FALSE(client.isConnected());
}

// T05 — 事件 payload 不应双层包装
// 缺陷：eventReceived 信号携带完整 eventObj，上层需提取 data 字段
TEST_F(WebSocketClientTest, T05_EventPayloadFlat) {
    QObject::connect(m_server, &QWebSocketServer::newConnection, [this]() {
        auto* sock = m_server->nextPendingConnection();
        // 模拟服务端推送 pub 消息
        QTimer::singleShot(100, [sock]() {
            QJsonObject inner;
            inner["event"] = "scanner.ready";
            inner["data"] = QJsonObject{{"vesselId", 42}};
            QJsonObject pub;
            pub["type"] = "pub";
            pub["message"] = QString::fromUtf8(
                QJsonDocument(inner).toJson(QJsonDocument::Compact));
            sock->sendTextMessage(
                QJsonDocument(pub).toJson(QJsonDocument::Compact));
        });
    });

    WebSocketClient client;
    QSignalSpy evSpy(&client, &WebSocketClient::eventReceived);
    client.connectToServer(m_url);

    // 等待事件
    ASSERT_TRUE(evSpy.wait(3000));
    ASSERT_EQ(evSpy.count(), 1);

    QString evName = evSpy.at(0).at(0).toString();
    QJsonObject evObj = evSpy.at(0).at(1).toJsonObject();

    EXPECT_EQ(evName, "scanner.ready");
    // eventObj 应包含 data 字段，上层应提取它
    EXPECT_TRUE(evObj.contains("data"));
    EXPECT_EQ(evObj["data"].toObject()["vesselId"].toInt(), 42);
}

// T06 — 地址切换重连
// 缺陷：m_connected==true 时直接返回，不检查 URL
TEST_F(WebSocketClientTest, T06_AddressSwitchReconnects) {
    QObject::connect(m_server, &QWebSocketServer::newConnection, [this]() {
        m_server->nextPendingConnection();
    });

    WebSocketClient client;
    client.connectToServer(m_url);

    QSignalSpy connSpy(&client, &WebSocketClient::connected);
    ASSERT_TRUE(connSpy.wait(3000));
    EXPECT_TRUE(client.isConnected());

    // 启动第二个 server
    QWebSocketServer server2("MockServer2", QWebSocketServer::NonSecureMode);
    ASSERT_TRUE(server2.listen(QHostAddress::LocalHost, 0));
    QObject::connect(&server2, &QWebSocketServer::newConnection, [&server2]() {
        server2.nextPendingConnection();
    });
    QString url2 = QString("ws://127.0.0.1:%1").arg(server2.serverPort());

    // 切换地址 — 当前实现会直接返回不重连（Bug）
    QSignalSpy connSpy2(&client, &WebSocketClient::connected);
    client.connectToServer(url2);

    // 修复后应先断开再重连
    EXPECT_TRUE(connSpy2.wait(3000))
        << "Should reconnect to new address";
    EXPECT_TRUE(client.isConnected());

    server2.close();
}

// T07 — 重连后自动恢复订阅
TEST_F(WebSocketClientTest, T07_SubscriptionRestoredAfterReconnect) {
    QList<QString> receivedSubs;
    QWebSocket* serverSocket = nullptr;

    QObject::connect(m_server, &QWebSocketServer::newConnection, [&]() {
        serverSocket = m_server->nextPendingConnection();
        QObject::connect(serverSocket, &QWebSocket::textMessageReceived,
            [&](const QString& msg) {
                auto doc = QJsonDocument::fromJson(msg.toUtf8());
                if (doc.object()["type"].toString() == "sub") {
                    receivedSubs.append(doc.object()["topic"].toString());
                }
            });
    });

    WebSocketClient client;
    client.connectToServer(m_url);

    QSignalSpy connSpy(&client, &WebSocketClient::connected);
    ASSERT_TRUE(connSpy.wait(3000));

    // 订阅
    client.subscribe("vessel.notify");
    QCoreApplication::processEvents();
    ASSERT_EQ(client.subscriptions().size(), 1);

    // 记录当前订阅数，清空记录
    receivedSubs.clear();

    // 强制断开再重连（模拟地址切换）
    QSignalSpy connSpy2(&client, &WebSocketClient::connected);
    // 启动新 server
    QWebSocketServer server2("MockServer2", QWebSocketServer::NonSecureMode);
    ASSERT_TRUE(server2.listen(QHostAddress::LocalHost, 0));
    QObject::connect(&server2, &QWebSocketServer::newConnection, [&]() {
        serverSocket = server2.nextPendingConnection();
        QObject::connect(serverSocket, &QWebSocket::textMessageReceived,
            [&](const QString& msg) {
                auto doc = QJsonDocument::fromJson(msg.toUtf8());
                if (doc.object()["type"].toString() == "sub") {
                    receivedSubs.append(doc.object()["topic"].toString());
                }
            });
    });

    QString url2 = QString("ws://127.0.0.1:%1").arg(server2.serverPort());
    client.connectToServer(url2);
    ASSERT_TRUE(connSpy2.wait(3000));

    // 等待订阅恢复消息
    QCoreApplication::processEvents(QEventLoop::AllEvents, 500);

    EXPECT_TRUE(receivedSubs.contains("vessel.notify"))
        << "Subscription should be restored after reconnect";

    server2.close();
}

// T08 — disconnect 清空订阅
TEST_F(WebSocketClientTest, T08_DisconnectClearsSubscriptions) {
    QObject::connect(m_server, &QWebSocketServer::newConnection, [this]() {
        m_server->nextPendingConnection();
    });

    WebSocketClient client;
    client.connectToServer(m_url);
    QSignalSpy connSpy(&client, &WebSocketClient::connected);
    ASSERT_TRUE(connSpy.wait(3000));

    client.subscribe("vessel.notify");
    EXPECT_EQ(client.subscriptions().size(), 1);

    client.disconnect();
    EXPECT_EQ(client.subscriptions().size(), 0);
    EXPECT_FALSE(client.isConnected());
}

// T09 — 空事件名被丢弃
TEST_F(WebSocketClientTest, T09_EmptyEventNameDiscarded) {
    QObject::connect(m_server, &QWebSocketServer::newConnection, [this]() {
        auto* sock = m_server->nextPendingConnection();
        QTimer::singleShot(100, [sock]() {
            // 发送空事件名
            QJsonObject inner;
            inner["event"] = "";
            inner["data"] = QJsonObject{{"x", 1}};
            QJsonObject pub;
            pub["type"] = "pub";
            pub["message"] = QString::fromUtf8(
                QJsonDocument(inner).toJson(QJsonDocument::Compact));
            sock->sendTextMessage(
                QJsonDocument(pub).toJson(QJsonDocument::Compact));

            // 随后发送正常事件
            QTimer::singleShot(100, [sock]() {
                QJsonObject inner2;
                inner2["event"] = "scanner.ready";
                inner2["data"] = QJsonObject{{"ok", true}};
                QJsonObject pub2;
                pub2["type"] = "pub";
                pub2["message"] = QString::fromUtf8(
                    QJsonDocument(inner2).toJson(QJsonDocument::Compact));
                sock->sendTextMessage(
                    QJsonDocument(pub2).toJson(QJsonDocument::Compact));
            });
        });
    });

    WebSocketClient client;
    QSignalSpy evSpy(&client, &WebSocketClient::eventReceived);
    client.connectToServer(m_url);

    // 等待正常事件到达（空名事件应被丢弃）
    ASSERT_TRUE(evSpy.wait(3000));

    // 应只收到 scanner.ready，空名事件被丢弃
    for (int i = 0; i < evSpy.count(); ++i) {
        EXPECT_FALSE(evSpy.at(i).at(0).toString().isEmpty())
            << "Empty event name should have been discarded";
    }
}

// T10 — ws.disconnect 幂等（未连接时调用不崩溃）
TEST_F(WebSocketClientTest, T10_DisconnectIdempotent) {
    WebSocketClient client;
    EXPECT_FALSE(client.isConnected());

    // 应不崩溃
    client.disconnect();
    EXPECT_FALSE(client.isConnected());

    // 多次调用也不崩溃
    client.disconnect();
    EXPECT_FALSE(client.isConnected());
}
```

**Red 阶段确认**：在修复前运行测试，预期以下失败：
- `T06` 失败：`connectToServer` 在已连接时直接返回，不触发第二次 connected
- `T07` 失败：`onConnected()` 不恢复订阅
- `T09` 失败：空事件名直接 emit

`T02`、`T03`、`T05`、`T08`、`T10` 可能因现有实现部分正确而通过，作为回归保护。

运行命令：
```powershell
.\build.bat
.\build\bin\stdiolink_tests.exe --gtest_filter="WebSocketClientTest.*:Vision3DHandlerBlackBoxTest.*"
```

---

### 4.2 Green — 最小修复实现

#### 修复 1：`src/drivers/driver_3dvision/main.cpp` — 消除悬空指针 + 修正事件映射

- 删除成员变量 `IResponder* m_wsResponder = nullptr`
- 修改构造函数中三个信号连接，改用临时 `StdioResponder`：

```cpp
#include "stdiolink/driver/stdio_responder.h"  // 新增引用

Vision3DHandler::Vision3DHandler()
{
    buildMeta();

    // eventReceived：提取业务 data，使用临时 StdioResponder
    QObject::connect(&m_wsClient, &WebSocketClient::eventReceived,
        [](const QString& eventName, const QJsonObject& eventObj) {
            if (eventName.isEmpty()) return;  // 防御（虽然 client 层已过滤）
            const QJsonValue payload = eventObj.contains("data")
                ? eventObj.value("data")
                : QJsonValue(eventObj);
            StdioResponder().event(eventName, 0, payload);
        });

    QObject::connect(&m_wsClient, &WebSocketClient::disconnected,
        []() {
            StdioResponder().event("ws.disconnected", 0, QJsonObject{});
        });

    QObject::connect(&m_wsClient, &WebSocketClient::error,
        [](const QString& msg) {
            StdioResponder().event("ws.error", 1, QJsonObject{{"message", msg}});
        });

    // Bug 3 修复：连接 connected 信号
    QObject::connect(&m_wsClient, &WebSocketClient::connected,
        []() {
            qDebug() << "WebSocket connected";
        });
}
```

**改动理由**：消除悬空指针 UB，同时修正事件 payload 双层包装，新增 connected 信号消费。

**验收方式**：H01（黑盒验证事件扁平+无崩溃）、编译验证无 `m_wsResponder` 引用。

---

#### 修复 2：`src/drivers/driver_3dvision/main.cpp` — ws.connect 同步等待 + 重入防护

替换 `handleWsConnect()` 为 §3.2 的 `QEventLoop` 实现。

同时删除 `handleWsDisconnect()` 中的 `m_wsResponder = nullptr` 行。

**重入防护**：在 `QEventLoop::exec()` 期间，主线程事件循环仍会处理 queued invoke 投递的新命令（来自 `DriverCore::handleStdioLineOnMainThread`）。若此时再收到 `ws.connect`/`ws.subscribe` 等命令，可能重入 handler 导致状态混乱。

控制策略：在 `Vision3DHandler` 中新增 `m_wsConnecting` 布尔标志，`handleWsConnect` 入口检查：
```cpp
if (m_wsConnecting) {
    resp.error(1, QJsonObject{{"message", "ws.connect already in progress"}});
    return;
}
m_wsConnecting = true;
// ... QEventLoop 等待 ...
m_wsConnecting = false;
```

**改动理由**：ws.connect 的 done 语义必须对应握手完成；嵌套 QEventLoop 期间需防止重入。

**验收方式**：H02（成功握手后 done）、H03a（超时 error）、H03b（连接被拒 error）、H05（重入防护）通过。

---

#### 修复 3：`src/drivers/driver_3dvision/websocket_client.h` — 新增 `m_currentUrl`

```cpp
private:
    // ... 现有成员 ...
    QString m_currentUrl;  // 新增：记录当前连接 URL
```

**改动理由**：支持地址切换检测。

---

#### 修复 4：`src/drivers/driver_3dvision/websocket_client.cpp` — 地址切换 + 订阅恢复 + 事件校验

- `connectToServer()`：改为 §3.4 版本（URL 变更检测 + 重连）
- `onConnected()`：增加订阅恢复循环
- `disconnect()`：增加 `m_currentUrl.clear()`
- `onTextMessageReceived()`：增加空事件名检查

具体实现见 §3.4 和 §3.5。

**改动理由**：修复 Bug 6/E/7。

**验收方式**：T06（地址切换）、T07（订阅恢复）、T08（disconnect 清空）、T09（空事件名过滤）通过。

---

**Green 阶段确认**：完成全部修复后运行：

```powershell
.\build.bat
.\build\bin\stdiolink_tests.exe
```

确认白盒层 `T01`–`T10` 与黑盒层 `H01`–`H05` 全部通过（Green），现有测试套件无回归。

---

### 4.3 Refactor — 重构

1. **确认 `m_wsResponder` 完全移除**：全文搜索确认 `m_wsResponder` 无残留引用。
2. **`handleWsDisconnect()` 简化**：删除 `m_wsResponder = nullptr` 行后检查函数完整性。
3. **`#include` 清理**：确认 `stdio_responder.h` 引用已正确添加。

若以上变更未引入重复代码或结构破坏，写明「无需额外重构」。

重构后全量测试必须仍全部通过。

---

## 5. 文件变更清单

### 5.1 新增文件
- `src/tests/test_3dvision_websocket.cpp` — WebSocket 命令路径修复测试
- `src/smoke_tests/m96_3dvision_ws.py` — 冒烟脚本

### 5.2 修改文件

| 文件 | 改动内容 |
|------|----------|
| `src/drivers/driver_3dvision/main.cpp` | 删除 `m_wsResponder` 成员；修改构造函数信号连接使用临时 `StdioResponder`；新增 connected 信号连接；重写 `handleWsConnect()` 为 QEventLoop 等待；删除 `handleWsDisconnect()` 中 `m_wsResponder` 操作 |
| `src/drivers/driver_3dvision/websocket_client.h` | 新增 `m_currentUrl` 私有成员 |
| `src/drivers/driver_3dvision/websocket_client.cpp` | 修改 `connectToServer()` 支持 URL 变更检测重连；修改 `onConnected()` 增加订阅恢复；修改 `disconnect()` 增加 `m_currentUrl.clear()`；修改 `onTextMessageReceived()` 增加空事件名过滤 |
| `src/tests/CMakeLists.txt` | `TEST_SOURCES` 新增 `test_3dvision_websocket.cpp`；`target_sources` 新增 `websocket_client.cpp` 编译；新增 `find_package(Qt6 COMPONENTS Test REQUIRED)` 和 `Qt6::Test` 链接依赖 |
| `src/smoke_tests/run_smoke.py` | 注册 `m96_3dvision_ws` plan |
| `src/smoke_tests/CMakeLists.txt` | CTest 注册 `smoke_m96_3dvision_ws` |

### 5.3 测试文件
- `src/tests/test_3dvision_websocket.cpp` — 15 条用例（白盒 T02-T11 共 9 条 + 黑盒 H01/H02/H03a/H03b/H04/H05 共 6 条）

---

## 6. 测试与验收

### 6.1 单元测试

- **测试对象**: 白盒层测 `WebSocketClient` 公共接口；黑盒层测 `Vision3DHandler` handler 真路径（经 stdin/stdout JSONL 协议）
- **用例分层**: 正常路径（T02/T05/T08/T10/H01/H02/H04）、边界值（T09）、异常输入（T03/H03a/H03b）、兼容回归（T06/T07）、重入防护（H05）
- **桩替身策略**: 白盒层用本地 `QWebSocketServer` mock；黑盒层启动 `driver_3dvision` 子进程 + 本地 mock WS server
- **测试文件**: `src/tests/test_3dvision_websocket.cpp`
- **构建依赖**: 需新增 `Qt6::Test`（`QSignalSpy` 依赖）

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `handleWsConnect`: 握手成功 | `done(0, {connected:true})` | H02 |
| `handleWsConnect`: 握手超时 | `error(1, {message:timeout})` | H03a |
| `handleWsConnect`: 连接被拒（connection refused） | `error(1, {message:...})` | H03b |
| `handleWsConnect`: 重入防护 | `error(1, "ws.connect already in progress")` | H05 |
| 事件回调: 事件到达 | 临时 `StdioResponder` 输出扁平 `data` | H01 |
| 事件回调: eventName 非空 | 正常输出事件 | T05/H01 |
| 事件回调: eventName 为空 | 丢弃，不输出 | T09 |
| `connectToServer`: 已连接且 URL 相同 | 直接返回 true | T02 |
| `connectToServer`: 已连接但 URL 不同 | 断开旧连接，发起新连接 | T06 |
| `connectToServer`: 未连接 | 发起新连接 | T02 |
| `onConnected`: 有已存订阅 | 恢复订阅 | T07 |
| `onConnected`: 无已存订阅 | 仅发 connected 信号 | T02 |
| `disconnect`: 已连接 | 清空订阅、关闭 socket | T08 |
| `disconnect`: 未连接 | 幂等，不崩溃 | T10/H04 |

#### 用例详情

**T01 — 悬空指针消除验证**
- 前置条件: `m_wsResponder` 成员已删除
- 输入: 编译检查
- 预期: 编译通过（无 `m_wsResponder` 引用）
- 断言: 编译无错误（由 H01 覆盖运行时验证）

**T02 — ws.connect 等待握手成功**
- 前置条件: 本地 mock QWebSocketServer 运行中
- 输入: `client.connectToServer(m_url)` 后等待 connected 信号
- 预期: connected 信号触发，`isConnected()` 返回 true
- 断言: `connSpy.wait(3000) == true`，`client.isConnected() == true`

**T03 — ws.connect 超时/失败**
- 前置条件: mock server 已关闭（端口不可达）
- 输入: `client.connectToServer(m_url)`
- 预期: error 信号触发，`isConnected()` 为 false
- 断言: `errSpy.wait(5000) == true`，`client.isConnected() == false`

**T04 — connected 信号被消费**
- 前置条件: mock server 运行中
- 输入: `client.connectToServer(m_url)`
- 预期: connected 信号触发
- 断言: 由 T02 直接覆盖（`connSpy.wait` 确认 connected 信号存在且可 spy）

**T05 — 事件 payload 扁平结构**
- 前置条件: mock server 运行中，模拟推送 `{"type":"pub","message":"{\"event\":\"scanner.ready\",\"data\":{\"vesselId\":42}}"}`
- 输入: 等待 eventReceived 信号
- 预期: eventName = "scanner.ready"，eventObj 含 "data" 字段
- 断言: `evName == "scanner.ready"`，`evObj["data"]["vesselId"] == 42`

**T06 — 地址切换重连**
- 前置条件: 两个 mock server 运行中
- 输入: 先连 server1，再连 server2
- 预期: 第二次 connected 信号触发
- 断言: `connSpy2.wait(3000) == true`，`client.isConnected() == true`

**T07 — 订阅重连后恢复**
- 前置条件: 两个 mock server 运行中
- 输入: 连接 server1→订阅 "vessel.notify"→切换到 server2
- 预期: server2 收到 sub 消息
- 断言: `receivedSubs.contains("vessel.notify") == true`

**T08 — disconnect 清空订阅**
- 前置条件: mock server 运行中，已连接
- 输入: subscribe→disconnect
- 预期: subscriptions 为空
- 断言: `client.subscriptions().size() == 0`，`client.isConnected() == false`

**T09 — 空事件名被丢弃**
- 前置条件: mock server 推送空名事件，随后推送正常事件
- 输入: 等待 eventReceived 信号
- 预期: 只收到正常事件
- 断言: 所有接收到的 eventName 均非空

**T10 — disconnect 幂等**
- 前置条件: 未连接的 WebSocketClient
- 输入: 连续调用两次 disconnect
- 预期: 不崩溃
- 断言: `client.isConnected() == false`

**H01 — 悬空指针消除 + 事件扁平化（黑盒）**
- 前置条件: 本地 mock WebSocket server 运行中；`driver_3dvision` 可执行文件存在
- 输入: 启动 driver_3dvision 子进程（`--profile=keepalive`），stdin 发 `ws.connect`（指向 mock server），mock server 推送一条事件后发 `ws.disconnect`
- 预期: stdout 收到事件行，`data` 字段为扁平结构（非 `data.data`）；进程不崩溃
- 断言: 事件行 JSON 中 `data` 不含嵌套 `event` 字段；进程退出码 0

**H02 — ws.connect 握手成功后 done（黑盒）**
- 前置条件: 本地 mock WebSocket server 运行中
- 输入: stdin 发 `ws.connect`（指向 mock server）
- 预期: stdout 收到 `done` 行，含 `"connected":true`
- 断言: 响应 JSON 中 `status == "done"` 且 `data.connected == true`

**H03a — ws.connect handler 超时（黑盒）**
- 前置条件: 使用一个可路由但不监听的远端地址（如 `192.0.2.1:9999`，RFC 5737 TEST-NET，TCP SYN 无应答导致真超时）
- 输入: stdin 发 `ws.connect`（指向该地址）
- 预期: stdout 在 5s 超时后收到 `error` 行，含 `timeout` 消息
- 断言: 响应 JSON 中 `status == "error"` 且 `data.message` 含 "timeout"；响应耗时 ≥4s
- 环境兜底: 若网络环境导致立即失败（而非超时），用例标记 skip 并注明原因

**H03b — ws.connect 连接被拒（黑盒）**
- 前置条件: 使用本地不监听的端口（connection refused 立即返回）
- 输入: stdin 发 `ws.connect`（指向 `127.0.0.1:<unused_port>`）
- 预期: stdout 立即收到 `error` 行（非 done），含错误描述
- 断言: 响应 JSON 中 `status == "error"`

**H04 — ws.disconnect 幂等（黑盒）**
- 前置条件: 未连接状态
- 输入: stdin 发 `ws.disconnect`
- 预期: stdout 收到 `done` 行，含 `"disconnected":true`
- 断言: 响应 JSON 中 `status == "done"`；进程不崩溃

**H05 — ws.connect 重入防护（黑盒）**
- 前置条件: 本地启动一个故意延迟握手的 mock WS server（接受 TCP 但不完成 WebSocket 升级，使 handleWsConnect 停留在 QEventLoop 中）
- 输入: stdin 连续快速写入两条 `ws.connect` 命令（拼为一次 write 确保 reader 线程连续投递）
- 预期: 第一条进入 QEventLoop 等待；第二条被 `m_wsConnecting` 拦截返回 error
- 断言: stdout 中存在一行 `status == "error"` 且 `data.message` 含 "already in progress"

#### 测试代码

白盒层代码见 §4.1.2 中 `WebSocketClientTest` 完整测试代码。

黑盒层代码采用与 M94 的 `DriverCoreAsyncTest` 相同模式：`QProcess` 启动 `driver_3dvision`，经 stdin 写 JSONL 命令，读 stdout 解析 JSONL 响应。核心辅助方法复用 `writeCommand()` / `readResponse()` 模式（见 M94 §4.1.2）。

### 6.2 冒烟测试脚本

- 脚本目录: `src/smoke_tests/`
- 脚本文件: `m96_3dvision_ws.py`
- 统一入口: `python src/smoke_tests/run_smoke.py --plan m96_3dvision_ws`
- CTest 接入: `smoke_m96_3dvision_ws`
- 覆盖范围: driver_3dvision 进程启动 + ws.connect 完整链路
- 用例清单:
  - `S01`: 启动 driver_3dvision（`--cmd=meta.describe`），验证退出码 0 且输出含 `ws.connect` 命令描述 → 断言 meta 导出正常
  - `S02`: 启动 driver_3dvision（`--help`），验证退出码 0 → 断言基础可用
  - `S03`: 脚本内启动本地 mock `QWebSocketServer`（Python `websockets` 库），再启动 driver_3dvision 进程，经 stdin 发送 `ws.connect`（指向 mock server），验证 stdout 输出包含 `"connected":true`；随后发 `ws.subscribe`，mock server 推送一条事件，验证 stdout 收到 `"status":"event"` 且 `data` 为扁平结构（非 `data.data`）；最后发 `ws.disconnect`，验证 `"disconnected":true` → 断言 WebSocket 修复本体链路通畅
- 失败输出规范: 失败时输出 stderr + 退出码非零
- 环境约束与跳过策略: `driver_3dvision` 可执行文件不存在时 skip；S03 若 `websockets` Python 包未安装则 skip（`pip install websockets`）

### 6.3 集成/端到端测试

- 全量运行 `stdiolink_tests` 验证无回归
- 手动验证（条件允许时）：在 DriverLab 中测试 `ws.connect` → `ws.subscribe` → 接收事件 → `ws.disconnect` 完整流程

### 6.4 验收标准

- [ ] `m_wsResponder` 成员变量已删除，全文无引用 — T01 (编译验证)
- [ ] `ws.connect` 在握手完成后才返回 `done` — H02
- [ ] `ws.connect` 超时返回 `error` — H03a
- [ ] `ws.connect` 连接被拒返回 `error` — H03b
- [ ] `connected` 信号已连接 — 代码审查确认 `QObject::connect` 调用存在 + H01 间接覆盖（connected 后 handler 才收到后续事件）
- [ ] 事件输出无 `data.data` 双层嵌套 — T05/H01
- [ ] 地址切换触发重连 — T06
- [ ] 重连后订阅自动恢复 — T07
- [ ] `disconnect` 清空订阅 — T08
- [ ] 空事件名被丢弃 — T09
- [ ] `disconnect` 幂等 — T10/H04
- [ ] ws.connect 重入防护 — H05
- [ ] `stdiolink_tests` 全量通过（无回归）

---

## 7. 风险与控制

- 风险: `QEventLoop` 嵌套导致命令重入
  - 触发条件: `handleWsConnect()` 内 `QEventLoop::exec()` 期间，主线程事件循环仍会处理 `DriverCore::handleStdioLineOnMainThread` 投递的 queued invoke，若此时 stdin 有新命令（如第二条 `ws.connect` 或 `ws.subscribe`），会重入 handler 导致状态混乱
  - 控制: 新增 `m_wsConnecting` 标志位，`handleWsConnect` 入口检查并拒绝重入（返回 `error(1, "ws.connect already in progress")`）
  - 控制: 超时兜底（5s）防止 loop 永不退出
  - 测试覆盖: H02 (正常退出)、H03a (超时退出)、H03b (连接被拒)、H05 (重入拦截)

- 风险: 订阅恢复时 WebSocket 尚未完全就绪，`send()` 静默失败
  - 控制: `onConnected()` 在 `m_connected = true` 之后恢复订阅，`send()` 内部检查 `ConnectedState`
  - 测试覆盖: T07

- 风险: 测试中使用本地 mock server，无法覆盖真实 3DVision 服务端协议差异
  - 控制: 冒烟测试 S01/S02 验证 driver 可启动和 meta 导出正确
  - 控制: 后续联调阶段手动验证完整链路（非本 milestone 范围）

---

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] 冒烟测试脚本已新增并接入统一入口（`run_smoke.py`）与 CTest
- [ ] 冒烟测试在目标环境执行通过（或有明确 skip 记录）
- [ ] 文档同步完成
- [ ] 向后兼容/迁移策略确认
- [ ] 【问题修复类】Red 阶段失败测试有执行记录
- [ ] 【问题修复类】Green 阶段全量测试通过（新增 + 既有无回归）
- [ ] 【问题修复类】Refactor 阶段（若有）全量测试仍通过


