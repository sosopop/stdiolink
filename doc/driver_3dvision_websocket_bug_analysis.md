# driver_3dvision WebSocket 接口问题分析（修订版）

**日期**: 2026-03-04  
**驱动**: `src/drivers/driver_3dvision`  
**审查方式**: 以当前源码为准，逐条校正文档中的结论（不依赖历史行号）

---

## 1. 执行结论（先说结论）

1. 旧文档中把“事件循环阻塞（Bug 2）”列为当前阻塞项，这一结论已不成立。`DriverCore` 在 M94 后已经改为“读线程阻塞读 stdin + 主线程事件循环处理”。
2. `driver_3dvision` 的 WebSocket 路径仍有真实缺陷，核心是 **Responder 生命周期错误（悬空指针）** 与 **ws.connect 成功语义错误（提前 done）**。
3. 旧文档中的部分修复示例代码（`StdinReaderWorker` 版本）已过时，应删除，避免误导后续开发。

---

## 2. 已确认“旧文档不合理”的条目

### 2.1 Bug 2（主线程 readLine 阻塞导致事件循环不运行）不再是现状

当前 `DriverCore` 已具备事件循环能力：
- `DriverCore::runStdioMode()` 中，stdin 读取在 reader 线程执行（`QThread::create(...)`）。
- 行数据通过 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 投递到主线程。
- 主线程执行 `QCoreApplication::exec()`。

对应源码：
- `src/stdiolink/driver/driver_core.cpp`：`runStdioMode()`、`handleStdioLineOnMainThread()`、`scheduleStdioQuit()`

**结论**：Bug 2 作为“当前未修复问题”应删除；保留为“历史问题，已由 M94 解决”。

### 2.2 旧文档中的 `StdinReaderWorker` 修复代码已过时

当前实现不是 `moveToThread + Worker 对象`，而是 `QThread::create + lambda`。两者思想一致，但旧代码片段已与现网实现不一致，不应继续作为执行参考。

### 2.3 旧文档大量精确行号已失效

代码迭代后行号漂移明显。后续定位应使用“函数名 + 文件路径”，不要再以旧行号作为证据。

---

## 3. 当前仍存在的问题（源码已核实）

### Bug 1: `m_wsResponder` 悬空指针风险（P0）

`Vision3DHandler` 保存了 `IResponder* m_wsResponder`，在 `handleWsConnect()` 中执行 `m_wsResponder = &resp`。  
`resp` 的真实对象来自 `DriverCore::processOneLine()` 内部栈变量（`StdioResponder responder`），命令返回后生命周期结束。后续 WebSocket 异步信号再用该指针属于未定义行为。

对应源码：
- `src/drivers/driver_3dvision/main.cpp`：成员 `m_wsResponder`、`handleWsConnect()`、构造函数信号回调中对 `m_wsResponder->event(...)` 的使用
- `src/stdiolink/driver/driver_core.cpp`：`processOneLine()` 内部的 `StdioResponder responder`

### Bug A: `ws.connect` 在握手完成前提前返回 `done`（P0）

`handleWsConnect()` 只要 `connectToServer()` 返回 true 就立即 `resp.done(...)`。  
但 `WebSocketClient::connectToServer()` 只是调用 `m_socket->open(...)` 发起异步连接，不能代表连接成功。

对应源码：
- `src/drivers/driver_3dvision/main.cpp`：`handleWsConnect()`
- `src/drivers/driver_3dvision/websocket_client.cpp`：`connectToServer()`、`onConnected()`

### Bug 3: 未消费 `WebSocketClient::connected` 信号（P1）

`Vision3DHandler` 构造函数目前只连接了：`eventReceived`、`disconnected`、`error`，没有连接 `connected`。

对应源码：
- `src/drivers/driver_3dvision/main.cpp`：`Vision3DHandler::Vision3DHandler()`

### Bug 5: 事件 payload 双层包装（P1）

`WebSocketClient` 上抛的是完整 `eventObj`（包含 `event`/`data`）。  
`Vision3DHandler` 又将该对象作为 `IResponder::event(eventName, ..., data)` 的 `data` 传入，`StdioResponder::event(...)` 会再次包装成 `{event, data}`，最终形成嵌套（`data.data`）。

对应源码：
- `src/drivers/driver_3dvision/websocket_client.cpp`：`emit eventReceived(eventName, eventObj)`
- `src/drivers/driver_3dvision/main.cpp`：`m_wsResponder->event(eventName, 0, data)`
- `src/stdiolink/driver/stdio_responder.cpp`：`event(const QString&, int, const QJsonValue&)`

### Bug 6: 地址切换时不会重连（P2）

`WebSocketClient::connectToServer(url)` 在 `m_connected == true` 时直接返回，不比较 URL。用户从 A 地址切到 B 地址时不会切换连接。

对应源码：
- `src/drivers/driver_3dvision/websocket_client.cpp`：`connectToServer()`

### Bug E: 重连订阅恢复策略缺失（P2）

当前 `onConnected()` 未做订阅恢复。`disconnect()` 还会清空 `m_subscriptions`。这意味着“地址切换重连后自动恢复订阅”能力不存在。

对应源码：
- `src/drivers/driver_3dvision/websocket_client.cpp`：`onConnected()`、`disconnect()`

### Bug 7: 事件格式校验不足（P2）

`onTextMessageReceived()` 中解析出 `eventName` 后直接 `emit eventReceived(...)`，没有对空事件名或 `data` 缺失做有效防御。

对应源码：
- `src/drivers/driver_3dvision/websocket_client.cpp`：`onTextMessageReceived()`

### Bug 4: `ws.connect` 的 token 策略仍未落地（待确认后实现）

当前 `ws.connect` Meta 只有 `addr` 参数，无可选 `token`，实际 URL 也未拼接 token。  
是否“必须 token”取决于服务端约束，仍需联调确认，但“可选 token 参数”作为排障能力是合理增强。

对应源码：
- `src/drivers/driver_3dvision/main.cpp`：`buildMeta()` 中 `ws.connect`、`handleWsConnect()`

---

## 4. 修复建议（按优先级，避免过度设计）

### P0-1 立刻修复

1. 去掉 `m_wsResponder` 长生命周期指针，禁止保存 `IResponder&` 地址。  
2. `ws.connect` 成功语义改为“握手成功后再 done”；失败/超时返回 error。  
3. 连接 `WebSocketClient::connected` 信号，并把连接状态事件输出做成可观测日志。  
4. 修复事件映射：对外 `event.data` 只传业务 payload（优先 `eventObj["data"]`），避免双层包装。

### P2 次优先修复

1. `connectToServer(url)` 支持 URL 变更检测（地址变更时触发重连）。  
2. 明确订阅恢复策略：
   - 地址切换重连时保留并恢复订阅；
   - 仅显式 `ws.disconnect` 清空订阅集合。  
3. 增加事件格式兜底：空 `event` 名直接丢弃并输出 warning。

### Token 策略（与服务端联调后定版）

建议在 `ws.connect` 增加可选 `token` 参数：
- 优先用参数 `token`
- 其次回退 `m_token`（login 缓存）
- 两者都空时不带 token 尝试

---

## 5. 测试覆盖缺口（当前确实缺）

目前仓库内没有针对 `driver_3dvision` WebSocket 命令路径的专用单测，建议补最小回归集：

1. `ws.connect` 仅在 connected 后返回 done（含 timeout 分支）。  
2. 连接建立后异步事件到达时不触发悬空指针（生命周期安全）。  
3. 事件输出结构无 `data.data` 双层嵌套。  
4. 地址切换重连与订阅恢复行为符合预期。  
5. 非法事件报文（空 event）不会污染上层事件流。

---

## 6. 修订后的状态汇总

| 条目 | 当前状态 | 处理建议 |
|---|---|---|
| Bug 1 悬空指针 | 未修复 | P0 立即修复 |
| Bug 2 事件循环阻塞 | **已在 M94 修复** | 从“现存问题”移除 |
| Bug A ws.connect 提前 done | 未修复 | P0 立即修复 |
| Bug 3 connected 信号未消费 | 未修复 | P1 修复 |
| Bug 5 事件双层包装 | 未修复 | P1 修复 |
| Bug 6 地址切换 | 未修复 | P2 修复 |
| Bug E 订阅恢复 | 未修复 | P2 修复 |
| Bug 7 事件格式校验 | 未修复 | P2 修复 |
| Bug 4 token 策略 | 未落地 | 联调确认后实现 |

---

## 7. 参考源码

- `src/drivers/driver_3dvision/main.cpp`
- `src/drivers/driver_3dvision/websocket_client.h`
- `src/drivers/driver_3dvision/websocket_client.cpp`
- `src/stdiolink/driver/driver_core.cpp`
- `src/stdiolink/driver/stdio_responder.cpp`
