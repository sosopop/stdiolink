# 里程碑 94：DriverCore 异步事件循环修复（TDD）

> **前置条件**: 无特殊前置里程碑依赖；`doc/drivercore_async_fix_plan.md`（执行版）已完成全部技术决策
> **目标**: 以 TDD 方式将 `DriverCore::runStdioMode()` 从主线程阻塞 `readLine()` 循环改造为「读线程阻塞读 + 主线程 `app.exec()` 事件循环」模型，使 KeepAlive 模式下 Qt 异步功能（网络/定时器/信号）可正常调度

---

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| `src/stdiolink/driver` | `DriverCore::runStdioMode()` 改为「读线程阻塞读 + 主线程事件循环处理」 |
| `src/tests` | 新增 DriverCore 异步回归测试（跨线程输入、EOF、OneShot、异步事件、非法 JSON 兼容） |
| `src/drivers/*_server` 运行链路 | `start_server` 在 keepalive 下不依赖后续 stdin 也能处理外部连接 |
| 文档 | 里程碑方案、测试路径矩阵、DoD 与风险控制落地 |

- `DriverCore` 在 Stdio 模式下主线程运行 `QCoreApplication::exec()`，stdin 读取移至独立读线程
- `--profile=keepalive` 下，异步驱动功能（WebSocket、定时器、TCP 监听）可在无后续 stdin 输入时继续工作
- `--profile=oneshot` 只处理一条命令后退出，语义与改造前一致
- Console 模式 (`runConsoleMode`) 行为不变
- 保持 `run()/run(argc, argv)`、`RunMode`、`Profile` 对外接口不变
- 退出流程可打断阻塞 `readLine`，避免 `thread.wait()` 卡死
- stdin 读取通过 `QThread::create()` + lambda 实现独立读线程（无需额外类定义）
- 新增 `test_driver_core_async.cpp` 测试文件，覆盖全部改造路径
- 更新 `test_driver_main.cpp` 辅助进程以支持异步测试场景
- `stdiolink_tests` 全量通过，无回归

---

## 2. 背景与问题

当前 `DriverCore::runStdioMode()`（`src/stdiolink/driver/driver_core.cpp:25-49`）在主线程执行 `QTextStream::readLine()` 阻塞循环，主线程永不进入 `QCoreApplication::exec()`，导致：

1. Qt 异步事件（信号/定时器/网络 socket）在 KeepAlive 下无法持续调度
2. `driver_3dvision` 的 WebSocket 订阅链路失效
3. `modbus*_server` 的 `start_server` KeepAlive 路径无法响应外部连接
4. DriverLab WebSocket 注入 `--profile=keepalive` 后触发上述问题

**范围**:
- `src/stdiolink/driver/driver_core.h` — 新增私有成员与辅助方法声明
- `src/stdiolink/driver/driver_core.cpp` — 改造 `runStdioMode()`，新增 `QThread::create()` 读线程及辅助方法
- `src/tests/test_driver_core_async.cpp` — 新增异步改造测试
- `src/tests/test_driver_main.cpp` — 扩展辅助 Driver 以支持异步测试命令
- `src/tests/CMakeLists.txt` — 注册新测试文件
- `src/tests/test_host_driver.cpp` — 若现有 Host↔Driver 集成测试在改造后出现回归，则补充 keepalive 异步断言（触发条件：`test_host_driver` 出现 FAIL）

**非目标**:
- 不引入多请求并行执行模型（仍保持 `processOneLine()` 主线程串行）
- 不改 JSONL 协议格式
- 不改变 `--profile=oneshot/keepalive` 的对外语义
- 不修改 Console 模式 (`runConsoleMode`)
- 不修改 `StdioResponder` 输出格式
- 不修复 `driver_3dvision` responder 生命周期问题（属并行 P0 任务）
- 不引入「每个驱动独立事件线程」架构
- 不在本次同时重构 Host 的单任务模型（`Driver::m_cur`）
- 不引入输入背压 / 队列限流机制

---

## 3. 技术要点

### 3.1 线程模型（核心决策）

**Before**（当前）：

```
主线程: readLine() 阻塞循环 → processOneLine() → 写 stdout
         ↑ 永不进入 app.exec()，Qt 异步事件无法调度
```

**After**（改造后）：

```
读线程 (QThread::create lambda):
  while (!stopRequested && !atEnd) {
      line = readLine();   // 阻塞
      invokeMethod(→ handleStdioLineOnMainThread(line))  // QueuedConnection
  }
  invokeMethod(→ scheduleStdioQuit())

主线程:
  app.exec()  // 持续分发事件
  ├── handleStdioLineOnMainThread() → processOneLine()
  ├── scheduleStdioQuit() → app.quit()
  ├── Qt 异步事件可正常调度（定时器/网络/WebSocket）
  └── 退出时：关闭 stdin fd → thread.wait(timeout) → 超时则 terminate
```

### 3.2 读线程实现方案

采用 `QThread::create()` + `QMetaObject::invokeMethod()` 方案，**不使用 `Q_OBJECT`**，避免匿名命名空间 moc 构建脆弱点：

```cpp
// driver_core.cpp — runStdioMode() 内部，无需 Q_OBJECT / .moc
auto* readerThread = QThread::create([this]() {
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            scheduleStdioQuit();
        }, Qt::QueuedConnection);
        return;
    }
    QTextStream in(&input);
    while (!m_stdioStopRequested.load(std::memory_order_relaxed) && !in.atEnd()) {
        QString line = in.readLine();
        if (line.isNull()) break;       // EOF 或读取错误
        if (!line.isEmpty()) {
            QByteArray data = line.toUtf8();
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, data]() {
                handleStdioLineOnMainThread(data);
            }, Qt::QueuedConnection);
        }
    }
    // EOF 到达，通知主线程退出
    QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
        m_stdioStopRequested.store(true, std::memory_order_relaxed);
        scheduleStdioQuit();
    }, Qt::QueuedConnection);
});
```

**关键约束**：
- 通过 `QMetaObject::invokeMethod` + `Qt::QueuedConnection` 投递到主线程，无需 `Q_OBJECT` 和 moc
- `m_stdioStopRequested` 为 `DriverCore` 成员的 `std::atomic_bool`，实现协作式停止
- 读线程不直接访问 handler/responder，仅投递原始行字节
- `QThread` 堆分配（`QThread::create` 返回 `QThread*`），避免栈对象生命周期问题

> **备选方案**：若实现时发现 `QMetaObject::invokeMethod` 的 lambda 重载在当前 Qt 版本不可用，可退回 `Q_OBJECT` + `driver_core.moc` 方案（§3.2.1），但需在独立具名命名空间中定义 worker 类以确保 moc 可处理。
>
> #### 3.2.1 备选：Q_OBJECT 方案注意事项
> - worker 类不得在匿名命名空间中定义（moc 无法处理）
> - 需在 `driver_core.cpp` 末尾添加 `#include "driver_core.moc"`
> - 编译验证：若出现 `undefined reference to vtable` 则说明 moc 未生效

### 3.3 DriverCore 新增私有成员

```cpp
// driver_core.h — 新增私有成员

// 线程与状态控制（仅 Stdio 模式使用）
QThread* m_stdinReaderThread = nullptr;      // 堆分配，退出时安全销毁
std::atomic_bool m_stdioStopRequested{false};
std::atomic_bool m_stdioAcceptLines{true};
bool m_stdioQuitScheduled = false;

// 私有辅助方法
void handleStdioLineOnMainThread(const QByteArray& line);
void scheduleStdioQuit();
```

### 3.4 runStdioMode() 改造（Before / After）

**Before**：

```cpp
int DriverCore::runStdioMode() {
    if (!m_handler) return 1;
    QFile input;
    (void)input.open(stdin, QIODevice::ReadOnly);
    QTextStream in(&input);
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.isEmpty()) continue;
        if (!processOneLine(line.toUtf8())) { /* 继续 */ }
        if (m_profile == Profile::OneShot) break;
    }
    return 0;
}
```

**After**：

```cpp
int DriverCore::runStdioMode() {
    if (!m_handler) return 1;

    auto* app = QCoreApplication::instance();
    if (!app) {
        QFile err;
        (void)err.open(stderr, QIODevice::WriteOnly);
        err.write("DriverCore: QCoreApplication not created\n");
        err.flush();
        return 1;
    }

    // 初始化线程状态
    m_stdioStopRequested.store(false, std::memory_order_relaxed);
    m_stdioAcceptLines.store(true, std::memory_order_relaxed);
    m_stdioQuitScheduled = false;

    // 创建 reader 线程（堆分配，避免栈对象析构时线程仍在运行的 UB）
    m_stdinReaderThread = QThread::create([this]() {
        QFile input;
        if (!input.open(stdin, QIODevice::ReadOnly)) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
                scheduleStdioQuit();
            }, Qt::QueuedConnection);
            return;
        }
        QTextStream in(&input);
        while (!m_stdioStopRequested.load(std::memory_order_relaxed) && !in.atEnd()) {
            QString line = in.readLine();
            if (line.isNull()) break;
            if (!line.isEmpty()) {
                QByteArray data = line.toUtf8();
                QMetaObject::invokeMethod(QCoreApplication::instance(), [this, data]() {
                    handleStdioLineOnMainThread(data);
                }, Qt::QueuedConnection);
            }
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_stdioStopRequested.store(true, std::memory_order_relaxed);
            scheduleStdioQuit();
        }, Qt::QueuedConnection);
    });

    m_stdinReaderThread->start();

    // 主线程进入事件循环
    int exitCode = app->exec();

    // === 退出收尾（三阶段） ===
    m_stdioStopRequested.store(true, std::memory_order_relaxed);

    // 阶段 1：关闭 stdin 底层 fd 以打断 readLine() 阻塞
    // 注意：不用 ::fclose(stdin)，避免跨线程操作 FILE* 的 CRT 未定义行为
    // 改用 fd 级关闭，在 Windows 管道场景下可安全打断阻塞读
    // 需在 driver_core.cpp 顶部引入平台头文件：
    //   #ifdef Q_OS_WIN
    //     #include <io.h>      // _close, _fileno
    //   #else
    //     #include <unistd.h>  // close, fileno
    //   #endif
#ifdef Q_OS_WIN
    ::_close(_fileno(stdin));
#else
    ::close(fileno(stdin));
#endif

    // 阶段 2：等待线程退出
    if (!m_stdinReaderThread->wait(2000)) {
        // 阶段 3：超时兜底 — 必须 terminate 以避免 delete 运行中线程导致崩溃
        qWarning("DriverCore: stdin reader thread did not exit within 2s, forcing terminate");
        m_stdinReaderThread->terminate();
        m_stdinReaderThread->wait(500);  // terminate 后短暂等待
    }

    delete m_stdinReaderThread;
    m_stdinReaderThread = nullptr;

    return exitCode;
}
```

### 3.5 handleStdioLineOnMainThread() 处理原则

```cpp
void DriverCore::handleStdioLineOnMainThread(const QByteArray& line) {
    // OneShot 下截断后续排队行
    if (!m_stdioAcceptLines.load(std::memory_order_relaxed)) return;
    if (line.trimmed().isEmpty()) return;

    processOneLine(line);

    if (m_profile == Profile::OneShot) {
        m_stdioAcceptLines.store(false, std::memory_order_relaxed);
        scheduleStdioQuit();
    }
}
```

**关键决策**：`m_stdioAcceptLines` 保证 OneShot 模式下即使 reader 线程多读几行（信号已在队列中），主线程也只处理首条命令。

### 3.6 scheduleStdioQuit() 幂等退出

```cpp
void DriverCore::scheduleStdioQuit() {
    if (m_stdioQuitScheduled) return;
    m_stdioQuitScheduled = true;
    QMetaObject::invokeMethod(QCoreApplication::instance(),
                              "quit", Qt::QueuedConnection);
}
```

### 3.7 语义保持点（不变项）

| 组件 | 行为 |
|------|------|
| `processOneLine()` | JSON 解析、meta 命令处理、参数校验、输出格式 — 不变 |
| `runConsoleMode()` | 同步处理 — 不变 |
| `StdioResponder` | stdout 输出协议 — 不变 |
| `run(int, char**)` | 命令行参数解析、profile/mode 检测 — 不变 |
| `Profile/RunMode` 枚举 | 对外语义 — 不变 |

### 3.8 退出阶段阻塞打断（三阶段）

| 阶段 | 行为 | 理由 |
|------|------|------|
| 1. 置 `m_stdioStopRequested = true` | 读线程下次循环检查时退出 | 协作式停止 |
| 2. 关闭 stdin 底层 fd | 打断当前 `readLine()` 阻塞 | `readLine()` 返回 null/EOF |
| 3a. `thread->wait(2000)` 成功 | 线程已退出，`delete thread` | 正常收尾 |
| 3b. `thread->wait(2000)` 超时 | `thread->terminate()` + `wait(500)` + `delete` | 避免 delete 运行中 QThread 导致崩溃 |

**关于 stdin 关闭方式**：
- **不使用** `::fclose(stdin)`：`fclose` 操作 `FILE*` 层，读线程同时通过同一 `FILE*` 调用 `readLine()`，跨线程操作 CRT `FILE*` 是未定义行为（尤其 Windows MSVCRT）
- **改用** fd 级关闭（`_close(_fileno(stdin))` / `close(fileno(stdin))`）：直接关闭底层文件描述符，使阻塞的 read 系统调用返回错误，`QTextStream::readLine()` 将感知到 EOF 或 I/O error 并返回 null
- 仅在退出路径执行，不影响正常协议阶段

**关于 `terminate()` 兜底**：
- `QThread::terminate()` 是危险操作（不释放锁、不执行析构），但在「等待超时 + 进程即将退出」场景下是可接受的最后手段
- 不 terminate 直接 delete 运行中 QThread 会触发 Qt 断言崩溃（`QThread::~QThread: Destroyed while thread is still running`）
- 三阶段保证：正常情况走阶段 3a（fd 关闭后 readLine 返回），极端情况走阶段 3b（terminate 兜底）

### 3.9 退出策略与错误处理契约

| 场景 | 行为 | 退出码 |
|------|------|--------|
| `m_handler == nullptr` | 直接返回失败 | `1` |
| `QCoreApplication::instance() == nullptr` | 输出错误到 stderr 并返回 | `1` |
| OneShot 处理完首条有效命令 | 设置 `m_stdioAcceptLines=false`，调度 `app.quit()` | `0` |
| KeepAlive 收到 stdin EOF | 调度 `app.quit()` | `0` |
| reader 线程退出等待超时 | 记录告警，避免无限阻塞 | `0`（主流程成功） |
| 输入非法 JSON 行 | 按既有协议输出 `error(1000)` 并继续循环 | `0`（进程不退出） |

### 3.10 向后兼容

- **不变**:
  - `run()` 和 `run(int, char**)` 签名不变
  - `DriverCore` 头文件公有接口不变，新增成员全部为 private
  - `--profile=oneshot/keepalive` 参数语义
  - JSONL 请求/响应结构
  - `processOneLine` 的校验与错误码行为
- **变化（目标行为）**:
  - Stdio KeepAlive 模式在空闲期仍可分发异步 Qt 事件（这是本次修复的目标行为）
- **兼容性变更（需注意）**:
  - `run()` 无参版本新增前置检查：若 `QCoreApplication::instance()` 为空则返回 1（原实现不检查，直接进入阻塞循环）。现有所有调用方（`test_driver_main.cpp` 及全部 driver `main()`）均已在 `main()` 中创建 `QCoreApplication`，因此实际不影响。但若存在未经 `QCoreApplication` 初始化直接调用 `run()` 的遗留代码，将产生行为变化。
- 现有驱动无需任何代码改动即可获得异步事件循环支持

---

## 4. 实现步骤（TDD Red-Green-Refactor）

### 4.1 Red — 编写失败测试

#### 4.1.1 辅助 Driver 扩展 (`test_driver_main.cpp`)

需要在 `TestHandler` 中新增三个命令以支持异步测试场景：

```cpp
// test_driver_main.cpp — TestHandler::handle() 新增分支

} else if (cmd == "timer_echo") {
    // 异步测试命令：启动 QTimer::singleShot，在定时器回调中发送 done
    // 此命令在改造前的阻塞循环下无法触发定时器回调
    int delayMs = data.toObject()["delay"].toInt(100);
    auto* resp = new StdioResponder();  // 堆分配，定时器回调后 delete
    QTimer::singleShot(delayMs, qApp, [resp, data]() {
        resp->done(0, QJsonObject{{"timer_fired", true}, {"delay", data.toObject()["delay"].toInt(100)}});
        delete resp;
    });
} else if (cmd == "async_event_once") {
    // 异步事件测试命令：立即返回 done，随后通过 QTimer 发射一个异步 event
    // 用于验证在无后续 stdin 输入时，异步事件仍可被主线程事件循环分发
    r.done(0, QJsonObject{{"scheduled", true}});
    auto* evtResp = new StdioResponder();
    QTimer::singleShot(200, qApp, [evtResp]() {
        evtResp->event(QStringLiteral("tick"), 0,
                       QJsonObject{{"source", "async_event_once"}});
        delete evtResp;
    });
} else if (cmd == "noop") {
    // 空操作，不回复任何响应
    // 用于 OneShot 多行测试中的第二条命令
}
```

> **重要**：`timer_echo` 和 `async_event_once` 均捕获堆分配的 `StdioResponder` 而非引用栈上的 responder，因为 `handle()` 返回后原 responder 生命周期结束。

#### 4.1.2 新增测试文件 (`test_driver_core_async.cpp`)

每条测试在修复前必须运行并确认失败（**Red**）。

| 测试 ID | 缺陷/场景 | 输入 | 当前错误行为 | 期望正确行为 |
|---------|-----------|------|-------------|-------------|
| `R01` | 主线程事件循环不可用 | KeepAlive 下发送 `timer_echo`，等待定时器触发 | 定时器永不触发（主线程阻塞在 readLine） | 定时器正常触发，收到 `done` 响应 |
| `R02` | KeepAlive 下 EOF 不退出 | KeepAlive 下发送命令后关闭 stdin | 进程可能挂死（取决于 readLine EOF 处理） | 进程在超时阈值内正常退出 |
| `R03` | OneShot 仅处理首命令 | stdin 连续写入两条命令 | 同上（验证不回归） | 只处理第一条，第二条被丢弃 |
| `R04` | OneShot 退出不挂死 | 发送一条命令后等待退出 | 可能卡在 readLine（stdin 未关闭） | 进程在超时阈值内退出 |
| `R05` | 空行不触发命令处理 | KeepAlive 下先写空行再写合法 echo | 需验证不回归 | 仅合法命令产生响应 |
| `R06` | 非法 JSON 错误码稳定 | KeepAlive 下先写非法 JSON 再写合法 echo | 需验证不回归 | 先返回 `error(1000)`，随后仍可处理合法命令 |
| `R07` | 无后续 stdin 时异步事件仍触发 | KeepAlive 下发送 `async_event_once` 后不再写入 | 事件不触发（主线程阻塞） | 定时器触发后收到 `event("tick")` |
| `R08` | OneShot 尾随行被忽略 | OneShot 下第一条 echo + 第二条 progress | 需验证不回归 | stdout 中不存在 progress 事件行 |

```cpp
// src/tests/test_driver_core_async.cpp

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <gtest/gtest.h>
#include "stdiolink/platform/platform_utils.h"

using namespace stdiolink;

static const QString TestDriver =
    PlatformUtils::executablePath(".", "test_driver");

class DriverCoreAsyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_driverPath = QCoreApplication::applicationDirPath() + "/" + TestDriver;
    }

    // 辅助方法：启动 driver 进程并设置 profile
    QProcess* startDriver(const QString& profile) {
        auto* proc = new QProcess();
        proc->setProgram(m_driverPath);
        proc->setArguments({"--profile=" + profile});
        proc->start();
        EXPECT_TRUE(proc->waitForStarted(5000))
            << "Failed to start test_driver";
        return proc;
    }

    // 辅助方法：向进程写入 JSONL 命令
    void writeCommand(QProcess* proc, const QString& cmd,
                      const QJsonObject& data = {}) {
        QJsonObject req{{"cmd", cmd}};
        if (!data.isEmpty()) req["data"] = data;
        QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n";
        proc->write(line);
        proc->waitForBytesWritten(1000);
    }

    // 辅助方法：读取一行 JSONL 响应（带超时）
    bool readResponse(QProcess* proc, QJsonObject& response, int timeoutMs = 5000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (proc->canReadLine()) {
                QByteArray line = proc->readLine().trimmed();
                if (line.isEmpty()) continue;
                QJsonDocument doc = QJsonDocument::fromJson(line);
                if (!doc.isNull() && doc.isObject()) {
                    response = doc.object();
                    return true;
                }
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        return false;
    }

    QString m_driverPath;
};

// R01 — 主线程事件循环可运行（KeepAlive + QTimer 回调）
// 缺陷：当前 runStdioMode() 阻塞主线程，QTimer::singleShot 永不触发
TEST_F(DriverCoreAsyncTest, R01_KeepAlive_TimerCallbackFires) {
    auto* proc = startDriver("keepalive");

    // 发送 timer_echo 命令（延迟 100ms 后由定时器回复）
    writeCommand(proc, "timer_echo", {{"delay", 100}});

    // 等待响应：改造前定时器不触发，此处超时失败
    QJsonObject resp;
    bool got = readResponse(proc, resp, 3000);
    EXPECT_TRUE(got) << "timer_echo should receive done response via QTimer callback";
    if (got) {
        EXPECT_EQ(resp["status"].toString(), "done");
        EXPECT_TRUE(resp["data"].toObject()["timer_fired"].toBool());
    }

    // 清理：关闭 stdin 触发 EOF 退出
    proc->closeWriteChannel();
    EXPECT_TRUE(proc->waitForFinished(5000));
    delete proc;
}

// R02 — KeepAlive 下 EOF 正常退出
// 缺陷：验证改造后 EOF 信号正确传递到主线程并触发 quit
TEST_F(DriverCoreAsyncTest, R02_KeepAlive_EOF_Exit) {
    auto* proc = startDriver("keepalive");

    // 发送一条普通命令验证连通性
    writeCommand(proc, "echo", {{"msg", "hello"}});
    QJsonObject resp;
    EXPECT_TRUE(readResponse(proc, resp, 3000));

    // 关闭 stdin（发送 EOF）
    proc->closeWriteChannel();

    // 验证进程在合理时间内退出
    QElapsedTimer timer;
    timer.start();
    EXPECT_TRUE(proc->waitForFinished(5000))
        << "Driver should exit after stdin EOF";
    EXPECT_LT(timer.elapsed(), 4000)
        << "Driver should exit promptly after EOF, not hang";
    EXPECT_EQ(proc->exitCode(), 0);
    delete proc;
}

// R03 — OneShot 仅处理首条命令
// 验证 m_stdioAcceptLines 截断逻辑
// 注意：使用单次 write 将两条命令拼接写入，确保两条命令在 reader 线程
// 可见范围内（避免第二条命令因进程退出 pipe 断裂而未入队的假阳性）
TEST_F(DriverCoreAsyncTest, R03_OneShot_OnlyFirstCommand) {
    auto* proc = startDriver("oneshot");

    // 拼接两条命令为一次写入，确保 reader 线程可读到两行
    QJsonObject req1{{"cmd", "echo"}, {"data", QJsonObject{{"msg", "first"}}}};
    QJsonObject req2{{"cmd", "echo"}, {"data", QJsonObject{{"msg", "second"}}}};
    QByteArray batch = QJsonDocument(req1).toJson(QJsonDocument::Compact) + "\n"
                     + QJsonDocument(req2).toJson(QJsonDocument::Compact) + "\n";
    proc->write(batch);
    proc->waitForBytesWritten(1000);

    // 等待进程退出
    EXPECT_TRUE(proc->waitForFinished(5000));

    // 读取所有输出
    QByteArray allOutput = proc->readAllStandardOutput();
    QList<QByteArray> lines = allOutput.split('\n');

    // 统计 done 响应数量：应该只有 1 个
    int doneCount = 0;
    for (const auto& line : lines) {
        if (line.trimmed().isEmpty()) continue;
        QJsonDocument doc = QJsonDocument::fromJson(line.trimmed());
        if (doc.isObject() && doc.object()["status"].toString() == "done") {
            doneCount++;
            // 验证是第一条命令的响应
            QJsonObject data = doc.object()["data"].toObject();
            EXPECT_EQ(data["msg"].toString(), "first");
        }
    }
    EXPECT_EQ(doneCount, 1) << "OneShot should only process the first command";
    delete proc;
}

// R04 — OneShot 退出不挂死
// 缺陷：改造后需要打断 readLine() 阻塞；验证进程在合理时间内退出
TEST_F(DriverCoreAsyncTest, R04_OneShot_ExitNotHang) {
    auto* proc = startDriver("oneshot");

    QElapsedTimer timer;
    timer.start();

    // 发送一条命令触发 OneShot 退出
    writeCommand(proc, "echo", {{"msg", "bye"}});

    // 验证进程在超时阈值内退出
    EXPECT_TRUE(proc->waitForFinished(5000))
        << "OneShot driver should exit after processing one command";
    EXPECT_LT(timer.elapsed(), 4000)
        << "OneShot exit should not hang waiting for stdin reader";
    EXPECT_EQ(proc->exitCode(), 0);
    delete proc;
}

// R05 — 空行不触发命令处理
TEST_F(DriverCoreAsyncTest, R05_EmptyLineIgnored) {
    auto* proc = startDriver("keepalive");

    // 先写空行，再写合法命令
    proc->write("\n");
    proc->waitForBytesWritten(1000);
    writeCommand(proc, "echo", {{"msg", "after_empty"}});

    // 应仅收到 echo 的 done 响应
    QJsonObject resp;
    EXPECT_TRUE(readResponse(proc, resp, 3000));
    EXPECT_EQ(resp["status"].toString(), "done");
    EXPECT_EQ(resp["data"].toObject()["msg"].toString(), "after_empty");

    proc->closeWriteChannel();
    EXPECT_TRUE(proc->waitForFinished(5000));
    delete proc;
}

// R06 — 非法 JSON 错误码稳定
TEST_F(DriverCoreAsyncTest, R06_InvalidJsonErrorCode) {
    auto* proc = startDriver("keepalive");

    // 先写非法 JSON
    proc->write("{bad json}\n");
    proc->waitForBytesWritten(1000);

    // 应返回 error(1000)
    QJsonObject resp1;
    EXPECT_TRUE(readResponse(proc, resp1, 3000));
    EXPECT_EQ(resp1["status"].toString(), "error");
    EXPECT_EQ(resp1["code"].toInt(), 1000);

    // 随后仍可处理合法命令
    writeCommand(proc, "echo", {{"msg", "recovered"}});
    QJsonObject resp2;
    EXPECT_TRUE(readResponse(proc, resp2, 3000));
    EXPECT_EQ(resp2["status"].toString(), "done");

    proc->closeWriteChannel();
    EXPECT_TRUE(proc->waitForFinished(5000));
    delete proc;
}

// R07 — 无后续 stdin 时异步事件仍触发
// 缺陷：验证在 idle stdin 期间 QTimer 回调仍可派发事件
TEST_F(DriverCoreAsyncTest, R07_AsyncEventWithoutFurtherStdin) {
    auto* proc = startDriver("keepalive");

    // 发送 async_event_once 命令
    writeCommand(proc, "async_event_once");

    // 先收到 done 响应
    QJsonObject respDone;
    EXPECT_TRUE(readResponse(proc, respDone, 3000));
    EXPECT_EQ(respDone["status"].toString(), "done");

    // 不再写入任何 stdin，等待异步 event 输出
    QJsonObject respEvent;
    bool got = readResponse(proc, respEvent, 3000);
    EXPECT_TRUE(got) << "Should receive async event without further stdin input";
    if (got) {
        EXPECT_EQ(respEvent["status"].toString(), "event");
        EXPECT_EQ(respEvent["event"].toString(), "tick");
    }

    proc->closeWriteChannel();
    EXPECT_TRUE(proc->waitForFinished(5000));
    delete proc;
}

// R08 — OneShot 尾随行被忽略（不出现第二条命令的事件）
// 注意：同 R03，使用单次 write 拼接确保第二条命令入队
TEST_F(DriverCoreAsyncTest, R08_OneShot_TrailingCommandIgnored) {
    auto* proc = startDriver("oneshot");

    // 拼接两条命令为一次写入
    QJsonObject req1{{"cmd", "echo"}, {"data", QJsonObject{{"msg", "first"}}}};
    QJsonObject req2{{"cmd", "progress"}, {"data", QJsonObject{{"steps", 3}}}};
    QByteArray batch = QJsonDocument(req1).toJson(QJsonDocument::Compact) + "\n"
                     + QJsonDocument(req2).toJson(QJsonDocument::Compact) + "\n";
    proc->write(batch);
    proc->waitForBytesWritten(1000);

    EXPECT_TRUE(proc->waitForFinished(5000));

    QByteArray allOutput = proc->readAllStandardOutput();
    // 不应出现 progress 命令的 step 事件
    EXPECT_FALSE(allOutput.contains("\"step\""))
        << "OneShot should not process trailing progress command";
    delete proc;
}
```

**Red 阶段确认**：在提交任何修复代码前，运行上述全部用例。

- `R01`/`R07` 预期失败原因：当前 `runStdioMode()` 在主线程 `readLine()` 循环中阻塞，`QTimer::singleShot` 回调永不触发，`readResponse` 超时返回 false。
- `R02`~`R06`/`R08` 预期行为：现有实现下 `readLine()` 阻塞循环在 EOF 时可退出循环（`QTextStream::atEnd()`），非法 JSON 走既有 `processOneLine` 错误路径。这些用例作为回归保护，确保改造后语义不变。

运行失败测试命令：

```powershell
.\build\runtime_debug\bin\stdiolink_tests.exe --gtest_filter="DriverCoreAsyncTest.*"
```

---

### 4.2 Green — 最小修复实现

#### 改动 1：`src/stdiolink/driver/driver_core.h`

新增私有成员和辅助方法声明：

```cpp
// driver_core.h — private 区域新增
#include <atomic>
#include <QThread>

// 在 private: 区域新增以下成员
QThread* m_stdinReaderThread = nullptr;
std::atomic_bool m_stdioStopRequested{false};
std::atomic_bool m_stdioAcceptLines{true};
bool m_stdioQuitScheduled = false;

void handleStdioLineOnMainThread(const QByteArray& line);
void scheduleStdioQuit();
```

**改动理由**：支持读线程与主线程的状态协调，实现 OneShot 截断和幂等退出。

**验收方式**：编译通过，R01 由 Red 变为 Green。

---

#### 改动 2：`src/stdiolink/driver/driver_core.cpp`

分为三个子步骤：

**步骤 2-a：改造 `runStdioMode()`**

替换 `DriverCore::runStdioMode()` 整体实现（代码见 §3.4 After 部分）。内部使用 `QThread::create()` 创建读线程，通过 `QMetaObject::invokeMethod()` + `Qt::QueuedConnection` 投递行数据到主线程，无需 `Q_OBJECT` 或 `.moc` 文件。

**步骤 2-b：新增 `handleStdioLineOnMainThread()`**

实现主线程命令处理槽（代码见 §3.5）。

**步骤 2-c：新增 `scheduleStdioQuit()`**

实现幂等退出调度（代码见 §3.6）。

**改动理由**：将 stdin 阻塞读取移至独立线程，主线程进入 Qt 事件循环。

**验收方式**：R01~R04 全部由 Red 变为 Green。

---

#### 改动 3：`src/tests/test_driver_main.cpp`

扩展 `TestHandler::handle()` 以支持 `timer_echo`、`async_event_once` 和 `noop` 三个命令（代码见 §4.1.1）。

新增头文件引用：

```cpp
#include <QTimer>
#include "stdiolink/driver/stdio_responder.h"
```

**改动理由**：提供异步测试命令，使 R01 可验证定时器回调、R07 可验证 idle stdin 下异步事件派发。

**验收方式**：R01 中 `timer_echo`、R07 中 `async_event_once` 命令可正常触发定时器回调/事件发射。

---

#### 改动 4：`src/tests/CMakeLists.txt`

在 `TEST_SOURCES` 列表中新增 `test_driver_core_async.cpp`：

```cmake
set(TEST_SOURCES
    # ... 现有文件 ...
    test_driver_core_async.cpp    # 新增
)
```

**改动理由**：注册新测试文件到测试套件。

**验收方式**：`stdiolink_tests` 编译通过，新增用例可执行。

---

**Green 阶段确认**：完成全部改动后运行全量测试：

```powershell
.\build.bat
.\build\runtime_debug\bin\stdiolink_tests.exe
```

确认 `R01`–`R08` 全部通过（Green），现有测试套件无回归。

---

### 4.3 Refactor — 代码整理

1. **`runStdioMode()` 旧实现清理**：删除旧 `QFile input / QTextStream in / while` 循环代码，确认无残留。

2. **`run()` 无参版本处理**：当前 `run()` 直接调用 `runStdioMode()`，不创建 `QCoreApplication`。改造后 `runStdioMode()` 需要 `QCoreApplication::instance()`，因此 `run()` 在 `instance()` 不存在时返回错误码 1。验证现有调用方均已在 `main()` 中创建 `QCoreApplication`。

3. **`#include` 整理**：`driver_core.cpp` 中 `QFile`、`QTextStream` 仍被其他方法使用（`printHelp` 等），不应删除。新增 `#include <QThread>` 和 `#include <QTimer>`（若 warning 辅助方法需要）。

4. **确认无死代码**：检查 `m_stdioStopRequested/m_stdioAcceptLines/m_stdioQuitScheduled` 是否全部有读写使用点，无未使用变量。

重构后全量测试（R01–R08 + 原有套件）必须仍全部通过。

**若无重复代码或结构破坏需要整理，写明「无需重构」。**

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/tests/test_driver_core_async.cpp` — DriverCore 异步改造单元测试/集成测试

### 5.2 修改文件

| 文件 | 改动内容 |
|------|---------|
| `src/stdiolink/driver/driver_core.h` | 新增私有成员 `m_stdinReaderThread`/`m_stdioStopRequested`/`m_stdioAcceptLines`/`m_stdioQuitScheduled`，新增私有方法 `handleStdioLineOnMainThread()`/`scheduleStdioQuit()` |
| `src/stdiolink/driver/driver_core.cpp` | 改造 `runStdioMode()` 为 `QThread::create()` 读线程 + `app->exec()` 事件循环模型，新增 `handleStdioLineOnMainThread()` 和 `scheduleStdioQuit()` 实现，无需 Q_OBJECT/.moc |
| `src/tests/test_driver_main.cpp` | `TestHandler` 新增 `timer_echo`/`async_event_once`/`noop` 命令支持 |
| `src/tests/CMakeLists.txt` | `TEST_SOURCES` 新增 `test_driver_core_async.cpp` |

### 5.3 测试文件

- `src/tests/test_driver_core_async.cpp` — 8 条集成测试用例 (R01–R08)
- `src/tests/test_host_driver.cpp` — 若改造导致既有 Host↔Driver 测试出现 FAIL，则补充 keepalive 异步回归断言
- `src/tests/test_modbustcp_server_handler.cpp` — 保留作为 server 行为基线（不改核心断言）

---

## 6. 测试与验收

### 6.1 单元测试（必填，重点）

- **测试对象**: `DriverCore::runStdioMode()` 改造后的异步行为
- **测试方式**: 进程级集成测试（通过 `QProcess` 启动 `test_driver` 辅助进程，模拟 Host-Driver 通信）
- **用例分层**: 正常路径（R01 异步回调、R02 EOF 退出、R07 异步事件）、边界值（R03/R08 OneShot 多行截断、R05 空行过滤）、异常输入（R06 非法 JSON 错误码）、错误传播（R06 error 1000 保持稳定）、兼容回归（R03/R04/R05/R06/R08 行为不变）
- **桩替身策略**: 使用 `test_driver` 辅助进程作为真实 Driver 替身，内嵌 `TestHandler` 测试处理器。不使用 mock — 本次改造的核心价值在于进程级事件循环行为，单元级 mock 无法覆盖。

#### 路径矩阵（必填）

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `runStdioMode`: `m_handler == nullptr` | 返回 1 | — 不可达于本测试套件：`test_driver_main.cpp` 始终设置 handler。注意 `test_driver_core.cpp` 仅测试 `MockResponder`/`EchoHandler` 单元行为，不覆盖 `runStdioMode()` 分支。此为防御式分支，通过代码审查确认。 |
| `runStdioMode`: `QCoreApplication::instance() == nullptr` | 返回 1，输出错误 | — 不可达于本测试套件：`test_driver_main.cpp` 的 `main()` 始终创建 `QCoreApplication`。防御式分支，通过代码审查确认。 |
| `runStdioMode`: 正常启动 → reader 线程运行 → 主线程处理 | KeepAlive 下异步功能可用 | R01 |
| `handleStdioLineOnMainThread`: `m_stdioAcceptLines == false` | 丢弃行 | R03, R08（拼接写入确保第二条命令已入队后被丢弃） |
| `handleStdioLineOnMainThread`: 空行 | 跳过 | R05（空行写入后仅后续合法命令产生响应） |
| `handleStdioLineOnMainThread`: `m_profile == OneShot` → `scheduleStdioQuit` | 设置 `m_stdioAcceptLines=false`，调度退出 | R03, R04, R08 |
| `handleStdioLineOnMainThread`: `m_profile == KeepAlive` | 继续接受后续行 | R01, R07 |
| `processOneLine`: 非法 JSON | 输出 `error(1000)` 并继续 | R06 |
| `eofReached` → `scheduleStdioQuit` | 设置 `m_stdioStopRequested=true`，调度 `quit` | R02 |
| `scheduleStdioQuit`: `m_stdioQuitScheduled == true` | 幂等返回 | R03（OneShot 下可能 EOF 和 OneShot 退出同时触发） |
| `scheduleStdioQuit`: `m_stdioQuitScheduled == false` | 调度 `app->quit()` | R02, R04 |
| 异步事件调度能力 | 无后续 stdin 时仍触发 `QTimer` 事件 | R07 |
| 退出阶段: `thread->wait(2000)` 成功 | 正常退出，`delete thread` | R02, R04 |
| 退出阶段: `thread->wait(2000)` 超时 | `terminate()` + `delete`（兜底） | — 极端场景。设计上 fd 关闭后 readLine 必返回，超时仅为防御。因依赖 OS 行为无法在单元测试中稳定复现，通过代码审查 + 退出阶段日志监控确认实现正确。 |

覆盖要求：所有可达且可稳定复现路径 100% 有用例；不可达路径给出设计级原因；极端环境依赖路径（如线程超时）通过代码审查 + 运行时日志监控确认。

#### 用例详情（必填）

**R01 — KeepAlive 下 QTimer 回调正常触发**
- 前置条件: `test_driver` 可执行文件已构建，`TestHandler` 支持 `timer_echo` 命令
- 输入: 以 `--profile=keepalive` 启动 `test_driver`，通过 stdin 写入 `{"cmd":"timer_echo","data":{"delay":100}}`
- 预期: 100ms 后 `QTimer::singleShot` 回调触发，`test_driver` 通过 stdout 输出 `{"status":"done","code":0,"data":{"timer_fired":true,"delay":100}}`
- 断言: `readResponse` 在 3000ms 内返回 true；响应 `status == "done"`；`data.timer_fired == true`

**R02 — KeepAlive 下 EOF 正常退出**
- 前置条件: `test_driver` 以 `--profile=keepalive` 启动
- 输入: 先发送 `echo` 命令（验证连通性），然后关闭 stdin（`proc->closeWriteChannel()`）
- 预期: 进程在 5 秒内正常退出，退出码 0
- 断言: `proc->waitForFinished(5000) == true`；`timer.elapsed() < 4000`；`proc->exitCode() == 0`

**R03 — OneShot 仅处理首条命令**
- 前置条件: `test_driver` 以 `--profile=oneshot` 启动
- 输入: 通过 stdin **单次 write 拼接**两条 `echo` 命令（`msg: "first"` 和 `msg: "second"`），确保两条命令在 reader 线程可见范围内入队
- 预期: stdout 中只有一个 `done` 响应，且 `data.msg == "first"`
- 断言: `doneCount == 1`；响应数据中 `msg == "first"`

**R04 — OneShot 退出不挂死**
- 前置条件: `test_driver` 以 `--profile=oneshot` 启动
- 输入: 发送一条 `echo` 命令
- 预期: 进程处理命令后在 5 秒内退出，退出码 0
- 断言: `proc->waitForFinished(5000) == true`；`timer.elapsed() < 4000`；`proc->exitCode() == 0`

**R05 — 空行不触发命令处理**
- 前置条件: `test_driver` 以 `--profile=keepalive` 启动
- 输入: 写入 `"\n"` 再写入合法 `echo` 命令
- 预期: 仅合法命令产生响应
- 断言: 第一条响应为 `done` 且 `data.msg == "after_empty"`

**R06 — 非法 JSON 错误码稳定**
- 前置条件: `test_driver` 以 `--profile=keepalive` 启动
- 输入: `"{bad json}\n"` 后再写合法 `echo`
- 预期: 先返回 `error(1000)`，随后仍可处理 `echo`
- 断言: 第一条 `status=="error"` 且 `code==1000`；第二条 `status=="done"`

**R07 — 无后续 stdin 时异步事件仍触发**
- 前置条件: `test_driver` 以 `--profile=keepalive` 启动
- 输入: 仅发送 `async_event_once` 一次命令，不再写任何 stdin
- 预期: 先收到 `done`，随后定时器触发 `event("tick")`
- 断言: 在 3000ms 内收到 `status=="event"` 且 `event=="tick"`

**R08 — OneShot 尾随行被忽略**
- 前置条件: `test_driver` 以 `--profile=oneshot` 启动
- 输入: 第一条 `echo`，第二条 `progress`
- 预期: 不出现 `progress` 事件
- 断言: stdout 中不存在 `"step"` 关键字

#### 测试代码（必填）

完整测试代码见 §4.1.2。

### 6.2 集成/端到端测试（涉及跨模块交互时必填）

- **现有回归测试**: 运行 `stdiolink_tests` 全量，重点关注：
  - `test_host_driver.cpp` — 通过 `test_driver` 进程测试 Host↔Driver 通信
  - `test_driver_core.cpp` — MockResponder / EchoHandler 单元测试
  - `test_wait_any.cpp` — 并发等待测试
  - `test_driverlab_ws_handler.cpp` — DriverLab WebSocket 处理器测试
- **DriverLab keepalive 链路**: 连接后执行异步命令，验证在 idle stdin 期间仍有事件上行
- **Server 管道链路**: `driverlab_ws_connection → driver stdin → start_server` 后外部 TCP 连通性
- **手动联调验收** (改造完成后由用户执行):
  1. 启动 `stdiolink_server`，通过 WebUI DriverLab 连接 `stdio.drv.modbustcp_server`
  2. 在 DriverLab 中发送 `start_server` 命令
  3. 从外部 Modbus TCP 客户端（如 `qModMaster` 或脚本）连接 server
  4. 验证可正常读写寄存器

### 6.3 验收标准

- [ ] `DriverCore::runStdioMode()` 主线程运行 `app.exec()`，stdin 读取在独立线程 (R01)
- [ ] `--profile=keepalive` 下 `QTimer::singleShot` 正常触发 (R01)
- [ ] `--profile=keepalive` 下 stdin EOF 后进程正常退出 (R02)
- [ ] `--profile=oneshot` 仅处理首条命令 (R03)
- [ ] `--profile=oneshot` 退出不挂死 (R04)
- [ ] 空行不触发命令处理 (R05)
- [ ] 非法 JSON 输入返回 `error(1000)` 且进程不退出 (R06)
- [ ] 无后续 stdin 时异步事件仍可分发 (R07)
- [ ] OneShot 尾随行被忽略（不出现第二条命令的 event） (R08)
- [ ] `stdiolink_tests` 全量通过，无回归
- [ ] OneShot 与 KeepAlive 行为与文档定义一致
- [ ] 无新增协议字段或错误码变更

---

## 7. 风险与控制

- **风险 R1**: 跨线程关闭 stdin fd 后 `readLine()` 在特定 OS/管道配置下行为不一致
  - 控制: 使用 fd 级关闭（`_close/_fileno` / `close/fileno`）而非 `fclose`，避免 CRT `FILE*` 层的跨线程 UB
  - 控制: fd 关闭后 readLine 应返回 null/空，但万一不返回，有 `thread->terminate()` 兜底
  - 控制: CI 在 Windows（MSVC + MinGW）和 Linux 环境下分别运行 R02/R04 验证
  - 测试覆盖: R02, R04 覆盖退出路径

- **风险 R2**: QThread 堆对象生命周期管理不当导致内存泄漏或重复释放
  - 控制: `runStdioMode()` 结尾严格执行 `wait → (timeout → terminate → wait) → delete → nullptr`
  - 控制: `m_stdinReaderThread` 初始化为 nullptr，仅在 `runStdioMode()` 内赋值，函数退出前清理
  - 测试覆盖: R02, R04 覆盖正常退出路径

- **风险 R3**: OneShot 下 reader 线程多读行导致额外命令执行
  - 控制: `m_stdioAcceptLines` 原子变量在主线程处理首条命令后立即置 false
  - 控制: `handleStdioLineOnMainThread()` 入口处检查该标志，丢弃后续行
  - 控制: R03/R08 使用单次 write 拼接两条命令，确保第二条命令已入 reader 线程缓冲区后被丢弃（避免假阳性）
  - 测试覆盖: R03, R08

- **风险 R4**: reader 线程退出同步不当导致进程退出卡住
  - 控制: `thread->wait(2000)` 带 2 秒超时
  - 控制: 超时后 `terminate()` 兜底，避免 delete 运行中 QThread 导致崩溃
  - 测试覆盖: R04 验证 OneShot 退出不挂死

- **风险 R5**: 现有驱动可能依赖 `readLine` 阻塞副作用（如在 `handle()` 中同步等待）
  - 控制: 回归测试 — `stdiolink_tests` 全量运行覆盖所有已有行为
  - 控制: `processOneLine()` 实现完全不变，仅调用位置从循环内改为信号槽
  - 测试覆盖: 全量回归

- **风险 R6**: 非法 JSON 处理语义被破坏（错误码 1000 不稳定）
  - 控制: 复用 `processOneLine` 既有错误路径（error code 1000），不做任何修改
  - 控制: 兼容性回归测试锁定错误码
  - 测试覆盖: R06

- **风险 R7**: 改造影响 DriverLab/Host 既有交互时序
  - 控制: 保持 `processOneLine` 串行，不引入并发处理
  - 控制: 跑 `test_host_driver`、`test_wait_any`、`test_driverlab_ws_handler` 全量回归
  - 测试覆盖: R01, R07, 集成回归

---

## 8. 里程碑完成定义（DoD）

- [ ] 代码改造完成：DriverCore Stdio 模式实现「读线程阻塞读 + 主线程事件循环处理」
- [ ] 单元测试完成：新增 `test_driver_core_async.cpp` 且 R01–R08 全通过
- [ ] 既有测试无回归：`stdiolink_tests` 全量通过
- [ ] 文档同步完成（本文件 + `drivercore_async_fix_plan.md` 无矛盾）
- [ ] 向后兼容确认：`run()/run(argc,argv)` 签名不变，`Profile`/`RunMode` 枚举不变，协议/错误码无破坏性变更
- [ ] 【问题修复类】Red 阶段失败测试有执行记录（R01/R07 在修复前确认失败）
- [ ] 【问题修复类】Green 阶段全量测试通过（R01–R08 + 既有无回归）
- [ ] 【问题修复类】Refactor 阶段（若有）全量测试仍通过
