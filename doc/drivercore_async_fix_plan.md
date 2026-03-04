# DriverCore 异步事件循环修复方案（执行版）

**文档版本**: v1.0  
**日期**: 2026-03-04  
**适用范围**: `src/stdiolink/driver/DriverCore` 及依赖 KeepAlive 的驱动运行链路

---

## 1. 背景与问题定义

基于 `doc/drivercore_event_loop_issue.md` 与现有代码核查，当前问题根因明确：

- `DriverCore::runStdioMode()` 在主线程执行 `QTextStream::readLine()` 阻塞循环。
- 主线程不进入 `QCoreApplication::exec()`，Qt 异步事件无法持续调度。
- 依赖异步信号/定时器的功能在 KeepAlive 下失效或行为异常。

关键代码位置：
- `src/stdiolink/driver/driver_core.cpp:25-49`

---

## 2. 影响面（已确认）

### 2.1 直接受影响（功能级）

1. `driver_3dvision` 的 WebSocket 命令链路（`ws.connect/ws.subscribe/...`）。
2. `driver_modbustcp_server` 的 `start_server` KeepAlive 路径。
3. `driver_modbusrtu_server` 的 `start_server` KeepAlive 路径。
4. `driver_modbusrtu_serial_server` 的 `start_server` KeepAlive 路径。

### 2.2 间接受影响（平台链路）

1. DriverLab WebSocket 会注入 `--profile=keepalive`，与上述问题直接耦合。  
   代码：`src/stdiolink_server/http/driverlab_ws_connection.cpp:63-65`
2. JS `openDriver()` 默认 profile 策略为 keepalive，同样会触发该问题。  
   代码：`src/stdiolink_service/proxy/driver_proxy.cpp:46-54`

### 2.3 不作为本次主修复对象

1. 使用阻塞 API（`waitForReadyRead/waitForBytesWritten`）的主站驱动：`modbustcp/modbusrtu/modbusrtu_serial/plc_crane`。
2. `driver_3dvision` 中 responder 生命周期问题（悬空指针）属于并行 P0 问题，需要单独修复，不应混入 DriverCore 主改造。

---

## 3. 决策与边界

### 3.1 采用方案（已确认）

**采用**: `读线程阻塞读 stdin + 主线程队列投递 + 主线程 app.exec()`。

### 3.2 不采用方案

1. 不采用 `QSocketNotifier(fileno(stdin))` 作为主路径。  
   原因：已在 Windows 管道 stdin 场景验证不可靠。
2. 不采用 `QWinEventNotifier` 作为默认路径。  
   原因：引入平台分叉，不满足“跨平台一次到位、处理方式一致”。
3. 不采用“每个驱动自行创建独立事件线程”作为本次主方案。  
   原因：重复改造、维护成本高、回归面大，不是框架级最优解。

### 3.3 本次明确不做（避免过度设计）

1. 不引入多请求并行执行模型（仍保持 `processOneLine()` 主线程串行）。
2. 不改 JSONL 协议格式。
3. 不改变 `--profile=oneshot/keepalive` 的对外语义。
4. 不在本次同时重构 Host 的单任务模型（`Driver::m_cur`）。

---

## 4. 目标架构

### 4.1 线程模型

- **主线程**:
  - 运行 `QCoreApplication::exec()`。
  - 串行执行 `processOneLine()`。
  - 处理 Qt 异步事件（网络/定时器/socket/queued signals）。
- **stdin 读线程**:
  - 阻塞执行 `readLine()`。
  - 读到完整行后通过 `Qt::QueuedConnection` 投递给主线程。

### 4.2 数据流

1. Host 写入一行 JSONL 到 driver stdin。
2. 读线程 `readLine()` 取到文本。
3. 读线程发射 `lineReady(QByteArray)`。
4. 主线程槽函数收到后调用 `processOneLine()`。
5. `StdioResponder` 继续写 stdout（行为保持不变）。

### 4.3 退出语义

1. **OneShot**: 处理第一条有效命令后请求退出主循环。
2. **KeepAlive**:
   - stdin EOF（对端关闭）后退出。
   - 或外部进程被 kill/terminate 退出。
3. **Console 模式**: 保持原有同步处理，不进入此改造路径。

---

## 5. 详细实现设计（按文件）

### 5.1 `src/stdiolink/driver/driver_core.h`

目标：仅做最小必要改动，避免破坏对外接口。

建议新增私有成员（示意）：

```cpp
// 线程与状态控制（仅私有）
std::atomic_bool m_stdioStopRequested{false};
std::atomic_bool m_stdioAcceptLines{true};
bool m_stdioQuitScheduled = false;
```

建议新增私有辅助方法（示意）：

```cpp
void handleStdioLineOnMainThread(const QByteArray& line);
void scheduleStdioQuit();
```

说明：
- `run()/run(argc,argv)` 对外签名不变。
- `RunMode/Profile` 枚举不变。

### 5.2 `src/stdiolink/driver/driver_core.cpp`

#### 5.2.1 新增内部 worker（cpp 匿名命名空间）

建议新增 `StdinReaderWorker : public QObject`（仅 `driver_core.cpp` 可见）：

- 输入：`stdin`
- 输出信号：
  - `lineReady(QByteArray)`
  - `eofReached()`
  - `readerError(QString)`（可选）
- 行为：
  - 循环 `readLine()` 阻塞读取。
  - 读取到非空行后发信号。
  - 遇 EOF 发 `eofReached()` 并退出。

#### 5.2.2 `runStdioMode()` 改造

目标行为：主线程进入事件循环，reader 线程负责输入。

核心步骤：

1. 校验 `m_handler` 非空。
2. 校验 `QCoreApplication::instance()` 非空。  
   若为空：输出错误并返回 1（防御式处理）。
3. 初始化线程状态：
   - `m_stdioStopRequested = false`
   - `m_stdioAcceptLines = true`
   - `m_stdioQuitScheduled = false`
4. 创建 reader worker + `QThread`。
5. 连接信号（全部 `QueuedConnection` 到主线程）。
6. `thread.start()`。
7. 主线程 `app->exec()`。
8. 退出时按“先打断阻塞读、再 join”收尾，避免卡死：
   - 置 stop 标志；
   - 关闭 stdin 读端（仅在退出路径执行）以打断 `readLine()`；
   - `thread.wait(timeout)` 等待 reader 线程退出；
   - 若超时，仅记录告警并继续退出流程（避免主线程永久挂住）。

#### 5.2.3 主线程命令处理槽

`handleStdioLineOnMainThread(line)` 处理原则：

1. 若 `m_stdioAcceptLines == false`，直接丢弃（保证 OneShot 下不会处理后续排队行）。
2. 空行跳过。
3. 调用 `processOneLine()`。
4. 如果 `m_profile == OneShot`：
   - `m_stdioAcceptLines = false`
   - 调用 `scheduleStdioQuit()`。

#### 5.2.4 EOF 处理

`eofReached()` 到主线程后：

- 设置 `m_stdioStopRequested = true`。
- 调用 `scheduleStdioQuit()`（幂等）。

#### 5.2.5 退出调度幂等

`scheduleStdioQuit()` 需要幂等：

- 若 `m_stdioQuitScheduled` 已 true，直接返回。
- 否则置 true，并 `QMetaObject::invokeMethod(app, "quit", Qt::QueuedConnection)`。

#### 5.2.6 语义保持点

1. `processOneLine()` 的 JSON 解析、meta 命令处理、参数校验、输出格式保持不变。
2. Console 模式 (`runConsoleMode`) 不改。
3. `StdioResponder` 输出协议不改。

---

## 6. 关键行为约束与注意事项

### 6.1 线程安全约束

1. `processOneLine()` 仅允许在主线程执行。
2. reader 线程不直接访问 handler/responder。
3. 仅通过 queued signal 传递原始行文本。

### 6.2 OneShot 边界

问题：reader 线程可能在主线程退出前多读几行。  
策略：主线程用 `m_stdioAcceptLines` 截断后续处理，保证只处理首条命令。

### 6.3 输入洪峰（不过度设计）

当前 Host 链路本身是“单任务串行请求”为主，短期不引入复杂背压机制。  
仅增加基本防御：

- 单行长度上限校验（可复用既有 parse 错误路径，建议在文档记录风险）。
- 若后续出现高并发写入场景，再单独立项做队列限流。

### 6.4 退出阶段的阻塞打断（必须落地）

`readLine()` 是阻塞调用，`thread.quit()` 不能可靠打断。若不主动打断，`OneShot` 下可能出现：

1. 主线程已 `app.quit()`；
2. reader 线程仍阻塞在 `readLine()`；
3. `thread.wait()` 卡住，进程无法按预期退出。

落地要求：

1. 退出阶段必须关闭 stdin 读端（仅退出路径，不影响正常协议阶段）。
2. `wait` 必须带超时（例如 1~2 秒），避免无限等待。
3. 超时后仅告警并继续退出，不做 `terminate()` 这类破坏性线程操作。

---

## 7. 与业务驱动配套改造（并行任务）

> 本节是依赖说明，不是 DriverCore 主方案的一部分。

1. `driver_3dvision` 必须并行修复 responder 生命周期问题（禁止保存 `IResponder*` 指向栈对象）。
2. `driver_3dvision ws.connect` 成功语义应改为“握手成功后 done”，避免假阳性。
3. `modbus*_server` 无需额外线程改造，DriverCore 修复后其 `start_server` 异步链路即可恢复。

---

## 8. 测试方案（可执行）

### 8.1 回归测试（必须）

1. 运行现有 `stdiolink_tests` 全量，确认无回归。
2. 重点关注：
   - `test_driver_main.cpp`
   - `test_meta_driver_main.cpp`
   - `test_host_driver.cpp`
   - `test_wait_any.cpp`
   - `test_driverlab_ws_handler.cpp`

### 8.2 新增测试（必须）

建议新增文件：`src/tests/test_driver_core_async.cpp`。

测试点：

1. **A01** 主线程事件循环可运行  
   场景：在 handler 内启动 `QTimer::singleShot`，在无后续 stdin 输入时仍可触发副作用。
2. **A02** KeepAlive 下 EOF 退出  
   场景：子进程收到 EOF 后应正常退出，不挂死。
3. **A03** OneShot 仅处理首命令  
   场景：stdin 连续写入两条命令，验证只处理第一条。
4. **A04** start_server 异步可用性回归（集成）  
   场景：拉起 `stdio.drv.modbustcp_server --profile=keepalive`，发送 `start_server` 后不再写新命令，从外部 TCP 客户端发请求，应能得到响应。
5. **A05** OneShot 退出不挂死  
   场景：发送一条命令触发 OneShot 退出，断言进程在超时阈值内退出（覆盖“阻塞读打断”逻辑）。

### 8.3 验收测试（场景级）

1. DriverLab 连接 keepalive 驱动后，`start_server` 型命令可在空闲状态下持续响应外部连接。
2. `driver_3dvision` 在修复生命周期后，WebSocket 事件可稳定推送（该项在并行任务验收）。

---

## 9. 实施步骤（建议顺序）

1. **Step 1**: 改造 `DriverCore`（worker + main-loop）。
2. **Step 2**: 新增/更新测试（A01-A05）。
3. **Step 3**: 跑全量单测并修复回归。
4. **Step 4**: 联调 `modbus*_server` keepalive 路径。
5. **Step 5**: 合并 `driver_3dvision` 并行修复，做端到端验证。

---

## 10. 风险与回滚

### 10.1 风险

1. OneShot 下多行排队导致额外命令被执行（已通过 `m_stdioAcceptLines` 规避）。
2. reader 线程退出同步不当导致进程退出卡住（需严格 `quit/wait`）。
3. 个别旧驱动可能依赖“readLine 阻塞副作用”（概率低，回归可覆盖）。

### 10.2 回滚策略

1. 保留当前 `runStdioMode()` 旧实现为临时分支备份（开发阶段）。
2. 若上线前出现阻断问题，可通过 revert `DriverCore` 单提交快速回退。
3. 文档中同步记录“已知受影响驱动列表”和回滚条件。

---

## 11. 完成标准（DoD）

以下条件全部满足，视为方案完成：

1. `DriverCore` 在 Stdio 模式主线程运行 `app.exec()`，输入读取移至读线程。
2. `--profile=keepalive` 下，异步驱动功能可在无后续 stdin 输入时继续工作。
3. `--profile=oneshot` 只处理一条命令后退出，语义与现状一致。
4. `stdiolink_tests` 全量通过。
5. 新增 `DriverCore` 异步改造测试用例并稳定通过。
6. 文档（本文件 + 关联调查文档）保持一致，无互相矛盾描述。

---

## 12. 关联文档

1. `doc/drivercore_event_loop_issue.md`（现状调查）
2. `doc/driver_3dvision_websocket_bug_analysis.md`（3dvision 并行问题）
3. `src/stdiolink/driver/driver_core.cpp`（主改造文件）
4. `src/stdiolink_server/http/driverlab_ws_connection.cpp`（KeepAlive 调用链）
5. `src/stdiolink_service/proxy/driver_proxy.cpp`（JS keepalive 默认策略）
