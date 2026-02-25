# 里程碑 75：WebSocket Ping/Pong 心跳检测

> **前置条件**: M55 (DriverLab WebSocket)、M57 (SSE Event Stream)
> **目标**: 为 DriverLabWsConnection 增加 WebSocket 协议级 ping/pong 心跳，实现空闲连接检测与自动清理

## 1. 目标
- 为所有 DriverLab WebSocket 连接增加周期性 ping 心跳
- 检测并关闭无响应的僵死连接（客户端静默断开、网络中断等场景）
- 在 DriverLabWsHandler 层统一管理心跳定时器，避免每个连接各自创建定时器
- 与 SSE 心跳机制保持一致的超时策略（心跳间隔 30s，超时 60s）

## 2. 背景与问题
- 当前 `DriverLabWsConnection` 没有任何 WebSocket 层面的心跳检测
- 如果客户端静默断开（网络中断、进程崩溃但 TCP FIN 未送达），服务端无法感知，连接永远不会被清理
- 僵死连接会占用 `kMaxConnections = 10` 的连接配额，最终导致新连接被拒绝
- SSE 侧已有完善的心跳 + stale sweep 机制（`EventStreamHandler`），WebSocket 侧缺失对等能力

**范围**:
- `DriverLabWsHandler` 心跳定时器管理（启动、停止、周期触发）
- `DriverLabWsConnection` ping 发送、pong 接收与时间戳记录
- 超时连接的自动检测与关闭

**非目标**:
- 不修改 SSE 心跳逻辑（`EventStreamHandler` / `EventStreamConnection`）
- 不改变 Driver 进程管理行为（启动、重启、crash backoff）
- 不引入应用层心跳消息（使用 WebSocket 协议原生 ping/pong 帧）
- 不修改 `kMaxConnections` 连接上限值

## 3. 技术要点

### 3.1 Qt QWebSocket ping/pong API

Qt 6 的 `QWebSocket` 原生支持 WebSocket 协议级 ping/pong 帧：

```cpp
// 发送 ping 帧（slot），payload ≤ 125 字节，超出自动截断
void QWebSocket::ping(const QByteArray& payload = QByteArray());

// 收到 pong 响应时触发（signal），elapsedTime 为往返毫秒数
void QWebSocket::pong(quint64 elapsedTime, const QByteArray& payload);
```

关键行为：Qt 自动回复收到的 ping（发送 pong），客户端无需手动处理。服务端只需调用 `ping()` 并监听 `pong` 信号即可实现心跳检测。

### 3.2 心跳架构与时序

定时器归属 `DriverLabWsHandler`（单定时器管理所有连接），与 SSE 侧 `EventStreamHandler` 的 `m_heartbeatTimer` 设计对齐。

```
                DriverLabWsHandler                     DriverLabWsConnection
                ┌──────────────────┐                   ┌─────────────────────┐
                │  m_pingTimer     │                   │  m_socket (QWebSocket)
                │  (30s interval)  │                   │  m_lastPongAt       │
                └──────┬───────────┘                   └──────────┬──────────┘
                       │                                          │
  Timer fires ────────►│                                          │
                       │  1. sweepDeadConnections()                │
                       │     for each conn:                       │
                       │       if now - lastPongAt > 60s:         │
                       │         m_connections.removeOne(conn) ───┤
                       │         conn->closeForPongTimeout() ─────┼──► m_socket->close(
                       │         conn->deleteLater()              │      GoingAway,
                       │                                          │      "pong timeout")
                       │                                          │
                       │  2. for each surviving conn:             │
                       │       conn->sendPing() ──────────────────┼──► m_socket->ping()
                       │                                          │       │
                       │                                          │       ▼
                       │                                     [WebSocket ping frame]
                       │                                          │
                       │                                     [Client auto-replies pong]
                       │                                          │
                       │                                          ▼
                       │                                     pong signal received
                       │                                          │
                       │                                     onPongReceived()
                       │                                     m_lastPongAt = now
```

### 3.3 常量定义与 SSE 对比

```cpp
// DriverLabWsHandler — 新增常量
static constexpr int kPingIntervalMs = 30000;                    // 30s
static constexpr int kPongTimeoutMs  = kPingIntervalMs * 2;      // 60s
```

| 参数 | WebSocket (本次新增) | SSE (已有) |
|------|---------------------|-----------|
| 心跳间隔 | `kPingIntervalMs = 30000` | `kHeartbeatIntervalMs = 30000` |
| 超时阈值 | `kPongTimeoutMs = 60000` | `kConnectionTimeoutMs = 60000` |
| 心跳载体 | WebSocket ping/pong 帧 | SSE comment (`: heartbeat\n\n`) |
| 超时判据 | `m_lastPongAt` 距今 > 60s | `m_lastSendAt` 距今 > 60s |
| 定时器归属 | `DriverLabWsHandler::m_pingTimer` | `EventStreamHandler::m_heartbeatTimer` |

### 3.4 错误处理策略

| 错误场景 | 行为 | 关闭码 / 后续 |
|---------|------|--------------|
| pong 超时（>60s 无 pong） | `sweepDeadConnections` 从列表移除 → 调用 `closeForPongTimeout()` → `deleteLater()` | `CloseCodeGoingAway` + reason `"pong timeout"` |
| `sendPing` 时 socket 已断开 | 跳过 ping（guard: `m_closing` 或 socket 状态检查） | 无操作，等待 `disconnected` 信号自然清理 |
| `sendPing` 时 socket 处于 closing | 跳过 ping | 无操作 |
| `closeAll` 调用 | 先停止 `m_pingTimer`，再逐个 delete 连接 | 定时器不再触发，避免 sweep 与 closeAll 交叉 |

### 3.5 向后兼容

- WebSocket 客户端无需任何改动：ping/pong 是 WebSocket 协议原生帧，Qt `QWebSocket` 客户端自动回复 pong
- 浏览器原生 WebSocket API 同样自动回复 pong，WebUI DriverLab 页面无需修改
- 现有 `DriverLabWsConnection` 的所有公共行为（exec、cancel、meta query、driver lifecycle 消息）不受影响
- 新增公共方法分两类：
  - 运行时方法（`sendPing()`、`lastPongAt()`、`closeForPongTimeout()`）仅由 `DriverLabWsHandler` 内部调用，不改变 WebSocket 协议及前端客户端行为契约
  - 测试辅助方法（`setLastPongAtForTest()`、`setPingIntervalForTest()`、`connectionAt()`）仅供单元测试使用，不影响生产行为

## 4. 实现步骤

### 4.1 修改 `driverlab_ws_connection.h` — 新增心跳相关声明

在 `DriverLabWsConnection` 类中新增公共方法、私有槽和成员变量。

- 新增公共方法 `sendPing()`、`lastPongAt()`、`closeForPongTimeout()`，另加测试辅助方法 `setLastPongAtForTest()`：
  ```cpp
  public:
      void sendPing();
      void closeForPongTimeout();
      QDateTime lastPongAt() const { return m_lastPongAt; }
      void setLastPongAtForTest(const QDateTime& dt) { m_lastPongAt = dt; }
  ```
- 新增私有槽 `onPongReceived`：
  ```cpp
  private slots:
      void onPongReceived(quint64 elapsedTime, const QByteArray& payload);
  ```
- 新增私有成员 `m_lastPongAt`：
  ```cpp
  private:
      QDateTime m_lastPongAt;
  ```

理由：`sendPing` / `lastPongAt` / `closeForPongTimeout` 需要被 `DriverLabWsHandler` 调用，因此为 public。`setLastPongAtForTest` 为测试专用，允许模拟超时场景而无需等待真实 60s。`onPongReceived` 仅作为信号槽连接目标，为 private slot。

### 4.2 修改 `driverlab_ws_connection.cpp` — 实现心跳逻辑

- 构造函数中初始化 `m_lastPongAt` 并连接 pong 信号：
  ```cpp
  // 在构造函数初始化列表末尾追加
  , m_lastPongAt(QDateTime::currentDateTimeUtc())

  // 在构造函数体中，与 textMessageReceived / disconnected 连接同级
  connect(m_socket.get(), &QWebSocket::pong,
          this, &DriverLabWsConnection::onPongReceived);
  ```
  理由：初始化为当前时间，确保新建连接不会被立即判定为超时。

- 新增 `onPongReceived()` 实现：
  ```cpp
  void DriverLabWsConnection::onPongReceived(quint64 /*elapsedTime*/,
                                              const QByteArray& /*payload*/) {
      m_lastPongAt = QDateTime::currentDateTimeUtc();
  }
  ```
  理由：仅需更新时间戳，`elapsedTime` 和 `payload` 当前无需使用。

- 新增 `sendPing()` 实现：
  ```cpp
  void DriverLabWsConnection::sendPing() {
      if (m_closing || !m_socket ||
          m_socket->state() != QAbstractSocket::ConnectedState) {
          return;
      }
      m_socket->ping();
  }
  ```
  理由：guard 条件与 `sendJson()` 一致，避免对已关闭/正在关闭的 socket 发送 ping。

- 新增 `closeForPongTimeout()` 实现：
  ```cpp
  void DriverLabWsConnection::closeForPongTimeout() {
      m_closing = true;
      stopDriver();
      if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
          m_socket->close(QWebSocketProtocol::CloseCodeGoingAway,
                          QStringLiteral("pong timeout"));
      }
  }
  ```
  理由：与析构函数的关闭路径类似，但显式传递 `CloseCodeGoingAway` + `"pong timeout"` 关闭码，使客户端能区分超时断开与正常关闭。由 `sweepDeadConnections` 调用，在从 `m_connections` 移除后执行。

### 4.3 修改 `driverlab_ws_handler.h` — 新增定时器与 sweep 声明

- 新增常量：
  ```cpp
  public:
      static constexpr int kPingIntervalMs = 30000;
      static constexpr int kPongTimeoutMs  = kPingIntervalMs * 2;
  ```
- 新增测试辅助访问器：
  ```cpp
  public:
      void setPingIntervalForTest(int ms);
      DriverLabWsConnection* connectionAt(int index) const;
  ```
- 新增私有槽和方法：
  ```cpp
  private slots:
      void onPingTick();

  private:
      void sweepDeadConnections();
      QTimer m_pingTimer;
  ```

理由：常量为 public 以便测试断言。`setPingIntervalForTest` 允许测试使用极短间隔（如 50ms）触发真实定时器路径，避免等待 30s。`connectionAt` 允许测试访问连接对象以检查 `lastPongAt()` 等状态。`m_pingTimer` 为值成员（非指针），生命周期随 Handler 自动管理。

### 4.4 修改 `driverlab_ws_handler.cpp` — 实现定时器与 sweep

- 构造函数中启动定时器：
  ```cpp
  // 在构造函数体末尾追加
  m_pingTimer.setInterval(kPingIntervalMs);
  connect(&m_pingTimer, &QTimer::timeout,
          this, &DriverLabWsHandler::onPingTick);
  m_pingTimer.start();
  ```
  理由：与 `EventStreamHandler` 构造函数中 `m_heartbeatTimer` 的初始化模式一致。

- 新增 `onPingTick()` 实现：
  ```cpp
  void DriverLabWsHandler::onPingTick() {
      sweepDeadConnections();
      for (auto* conn : m_connections) {
          conn->sendPing();
      }
  }
  ```
  理由：先 sweep 再 ping，与 SSE 侧 `onHeartbeat()` 的"先 sweep 再发心跳"流程一致。

- 新增 `sweepDeadConnections()` 实现：
  ```cpp
  void DriverLabWsHandler::sweepDeadConnections() {
      const QDateTime now = QDateTime::currentDateTimeUtc();
      QVector<DriverLabWsConnection*> dead;
      for (auto* conn : m_connections) {
          const qint64 elapsed = conn->lastPongAt().msecsTo(now);
          if (elapsed > kPongTimeoutMs) {
              dead.append(conn);
          }
      }
      for (auto* conn : dead) {
          m_connections.removeOne(conn);
          conn->closeForPongTimeout();
          conn->deleteLater();
      }
  }
  ```
  理由：先从 `m_connections` 移除再关闭，避免依赖析构函数中 `disconnected` 信号的同步触发。`closeForPongTimeout()` 显式发送 `CloseCodeGoingAway` + `"pong timeout"`，使客户端能区分超时断开。`deleteLater()` 延迟销毁，确保当前事件循环中的其他槽函数不会访问已销毁对象。与 SSE 侧 `sweepStaleConnections()` 的收集后清理、延迟销毁思路对齐（SSE 侧通过 `close()` + `emit disconnected()` → `removeConnection()` / `deleteLater()` 实现，路径不完全相同但延迟销毁策略一致）。

- 修改 `closeAll()` — 停止定时器：
  ```cpp
  void DriverLabWsHandler::closeAll() {
      m_pingTimer.stop();          // ← 新增：先停定时器
      const auto connections = m_connections;
      for (auto* conn : connections) {
          delete conn;
      }
      m_connections.clear();
  }
  ```
  理由：先停定时器确保 `onPingTick` 不会在 closeAll 清理过程中触发。

- 新增 `setPingIntervalForTest()` 实现：
  ```cpp
  void DriverLabWsHandler::setPingIntervalForTest(int ms) {
      m_pingTimer.setInterval(ms);
  }
  ```
  理由：测试专用，允许将 30s 间隔缩短至 50ms 级别，使测试能在合理时间内触发真实定时器路径。

- 新增 `connectionAt()` 实现：
  ```cpp
  DriverLabWsConnection* DriverLabWsHandler::connectionAt(int index) const {
      if (index < 0 || index >= m_connections.size()) {
          return nullptr;
      }
      return m_connections.at(index);
  }
  ```
  理由：测试专用，允许测试访问连接对象以检查 `lastPongAt()` 时间戳、调用 `setLastPongAtForTest()` 模拟超时等。

## 5. 文件变更清单

### 5.1 新增文件
- 无

### 5.2 修改文件
- `src/stdiolink_server/http/driverlab_ws_connection.h` — 新增 `m_lastPongAt`、`onPongReceived` 槽、`lastPongAt()` 访问器、`sendPing()`、`closeForPongTimeout()`、`setLastPongAtForTest()` 方法
- `src/stdiolink_server/http/driverlab_ws_connection.cpp` — 构造函数连接 pong 信号、实现 `onPongReceived`、`sendPing`、`closeForPongTimeout`
- `src/stdiolink_server/http/driverlab_ws_handler.h` — 新增 `m_pingTimer`、`onPingTick` 槽、`sweepDeadConnections` 方法、常量、`setPingIntervalForTest()`、`connectionAt()` 访问器
- `src/stdiolink_server/http/driverlab_ws_handler.cpp` — 构造函数启动定时器、实现 `onPingTick`、`sweepDeadConnections`（显式移除+关闭模式）、`closeAll` 停止定时器、测试辅助方法

### 5.3 测试文件
- `src/tests/test_driverlab_ws_handler.cpp` — 新增心跳相关单元测试（T01–T07）

## 6. 测试与验收

### 6.1 单元测试（必填，重点）

- 测试对象: `DriverLabWsHandler` 心跳定时器与 sweep 逻辑、`DriverLabWsConnection` ping/pong 交互
- 用例分层: 正常路径（T01–T03）、边界值（T04–T05）、异常输入（T06）、兼容回归（T07）
- 断言要点: 常量值、`lastPongAt` 时间戳更新、`activeConnectionCount()` 变化、连接关闭码
- 桩替身策略: 使用真实 `QHttpServer` + `QWebSocket` 客户端（与现有 `WsTestFixture` 一致）；通过 `setPingIntervalForTest(50)` 将定时器缩短至 50ms 使测试能触发真实 `onPingTick` 路径；通过 `connectionAt()` + `setLastPongAtForTest()` 模拟超时场景
- 测试文件: `src/tests/test_driverlab_ws_handler.cpp`

#### 路径矩阵（必填）

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `onPingTick`: 连接列表为空 | 空列表 → sweep 无操作 → ping 循环不执行 | T05 |
| `onPingTick`: 连接列表非空 | sweep + 逐连接 ping | T02, T03 |
| `sweepDeadConnections`: `elapsed ≤ kPongTimeoutMs` | 跳过，连接存活 | T03, T04 |
| `sweepDeadConnections`: `elapsed > kPongTimeoutMs` | 从列表移除 → `closeForPongTimeout()` → `deleteLater()` | T06 |
| `sendPing`: socket 已连接且非 closing | 调用 `m_socket->ping()` | T02 |
| `sendPing`: `m_closing == true` | 跳过 | 极低概率 — 正常流程中 `m_closing` 置 true 后 `closed` 信号触发 `onConnectionClosed` 移除连接，但在客户端断开到信号处理之间的窗口期，`onPingTick` 遍历可能访问到该连接。guard 作为防御性检查保留，当前测试主路径未覆盖 |
| `sendPing`: socket 为 null 或未连接 | 跳过 | 极低概率 — 连接构造时 socket 必定已连接，断开后 `disconnected` → `closed` → 从列表移除，但信号处理前存在短暂窗口期 socket 已非 ConnectedState 而连接仍在列表中。guard 作为防御性检查保留，当前测试主路径未覆盖 |
| `onPongReceived`: 收到 pong | 更新 `m_lastPongAt` | T02 |
| `closeForPongTimeout`: 正常调用 | 设置 `m_closing`、停止 Driver、发送 `CloseCodeGoingAway` | T06 |

覆盖要求（硬性）: 所有可达路径 100% 有用例；极低概率防御性分支已说明存在理由与未覆盖原因。

#### 用例详情（必填）

**T01 — 心跳常量值正确**
- 前置条件: 无
- 输入: 直接访问 `DriverLabWsHandler::kPingIntervalMs` 和 `kPongTimeoutMs`
- 预期: `kPingIntervalMs == 30000`，`kPongTimeoutMs == 60000`，`kPongTimeoutMs == kPingIntervalMs * 2`
- 断言: `EXPECT_EQ(DriverLabWsHandler::kPingIntervalMs, 30000)`、`EXPECT_EQ(DriverLabWsHandler::kPongTimeoutMs, 60000)`

**T02 — 服务端 ping 触发客户端 auto-pong，服务端 onPongReceived 更新 lastPongAt**
- 前置条件: `WsTestFixture` 启动，`setPingIntervalForTest(50)` 缩短定时器至 50ms，WebSocket 客户端已连接
- 输入: 等待定时器触发 `onPingTick` → 服务端发送 ping → 客户端自动回复 pong → 服务端 `pong` 信号触发 `onPongReceived`
- 预期: `connectionAt(0)->lastPongAt()` 被更新为晚于连接建立时间 `t0`
- 断言: `EXPECT_GT(conn->lastPongAt(), t0)`，`EXPECT_EQ(fixture.handler->activeConnectionCount(), 1)`

**T03 — 正常连接经过 sweep 后存活**
- 前置条件: `WsTestFixture` 启动，`setPingIntervalForTest(50)` 缩短定时器，WebSocket 客户端已连接且正常回复 pong
- 输入: 等待至少 2 次定时器触发（`waitMs(200)`），期间客户端自动回复 pong 使 `lastPongAt` 持续更新
- 预期: 连接存活，sweep 不清理（`lastPongAt` 距今远小于 60s）
- 断言: `EXPECT_EQ(fixture.handler->activeConnectionCount(), 1)`

**T04 — 新建连接不会被立即 sweep**
- 前置条件: `WsTestFixture` 启动，`setPingIntervalForTest(50)` 缩短定时器
- 输入: WebSocket 客户端刚完成连接握手，等待一次定时器触发（`waitMs(100)`）
- 预期: 连接存活（`m_lastPongAt` 初始化为当前时间，距今远小于 60s，sweep 跳过）
- 断言: `EXPECT_EQ(fixture.handler->activeConnectionCount(), 1)`

**T05 — 空连接列表时定时器触发不崩溃**
- 前置条件: `DriverLabWsHandler` 已构造，`setPingIntervalForTest(50)` 缩短定时器，无客户端连接
- 输入: 等待定时器触发（`waitMs(200)`），`onPingTick` 在空列表上执行 sweep + ping 循环
- 预期: 无崩溃、无异常，连接数保持 0
- 断言: `EXPECT_EQ(handler.activeConnectionCount(), 0)`

**T06 — 超时连接被 sweep 清理，收到 CloseCodeGoingAway，配额释放后可重连**
- 前置条件: `WsTestFixture` 启动，`setPingIntervalForTest(50)` 缩短定时器，WebSocket 客户端已连接
- 输入: 通过 `connectionAt(0)->setLastPongAtForTest(QDateTime::currentDateTimeUtc().addMSecs(-61000))` 将 `lastPongAt` 设为 61 秒前，等待定时器触发 sweep；sweep 清理后再连接一个新客户端验证配额释放
- 预期: 连接被 `sweepDeadConnections` 从列表移除，`closeForPongTimeout()` 发送 `CloseCodeGoingAway` + `"pong timeout"`，连接计数归零；新客户端可成功连接
- 断言: `EXPECT_EQ(fixture.handler->activeConnectionCount(), 0)`；通过独立 `disconnected` 监听捕获 `ws.closeCode() == QWebSocketProtocol::CloseCodeGoingAway`；新连接后 `EXPECT_EQ(activeConnectionCount(), 1)`

**T07 — closeAll 停止定时器并清空所有连接**
- 前置条件: `WsTestFixture` 启动，`setPingIntervalForTest(50)` 缩短定时器，两个 WebSocket 客户端已连接
- 输入: 调用 `fixture.handler->closeAll()`，然后连接一个新客户端，将其 `lastPongAt` 设为 61 秒前，等待若干定时器周期
- 预期: closeAll 后连接数归零；新连接的过期 `lastPongAt` 不会被 sweep 清理（因为定时器已停止），连接数保持 1
- 断言: closeAll 后 `EXPECT_EQ(activeConnectionCount(), 0)`；新连接后等待 `waitMs(200)` 后 `EXPECT_EQ(activeConnectionCount(), 1)`（定时器已停，sweep 不触发）

#### 测试代码（必填）

```cpp
// ---------------------------------------------------------------------------
// T01 — 心跳常量值正确
// ---------------------------------------------------------------------------
TEST(DriverLabWsHandlerTest, HeartbeatConstants) {
    EXPECT_EQ(DriverLabWsHandler::kPingIntervalMs, 30000);
    EXPECT_EQ(DriverLabWsHandler::kPongTimeoutMs, 60000);
    EXPECT_EQ(DriverLabWsHandler::kPongTimeoutMs,
              DriverLabWsHandler::kPingIntervalMs * 2);
}
```

```cpp
// ---------------------------------------------------------------------------
// T02 — 服务端 ping → 客户端 auto-pong → 服务端 onPongReceived 更新 lastPongAt
// ---------------------------------------------------------------------------
TEST(DriverLabWsHandlerTest, PongUpdatesLastPongAt) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    // Shorten ping interval to 50ms so timer fires quickly
    fixture.handler->setPingIntervalForTest(50);

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver")
                       .arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    ASSERT_TRUE(connected);
    waitMs(200);  // wait for driver.started

    // Record initial lastPongAt (≈ connection creation time)
    auto* conn = fixture.handler->connectionAt(0);
    ASSERT_NE(conn, nullptr);
    const QDateTime t0 = conn->lastPongAt();

    // Wait for timer to fire → server sends ping → client auto-replies pong
    // → server onPongReceived updates lastPongAt
    waitMs(300);

    EXPECT_GT(conn->lastPongAt(), t0);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 1);

    ws.close();
    waitMs(200);
}
```

```cpp
// ---------------------------------------------------------------------------
// T03 — 正常连接经过多次 sweep 后存活
// ---------------------------------------------------------------------------
TEST(DriverLabWsHandlerTest, HealthyConnectionSurvivesSweep) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    // Shorten ping interval so multiple timer ticks fire during test
    fixture.handler->setPingIntervalForTest(50);

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver")
                       .arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    ASSERT_TRUE(connected);
    waitMs(200);

    // Wait for multiple timer ticks (sweep + ping cycles)
    // Client auto-replies pong, so lastPongAt stays fresh → sweep skips
    waitMs(300);

    EXPECT_EQ(fixture.handler->activeConnectionCount(), 1);

    ws.close();
    waitMs(200);
}
```

```cpp
// ---------------------------------------------------------------------------
// T04 — 新建连接不会被立即 sweep
// ---------------------------------------------------------------------------
TEST(DriverLabWsHandlerTest, NewConnectionNotSweptImmediately) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    // Shorten ping interval so timer fires quickly after connect
    fixture.handler->setPingIntervalForTest(50);

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver")
                       .arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    ASSERT_TRUE(connected);

    // Wait for at least one timer tick → sweep runs on fresh connection
    // m_lastPongAt ≈ now → elapsed ≈ 0 → well within 60s → survives
    waitMs(150);

    EXPECT_EQ(fixture.handler->activeConnectionCount(), 1);

    ws.close();
    waitMs(200);
}
```
```cpp
// ---------------------------------------------------------------------------
// T05 — 空连接列表时定时器触发不崩溃
// ---------------------------------------------------------------------------
TEST(DriverLabWsHandlerTest, PingTickWithNoConnectionsIsNoOp) {
    stdiolink::DriverCatalog catalog;
    DriverLabWsHandler handler(&catalog);

    // Shorten interval so timer actually fires during test
    handler.setPingIntervalForTest(50);

    // No connections — multiple timer ticks must not crash
    EXPECT_EQ(handler.activeConnectionCount(), 0);
    waitMs(200);  // several ticks fire on empty list
    EXPECT_EQ(handler.activeConnectionCount(), 0);
}
```

```cpp
// ---------------------------------------------------------------------------
// T06 — 超时连接被 sweep 清理，收到 CloseCodeGoingAway，配额释放后可重连
// ---------------------------------------------------------------------------
TEST(DriverLabWsHandlerTest, TimeoutConnectionSweptWithGoingAway) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    // Shorten ping interval so timer fires quickly
    fixture.handler->setPingIntervalForTest(50);

    QWebSocket ws;
    QStringList messages;
    QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver")
                       .arg(fixture.port));

    const bool connected = attemptWsConnect(ws, url, messages, closeCode);
    ASSERT_TRUE(connected);
    waitMs(200);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 1);

    // attemptWsConnect's disconnected listener is bound to a local QEventLoop
    // that is already destroyed. Set up a persistent listener to capture the
    // close code when the server later closes this connection.
    QWebSocketProtocol::CloseCode sweepCloseCode = QWebSocketProtocol::CloseCodeNormal;
    bool disconnected = false;
    QObject::connect(&ws, &QWebSocket::disconnected, [&]() {
        sweepCloseCode = ws.closeCode();
        disconnected = true;
    });

    // Simulate pong timeout: set lastPongAt to 61 seconds ago
    auto* conn = fixture.handler->connectionAt(0);
    ASSERT_NE(conn, nullptr);
    conn->setLastPongAtForTest(
        QDateTime::currentDateTimeUtc().addMSecs(-61000));

    // Wait for next timer tick → sweep detects timeout → closeForPongTimeout
    waitMs(300);

    EXPECT_EQ(fixture.handler->activeConnectionCount(), 0);
    EXPECT_TRUE(disconnected);
    EXPECT_EQ(sweepCloseCode, QWebSocketProtocol::CloseCodeGoingAway);

    // Verify quota released: a new client can connect successfully
    QWebSocket ws2;
    QStringList msg2;
    QWebSocketProtocol::CloseCode cc2 = QWebSocketProtocol::CloseCodeNormal;
    const bool reconnected = attemptWsConnect(ws2, url, msg2, cc2);
    ASSERT_TRUE(reconnected);
    waitMs(200);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 1);

    ws2.close();
    waitMs(200);
}
```

```cpp
// ---------------------------------------------------------------------------
// T07 — closeAll 停止定时器并清空所有连接
// ---------------------------------------------------------------------------
TEST(DriverLabWsHandlerTest, CloseAllStopsPingTimer) {
    WsTestFixture fixture;
    ASSERT_TRUE(fixture.setup());

    fixture.handler->setPingIntervalForTest(50);

    QWebSocket ws1, ws2;
    QStringList msg1, msg2;
    QWebSocketProtocol::CloseCode cc1 = QWebSocketProtocol::CloseCodeNormal;
    QWebSocketProtocol::CloseCode cc2 = QWebSocketProtocol::CloseCodeNormal;
    const QUrl url(QString("ws://127.0.0.1:%1/api/driverlab/test_driver")
                       .arg(fixture.port));

    attemptWsConnect(ws1, url, msg1, cc1);
    attemptWsConnect(ws2, url, msg2, cc2);
    waitMs(300);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 2);

    // closeAll: stop timer + delete all connections
    fixture.handler->closeAll();
    waitMs(200);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 0);

    // Verify timer is actually stopped: connect a new client,
    // set its lastPongAt to 61s ago, wait for would-be timer ticks.
    // If timer were still running, sweep would clean this connection.
    QWebSocket ws3;
    QStringList msg3;
    QWebSocketProtocol::CloseCode cc3 = QWebSocketProtocol::CloseCodeNormal;
    const bool connected = attemptWsConnect(ws3, url, msg3, cc3);
    ASSERT_TRUE(connected);
    waitMs(200);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 1);

    auto* conn = fixture.handler->connectionAt(0);
    ASSERT_NE(conn, nullptr);
    conn->setLastPongAtForTest(
        QDateTime::currentDateTimeUtc().addMSecs(-61000));

    // Wait well beyond timer interval — if timer were active,
    // sweep would have removed this expired connection
    waitMs(300);
    EXPECT_EQ(fixture.handler->activeConnectionCount(), 1);

    ws3.close();
    waitMs(200);
}
```

### 6.2 集成测试
- 真实 WebSocket 连接下验证服务端 ping → 客户端 auto-pong → 服务端 `onPongReceived` 更新 `lastPongAt`（T02 覆盖）
- 验证超时连接被 sweep 正确清理，客户端收到 `CloseCodeGoingAway` 关闭码（T06 覆盖）
- 验证正常连接经过多次 sweep 周期后存活（T03 覆盖）

### 6.3 验收标准
- [ ] WebSocket 连接建立后，服务端周期性发送 ping 帧（T02, T03 验证定时器触发）
- [ ] 客户端正常回复 pong 时，`lastPongAt` 被更新，连接保持存活（T02, T03）
- [ ] 连接超过 60 秒未收到 pong 时，服务端通过 `closeForPongTimeout()` 主动关闭，客户端收到 `CloseCodeGoingAway`（T06）
- [ ] 僵死连接被清理后，连接配额释放，新连接可正常建立（T06）
- [ ] 现有 WebSocket 功能（exec、cancel、meta query、crash backoff）无回归（T03, T07）

## 7. 风险与控制
- 风险: ping 定时器在高负载下可能与 Driver 进程 I/O 竞争事件循环
  - 控制: 30s 间隔足够宽松，ping 帧极小（≤125 字节），对事件循环影响可忽略
  - 测试覆盖: T02, T03
- 风险: 网络抖动导致 pong 偶尔延迟，误判为超时
  - 控制: 60s 超时窗口（2 倍心跳间隔）提供充足容错；与 SSE 侧策略一致，已验证可靠
  - 测试覆盖: T04（新建连接不被误判）
- 风险: `closeAll` 与 sweep 并发执行导致 double-free
  - 控制: 两者均在同一线程（Qt 主事件循环）执行，无并发风险；`closeAll` 先停定时器再清理
  - 测试覆盖: T07

## 8. 里程碑完成定义（DoD）
- [ ] 代码与测试完成
- [ ] `DriverLabWsHandler` 心跳定时器正常启停
- [ ] `DriverLabWsConnection` 正确处理 pong 信号并更新时间戳
- [ ] 超时连接被自动清理
- [ ] 全量既有测试无回归
- [ ] 文档同步完成
- [ ] 向后兼容确认（WebSocket 客户端无需任何改动，ping/pong 由协议层自动处理）

---
