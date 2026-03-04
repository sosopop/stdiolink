# driver_3dvision WebSocket 接口 Bug 分析报告

**日期**: 2026-03-04
**驱动**: `driver_3dvision`
**问题**: WebSocket 命令接口无法在 WebUI DriverLab 中正常测试

---

## 1. 问题概述

driver_3dvision 实现了 WebSocket 命令接口（`ws.connect`, `ws.subscribe`, `ws.unsubscribe`, `ws.disconnect`），但在 WebUI 的 DriverLab 中无法正常工作。通过深入分析代码和通信链路，发现了多个架构级和实现级的关键问题。

---

## 2. 代码分析

### 2.1 当前实现

**文件**: `src/drivers/driver_3dvision/main.cpp`

**类定义**（第25-102行）：
```cpp
class Vision3DHandler : public IMetaCommandHandler {
public:
    Vision3DHandler();
    // ...
private:
    HttpClient m_client;
    WebSocketClient m_wsClient;
    DriverMeta m_meta;
    QString m_token;
    IResponder* m_wsResponder = nullptr;  // 保存 responder 指针
};
```

**构造函数**（第117-142行）：
```cpp
Vision3DHandler::Vision3DHandler()
{
    buildMeta();

    // 已连接的信号
    QObject::connect(&m_wsClient, &WebSocketClient::eventReceived, ...);
    QObject::connect(&m_wsClient, &WebSocketClient::disconnected, ...);
    QObject::connect(&m_wsClient, &WebSocketClient::error, ...);
    // 缺少 connected 信号
}
```

**ws.connect 实现**（第1009-1024行）：
```cpp
void Vision3DHandler::handleWsConnect(const QJsonObject& params, IResponder& resp)
{
    QString addr = params["addr"].toString(DEFAULT_ADDR);
    QString wsUrl = "ws://" + addr + "/ws";

    m_wsResponder = &resp;  // 保存栈对象引用

    if (m_wsClient.connectToServer(wsUrl)) {
        resp.done(0, QJsonObject{{"connected", true}});  // 立即返回
    } else {
        resp.error(1, QJsonObject{{"message", "Failed to connect"}});
    }
}
```

---

### 2.2 实验验证（Windows 管道场景）

为验证 `stdin` 驱动方式的可行性，增加了临时探针程序：
- `tmp/qsocketnotifier_probe/main.cpp`
- `tmp/qsocketnotifier_probe/CMakeLists.txt`

测试拓扑：父进程 `QProcess` 向子进程 `stdin` 管道写入一行 `PING_FROM_PARENT\n`。

**实验结果（2026-03-04）**：

| 模式 | 子进程参数 | 结果 | 结论 |
|---|---|---|---|
| `QSocketNotifier(fileno(stdin), Read)` | `--child`（父进程用 `--parent`） | 3 秒超时未触发（`TIMEOUT`，exit=2） | 在当前 Windows + 管道 stdin 场景不可靠 |
| `QWinEventNotifier(GetStdHandle(STD_INPUT_HANDLE))` | `--child-winevent`（父进程用 `--parent-winevent`） | 成功触发并读到 `PING_FROM_PARENT`（exit=0） | Windows 可行，但平台特化 |
| 阻塞 `QTextStream::readLine()`（对照） | `--child-blocking`（父进程用 `--parent-blocking`） | 成功读到 `PING_FROM_PARENT`（exit=0） | 管道链路本身正常 |

**实验结论**：
- 为满足“跨平台一次到位 + 多平台处理一致”，不采用平台分叉的 `QWinEventNotifier` 路径。
- 最终采用“读线程阻塞读 stdin + 主线程队列投递处理”的统一方案。

---

## 3. 发现的 Bug

### Bug 1: 悬空指针风险（架构级问题）🔴 P0

**问题描述**:
- `handleWsConnect()` 保存了 `IResponder&` 的地址到 `m_wsResponder`（main.cpp 第1014行）
- 但 `IResponder` 是 `processOneLine()` 中的栈对象 `StdioResponder responder`（driver_core.cpp 第138行）
- `handle()` 返回后，栈对象销毁，`m_wsResponder` 成为悬空指针
- 后续异步信号（connected, eventReceived 等）仍会使用该指针（main.cpp 第125、132、139行）

**代码证据**:
```cpp
// driver_core.cpp 第138行
StdioResponder responder;  // 栈对象

// driver_core.cpp 第164/169行
m_handler->handle(req.cmd, req.data, responder);  // 传递引用
// handle() 返回后，responder 销毁

// main.cpp 第1014行
m_wsResponder = &resp;  // 保存悬空指针

// main.cpp 第125行（异步信号处理）
if (m_wsResponder) {
    m_wsResponder->event(...);  // ❌ 使用悬空指针 → 崩溃
}
```

**影响**:
- 未定义行为（UB）
- 可能导致段错误（Segmentation Fault）
- 可能导致数据损坏

**根本原因**:
KeepAlive 命令的 responder 生命周期管理问题。

---

### Bug 2: 事件循环阻塞（架构级问题）🔴 P0

**问题描述**:
- WebSocket 是异步的，依赖 Qt 事件循环来 dispatch 信号
- `connectToServer()` 调用 `m_socket->open()`，异步发起连接（websocket_client.cpp 第32行）
- 但 `DriverCore::runStdioMode()` 使用 `readLine()` 阻塞等待 stdin 输入（driver_core.cpp 第34-35行）
- **阻塞的本质**：主线程在 `readLine()` 等待下一条命令，Qt 事件循环不运行
- WebSocket 的 `connected`/`error`/`eventReceived` 信号在队列中等待 dispatch，但主线程被锁住
- 只有当下一条 stdin 命令到达时，`readLine()` 才返回，事件循环才有机会运行

**代码证据**:
```cpp
// driver_core.cpp 第34-46行
while (!in.atEnd()) {
    QString line = in.readLine();  // ❌ 阻塞等待 stdin，事件循环不运行
    if (line.isEmpty()) continue;

    if (!processOneLine(line.toUtf8())) {
        // 处理失败
    }

    if (m_profile == Profile::OneShot) {
        break;
    }
    // KeepAlive 模式：继续循环，再次阻塞在 readLine()
}
```

**影响**:
- **KeepAlive 模式**：执行 ws.connect 后，主线程立即回到 readLine() 阻塞，WebSocket 连接永远无法建立
- **OneShot 模式**：处理完 ws.connect 后进程直接退出，连接还没建立进程就没了
- 整个 WebSocket 功能完全不可用

**根本原因**:
DriverCore 的主循环设计假设所有命令都是同步的，不支持需要事件循环的异步操作。

---

### Bug 3: 连接建立确认机制不完整 🟡 P1

**问题描述**:
- 构造函数连接了 `eventReceived`、`disconnected`、`error` 信号（main.cpp 第122-141行）
- 但缺少 `connected` 信号的连接
- `ws.connect` 当前在调用 `open()` 后立即返回 `done`，并未等待真实握手结果

**代码证据**:
```cpp
// main.cpp 第117-142行
Vision3DHandler::Vision3DHandler()
{
    buildMeta();

    QObject::connect(&m_wsClient, &WebSocketClient::eventReceived, ...);
    QObject::connect(&m_wsClient, &WebSocketClient::disconnected, ...);
    QObject::connect(&m_wsClient, &WebSocketClient::error, ...);
    // ❌ 缺少 connected 信号
}
```

**影响**:
- `done({"connected": true})` 可能成为“假阳性”成功
- Host/WebUI 无法区分“已发起连接”与“已完成连接”

---

### Bug 4: Token 认证需求未确认 ⚠️ 待验证

**问题描述**:
- 代码当前直接连接 `ws://addr/ws`，未携带 token
- 现有材料未形成“WebSocket 必须 token 认证”的确定证据
- token 传递方式也未最终确认（query 参数、header、或无需 token）

**代码证据**:
```cpp
// main.cpp 第1009-1024行
void Vision3DHandler::handleWsConnect(const QJsonObject& params, IResponder& resp)
{
    QString addr = params["addr"].toString(DEFAULT_ADDR);
    QString wsUrl = "ws://" + addr + "/ws";  // ❌ 未携带 token
    // ...
}
```

**影响**:
- 连接失败原因不透明，容易误判为“网络问题”或“事件循环问题”
- 若后续实测确认需要 token，当前实现将出现认证失败

---

### Bug 5: 事件 payload 结构错误（双层包装） 🟡 P1

**问题描述**:
- `WebSocketClient::eventReceived` 已将完整事件对象 `eventObj` 透传给上层
- `main.cpp` 直接把该完整 `eventObj` 作为 `IResponder::event(eventName, ..., data)` 的 `data` 参数
- `IResponder::event(eventName,...)` 会再次封装 `{"event": eventName, "data": data}`，形成双层包装

**代码证据**:
```cpp
// websocket_client.cpp
emit eventReceived(eventName, eventObj); // eventObj 形如 {"event":"x","data":{...}}

// main.cpp
m_wsResponder->event(eventName, 0, data); // data = eventObj（完整对象）

// iresponder.h
payload["event"] = eventName;
payload["data"] = data; // 再次包装，最终变成 data.data...
```

**影响**:
- Host/WebUI 读取事件数据路径变深（`data.data`），与常规 typed-event 结构不一致
- 事件消费端需要特判，增加兼容成本和出错概率

---

### Bug 6: 连接地址切换问题 🟢 P2

**问题描述**:
- `WebSocketClient::connectToServer()` 在已连接时直接返回 true（websocket_client.cpp 第28行）
- 如果用户切换连接地址（从 192.168.1.100 → 192.168.1.101），不会断开旧连接
- 导致连接到错误的服务器

**代码证据**:
```cpp
// websocket_client.cpp 第27-33行
bool WebSocketClient::connectToServer(const QString& url)
{
    if (m_connected) {
        return true;  // ❌ 已连接时直接返回，不检查 URL 是否变化
    }
    m_socket->open(QUrl(url));
    return true;
}
```

**影响**:
- 无法切换连接目标
- 用户需要手动调用 `ws.disconnect` 再重连

---

### Bug 7: 事件格式校验不足 🟢 P2

**问题描述**:
- 当前上行逻辑未校验 `eventName` 是否为空
- `eventObj` 中缺失 `data` 时，未定义 fallback 行为

**影响**:
- 事件可观测性下降，问题定位困难
- 与下游约定不一致时容易出现“事件已到达但无有效载荷”的歧义

---

## 4. 当前代码状态

### 4.1 已实现的功能
- ✅ WebSocket 客户端封装（websocket_client.cpp）
- ✅ 信号连接（eventReceived, disconnected, error）
- ✅ 命令元数据定义（ws.connect, ws.subscribe, ws.unsubscribe, ws.disconnect）

### 4.2 缺失的功能
- ❌ `connected` 信号处理
- ❌ `ws.connect` 认证策略（是否必须 token）未实测确认
- ❌ 事件循环支持（KeepAlive 模式）
- ❌ Responder 生命周期管理
- ❌ 事件 payload 结构对齐（避免双层包装）

---

## 5. 修复方案

### 5.1 Bug 1 修复：使用临时 Responder（P0）

**关键发现**：`StdioResponder::writeResponse()` 使用 `static QFile output`，是进程级 stdout，与实例无关。

```cpp
// stdio_responder.cpp
void StdioResponder::writeResponse(...) {
    static QFile output;  // ← static，只初始化一次
    if (!output.isOpen()) {
        (void)output.open(stdout, QIODevice::WriteOnly);
    }
    // 写入 stdout
}
```

**方案**：信号处理中直接构造临时 `StdioResponder`，无需保存指针

```cpp
// 删除成员变量 m_wsResponder

// 构造函数中连接信号（注意 eventReceived 签名是 eventName + eventObj）
QObject::connect(&m_wsClient, &WebSocketClient::eventReceived,
    [](const QString& eventName, const QJsonObject& eventObj) {
        // 只透传业务 data，避免 event 被二次包装成 data.data
        const QJsonValue dataValue = eventObj.contains("data")
            ? eventObj.value("data")
            : QJsonValue(eventObj);
        StdioResponder().event(eventName, 0, dataValue);
    });

// handleWsConnect 需要同步等待连接结果（见 Bug A）
```

### 5.2 Bug 2 修复：事件循环集成（P0，最终方案）

> 决策对应：Q1 选 A（主线程事件循环化），并结合实验结果采用跨平台统一实现。

**最终实现**：读线程阻塞读取 `stdin`，主线程通过队列安全处理命令。

实现要点：
1. 主线程进入 `QCoreApplication::exec()`，保障 WebSocket 异步信号可调度。
2. 单独 reader 线程执行 `QTextStream::readLine()` 阻塞读，避免主线程阻塞。
3. reader 线程每读到一行，通过 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 或信号槽 `QueuedConnection` 投递到主线程。
4. 主线程串行调用 `processOneLine()`（保持当前处理语义）。
5. EOF / 关闭时由 reader 线程发送结束信号，主线程决定退出时机（`OneShot` 命令后退出；`KeepAlive` 按连接状态和输入结束策略退出）。

```cpp
// driver_core.cpp (示意)
int DriverCore::runStdioMode() {
    if (!m_handler) {
        return 1;
    }

    auto* reader = new StdinReaderWorker(); // 内部阻塞 readLine
    QThread readerThread;
    reader->moveToThread(&readerThread);

    QObject::connect(&readerThread, &QThread::started,
                     reader, &StdinReaderWorker::run);

    QObject::connect(reader, &StdinReaderWorker::lineReady, this,
        [this](const QByteArray& line) {
            if (!processOneLine(line)) {
                // 保持现有容错语义：单行失败不终止循环
            }
            if (m_profile == Profile::OneShot) {
                QCoreApplication::quit();
            }
        }, Qt::QueuedConnection);

    QObject::connect(reader, &StdinReaderWorker::eof, this,
        []() {
            QCoreApplication::quit();
        }, Qt::QueuedConnection);

    QObject::connect(&readerThread, &QThread::finished,
                     reader, &QObject::deleteLater);

    readerThread.start();
    const int rc = QCoreApplication::exec();
    readerThread.quit();
    readerThread.wait();
    return rc;
}
```

**为什么不选 `QSocketNotifier` 主方案**：
- 实验已验证在当前 Windows 管道 stdin 场景下不触发（见 §2.2）。
- `QWinEventNotifier` 虽可用，但会引入平台分叉；与“多平台一致”目标冲突。

### 5.3 Bug A 修复：ws.connect 同步等待连接结果（P0）

**问题**：当前 `handleWsConnect` 在连接建立前就调用 `resp.done()`，语义错误。

**方案**：使用 `QEventLoop` 同步等待连接结果（必须加超时）

```cpp
void Vision3DHandler::handleWsConnect(const QJsonObject& params, IResponder& resp)
{
    QString addr = params["addr"].toString(DEFAULT_ADDR);
    QString wsUrl = QString("ws://%1/ws").arg(addr);  // token 见 Bug B/4

    QEventLoop loop;
    bool success = false;
    QString errorMsg;
    QTimer timer;
    timer.setSingleShot(true);

    // 连接成功信号
    auto connConn = QObject::connect(&m_wsClient, &WebSocketClient::connected,
        [&]() {
            success = true;
            loop.quit();
        });

    // 连接失败信号
    auto errConn = QObject::connect(&m_wsClient, &WebSocketClient::error,
        [&](const QString& err) {
            errorMsg = err;
            loop.quit();
        });

    QObject::connect(&timer, &QTimer::timeout, [&]() {
        errorMsg = "WebSocket connect timeout";
        loop.quit();
    });

    m_wsClient.connectToServer(wsUrl);
    timer.start(5000);
    loop.exec();  // 同步等待

    QObject::disconnect(connConn);
    QObject::disconnect(errConn);

    if (success) {
        resp.done(0, QJsonObject{{"connected", true}});
    } else {
        resp.error(1, QJsonObject{{"message", errorMsg}});
    }
}
```
### 5.4 Bug 3 修复：连接建立确认机制（P1）

**说明**：`connected` 信号用于确认握手成功，再返回 `resp.done()`。这样 `ws.connect` 的成功语义与真实连接状态一致。

构造函数中仍需保留 `eventReceived`、`disconnected`、`error` 信号连接（用于推送异步事件）。

### 5.5 Bug B 修复：Meta 可选 token 输入（P2，可用性增强）

**问题**：若联调确认“支持或需要外部 token”，当前 `ws.connect` Meta 无法直接输入 token，排障效率较低。

**方案**：在 `buildMeta()` 中为 `ws.connect` 添加**可选** token 参数（不改变默认调用路径）

```cpp
.command(CommandBuilder("ws.connect")
    .param(addrParam())
    .param(FieldBuilder("token", FieldType::String)
        .description("认证 Token（可选，未提供时使用 login 获取的 token）")
        .placeholder("输入 token 或留空"))
    ...
```

**handleWsConnect 中优先使用参数 token**：
```cpp
QString token = params["token"].toString();
if (token.isEmpty()) {
    token = m_token;  // 回退到 login 获取的 token
}
QString wsUrl = QString("ws://%1/ws?token=%2").arg(addr).arg(token);
```

**定级说明**：
- 该项不直接修复崩溃/不可用问题；
- 主要提升联调效率与可观察性，建议按 P2 执行。

### 5.6 Bug 4 修复：Token 认证策略先验证后固化 ⚠️

**注意**：当前证据不足以断言“必须 token”。应先通过抓包/联调确认服务端要求，再固化实现。

**假设方案**（如果需要 token）：
```cpp
QString wsUrl = QString("ws://%1/ws?token=%2").arg(addr).arg(token);
```

**建议**：按以下顺序验证并留档（建议写入测试记录）：
1. 不带 token 连接；
2. query token 连接；
3. 若服务端支持，尝试 header/token 子协议方式；
4. 根据结果再决定 `ws.connect` 的参数是否 `required`。

### 5.7 Bug 5 + Bug 7 修复：事件映射与健壮性（P1/P2）

```cpp
QObject::connect(&m_wsClient, &WebSocketClient::eventReceived,
    [](const QString& eventName, const QJsonObject& eventObj) {
        if (eventName.isEmpty()) {
            qWarning() << "Invalid websocket event: empty event name" << eventObj;
            return;
        }

        // 统一输出结构：status=event, data={event:..., data:...}
        const QJsonValue payload = eventObj.contains("data")
            ? eventObj.value("data")
            : QJsonValue(eventObj);
        StdioResponder().event(eventName, 0, payload);
    });
```

### 5.8 Bug 6 + Bug E 修复：支持地址切换 + 订阅恢复（P2）

**问题**：切换地址时不断开旧连接，且重连后已有订阅未恢复。

**方案**：
```cpp
// websocket_client.cpp
bool WebSocketClient::connectToServer(const QString& url)
{
    if (m_connected && m_currentUrl == url) {
        return true;
    }

    // 地址切换重连：仅关闭 socket，不清空订阅
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_heartbeatTimer->stop();
        m_socket->close();
        m_connected = false;
    }

    m_currentUrl = url;
    m_socket->open(QUrl(url));
    return true;
}

// 在 onConnected 中恢复订阅
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

// 显式 ws.disconnect 才清空订阅
void WebSocketClient::disconnect()
{
    m_heartbeatTimer->stop();
    m_subscriptions.clear();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }
    m_connected = false;
}
```

---

## 6. 优先级汇总

| Bug | 描述 | 优先级 | 影响范围 |
|-----|------|--------|----------|
| Bug 1 | 悬空指针风险 | 🔴 P0 | 崩溃风险 |
| Bug 2 | 事件循环阻塞 | 🔴 P0 | 功能完全不可用 |
| Bug A | ws.connect 的 done() 时机错误 | 🔴 P0 | 协议语义错误 |
| Bug 3 | 连接建立确认机制不完整 | 🟡 P1 | 成功语义不可靠 |
| Bug B | Meta 可选 token 输入能力 | 🟢 P2 | 联调/排障效率 |
| Bug 4 | Token 认证需求未确认 | ⚠️ 待验证 | 需实测确认 |
| Bug 5 | 事件 payload 双层包装 | 🟡 P1 | 下游解析歧义 |
| Bug 6 | 连接地址切换问题 | 🟢 P2 | 用户体验 |
| Bug E | 重连后订阅未恢复 | 🟢 P2 | 功能完整性 |
| Bug 7 | 事件格式校验不足 | 🟢 P2 | 可观测性/容错 |

---

## 7. 总结

driver_3dvision 的 WebSocket 接口存在三个架构级 P0 问题，导致功能完全不可用：

1. **悬空指针**：KeepAlive 命令保存栈对象引用，异步信号触发时访问已销毁对象
2. **事件循环阻塞**：主线程阻塞在 `readLine()`，Qt 事件循环无法运行，WebSocket 信号永远不会 dispatch
3. **done() 时机错误**：`ws.connect` 在连接建立前就返回 done，违反协议语义

修复建议：
- 信号处理中使用临时 `StdioResponder` 对象（利用 static stdout）
- 在 `DriverCore` 中采用“读线程阻塞读 stdin + 主线程队列投递 + `app.exec()`”统一跨平台方案
- `handleWsConnect` 使用 `QEventLoop` 同步等待连接结果，并增加超时兜底
- 修正事件映射逻辑，避免 `data.data` 双层包装
- token 认证策略先做联调验证，再决定是否强制
- 重连时恢复已有订阅，并避免“重连路径误清空订阅”

完成上述修复并通过联调后，WebSocket 功能可在 DriverLab 中稳定测试。

---

**参考文件**:
- `src/drivers/driver_3dvision/main.cpp`
- `src/drivers/driver_3dvision/websocket_client.cpp`
- `src/stdiolink/driver/driver_core.cpp`
- `src/drivers/driver_modbustcp/main.cpp` (参考实现)
- `d:\code\3dvision\doc\API_DOCUMENTATION.md`

---

## 8. 已确认决策（全部选 A）

本节记录已确认的最终决策，不再保留待选分支。

### Q1 事件循环改造路径：A（已确认）

- 目标：主线程事件循环化，消除 `readLine()` 阻塞对异步 WebSocket 的影响。
- 落地：结合 §2.2 实验结果，采用“读线程阻塞读 + 主线程队列投递”的跨平台统一实现。
- 说明：不采用 `QSocketNotifier` 作为主路径；不采用平台分叉的 `QWinEventNotifier` 作为默认路径。

### Q2 `ws.connect` 返回语义：A（已确认）

- 落地：等待 `connected` / `error` / `timeout` 后返回 `done/error`。
- 要求：超时阈值固定（建议 5000ms），并返回明确错误信息。

### Q3 token 策略：A（已确认）

- 落地：优先使用参数 `token`，否则回退 `m_token`，均为空时不带 token 尝试连接。
- 要求：在日志中明确记录当前采用的认证路径（参数/缓存/无 token）。

### Q4 `ws.connect` Meta 暴露 token 字段：A（已确认）

- 落地：在 Meta 中新增可选 `token` 字段。
- 要求：字段说明明确“可留空并回退 login token”，不改变旧流程可用性。

### Q5 订阅恢复策略：A（已确认）

- 落地：地址切换重连保留并自动恢复订阅；仅显式 `ws.disconnect` 清空订阅集合。
- 要求：重连成功后按 `m_subscriptions` 顺序恢复，失败时输出可观测错误事件。
