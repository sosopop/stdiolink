# stdiolink_server 主线程阻塞消除方案（最终版 v2）

## 1. 背景

stdiolink_server 采用单线程 Qt 事件循环架构，所有 HTTP/SSE/WebSocket 请求、定时调度、进程管理均在主线程执行。多处 `waitForStarted()` / `waitForFinished()` 阻塞调用会冻结整个服务器，影响所有客户端响应。

本方案目标：在不破坏主线程状态一致性的前提下，消除所有可避免的阻塞。

## 2. 阻塞点清单

| 位置 | 阻塞调用 | 最大阻塞时长 |
|------|----------|-------------|
| `instance_manager.cpp:169` | `proc->waitForStarted(5000)` | 5s |
| `instance_manager.cpp:195` | `proc->waitForFinished(1000)` | 1s |
| `driverlab_ws_connection.cpp:68` | `m_process->waitForStarted(5000)` | 5s |
| `driver_manager_scanner.cpp:97` | `proc.waitForFinished(10000)` | 10s × N 个 driver |
| `process_guard_server.cpp:31` | `probe.waitForConnected(200)` | 200ms |

## 3. 设计决策记录

| 决策项 | 选定方案 | 理由 |
|--------|---------|------|
| 终止策略 | 直接 `kill()` | service 进程无复杂退出清理逻辑，简单可靠 |
| 扫描异步化 | QtConcurrent + QFuture | 轻量无新模块，当前仅 driver scan 需要 |
| 启动响应码 | `200 OK` + `status:"starting"` | 兼容现有前端，不引入 breaking change |
| service scan | 保持同步 | 仅读本地小 JSON 文件，亚毫秒级，不值得异步化 |
| Guard probe | UUID 名称跳过探测 | UUID 冲突概率可忽略，仅测试 override 保留探测 |
| 中间状态 | 只加 `starting` | `kill()` 后 `finished` 信号极快到达，`stopping` 无实际观测价值 |
| SSE 事件 | 新增 `instance.startFailed` | 前端实时感知启动失败，配合异步启动必需 |
| Qt 版本要求 | ≥ 6.6（实际 6.10.0） | QHttpServer 返回 QFuture 需 6.4+，makeReadyValueFuture 需 6.6+ |
| 实施节奏 | 一次性全部实施 | 5 个改造点一个 PR 提交 |

---

## 4. 改造方案

### 4.1 InstanceManager::terminateInstance() — 移除 waitForFinished

**修改文件**: `src/stdiolink_server/manager/instance_manager.cpp`

删除 `kill()` 后的 `waitForFinished(1000)`。`QProcess::finished` 信号已连接到 `onProcessFinished()` 槽，会异步完成清理（状态更新、`deleteLater`、从 map 移除）。

```cpp
void InstanceManager::terminateInstance(const QString& instanceId) {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) return;
    QProcess* proc = it->second->process;
    if (!proc || proc->state() == QProcess::NotRunning) return;

    proc->kill();
    // 移除: proc->waitForFinished(1000);
    // finished 信号触发 onProcessFinished() 完成清理
}
```

**级联影响**: `terminateByProject()`、`terminateAll()` 改造后立即返回。`waitAllFinished()` 的 shutdown 路径已有 `processEvents` 循环，不受影响。

---

### 4.2 InstanceManager::startInstance() — 信号驱动异步启动

**修改文件**:
- `src/stdiolink_server/manager/instance_manager.h`
- `src/stdiolink_server/manager/instance_manager.cpp`
- `src/stdiolink_server/server_manager.cpp`
- `src/stdiolink_server/http/api_router.cpp`

将 `waitForStarted(5000)` 替换为 `QProcess::started` / `errorOccurred` 信号驱动。实例以 `"starting"` 状态立即入 map，HTTP handler 立即返回。

**新增信号** (`instance_manager.h`):

```cpp
signals:
    void instanceStarted(const QString& instanceId,
                         const QString& projectId);          // 已有
    void instanceStartFailed(const QString& instanceId,
                             const QString& projectId,
                             const QString& error);          // 新增
    void instanceFinished(const QString& instanceId,
                          const QString& projectId,
                          int exitCode,
                          QProcess::ExitStatus exitStatus);  // 已有
```

**核心改动** (`instance_manager.cpp`):

```cpp
// ⚠ 关键：先入 map，再 start()。
// errorOccurred(FailedToStart) 可能从 start() 内部同步触发，
// 若 emplace 在 start() 之后，lambda 中 find(instanceId) 会找不到实例，
// 导致状态迁移丢失、实例永远卡在 starting。
m_instances.emplace(instanceId, std::move(inst));

// 在 proc->start() 之前注册信号
connect(proc, &QProcess::started, this, [this, instanceId]() {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) return;
    Instance* inst = it->second.get();
    inst->pid = inst->process->processId();
    inst->status = "running";
    emit instanceStarted(instanceId, inst->projectId);
});

connect(proc, &QProcess::errorOccurred, this, [this, instanceId](QProcess::ProcessError err) {
    if (err != QProcess::FailedToStart) return;
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) return;
    Instance* inst = it->second.get();
    const QString projectId = inst->projectId;
    inst->status = "failed";
    inst->startFailedEmitted = true;  // 去重标记
    emit instanceStartFailed(instanceId, projectId, inst->process->errorString());
    // FailedToStart 时 Qt 不会发射 finished 信号，手动补发以维持"始终发射"不变量
    emit instanceFinished(instanceId, projectId, -1, QProcess::CrashExit);
    inst->process->deleteLater();
    inst->process = nullptr;
    m_instances.erase(it);
});

proc->start();
// 移除: waitForStarted(5000) 及后续同步赋值

// 安全超时（替代原来的 5s 阻塞等待）
QTimer::singleShot(5000, this, [this, instanceId]() {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) return;
    if (it->second->status == "starting") {
        // 超时未启动，发 startFailed 事件后 kill
        it->second->startFailedEmitted = true;  // 去重标记
        emit instanceStartFailed(instanceId, it->second->projectId,
                                 QStringLiteral("start timeout (5s)"));
        it->second->process->kill();
        // kill 触发 finished → onProcessFinished 完成清理
    }
});

return instanceId;  // 立即返回，状态为 "starting"
```

**onProcessFinished 适配**：

设计原则：
- `instanceFinished` 始终发射 — 进程确实退出了，这是事实。ScheduleEngine 依赖此信号做 daemon 重启和失败计数，不能断链。
- `instanceStartFailed` 在 starting 阶段额外发射 — 给前端/SSE 提供明确的"启动失败"语义。用 `startFailedEmitted` 标记去重，防止与 errorOccurred/timeout handler 重复。

```cpp
void InstanceManager::onProcessFinished(const QString& instanceId,
                                        int exitCode,
                                        QProcess::ExitStatus status) {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) return;

    Instance* inst = it->second.get();
    const QString projectId = inst->projectId;
    const bool wasStarting = (inst->status == "starting");
    const bool abnormal = status == QProcess::CrashExit || exitCode != 0;
    inst->status = abnormal ? "failed" : "stopped";

    // starting 阶段退出：补发 startFailed（如果 errorOccurred/timeout 未发过）
    if (wasStarting && !inst->startFailedEmitted) {
        inst->startFailedEmitted = true;
        const QString reason = abnormal
            ? QStringLiteral("process exited during startup (code %1)").arg(exitCode)
            : QStringLiteral("process exited normally before started signal");
        emit instanceStartFailed(instanceId, projectId, reason);
    }

    // 始终发射 instanceFinished — ScheduleEngine 依赖此信号做 daemon 重启
    emit instanceFinished(instanceId, projectId, exitCode, status);

    if (inst->process) {
        inst->process->deleteLater();
        inst->process = nullptr;
    }
    m_instances.erase(it);
}
```

**Instance 模型新增去重标记** (`model/instance.h`):

```cpp
struct Instance {
    // ... 现有字段 ...
    bool startFailedEmitted = false;  // 防止 startFailed 事件重复发射
};
```

**EventBus 连接** (`server_manager.cpp`):

```cpp
connect(m_instanceManager, &InstanceManager::instanceStartFailed,
        this, [this](const QString& instanceId, const QString& projectId, const QString& error) {
            m_eventBus->publish("instance.startFailed", QJsonObject{
                {"instanceId", instanceId},
                {"projectId", projectId},
                {"error", error}
            });
        });
```

**HTTP 响应变化** (`api_router.cpp` — `handleProjectStart`):

返回 `200 OK` + `{"instanceId": "inst_xxx", "status": "starting"}`。客户端通过 SSE 事件 `instance.started` 获取最终 PID。

**ScheduleEngine 影响**: `onInstanceFinished` 继续通过 `instanceFinished` 信号工作，零回归风险（`instanceFinished` 始终发射）。额外连接 `instanceStartFailed` 到 EventBus 推送 SSE 即可，ScheduleEngine 本身无需改动。

---

### 4.3 DriverLabWsConnection — 异步启动/停止

**修改文件**: `src/stdiolink_server/http/driverlab_ws_connection.cpp`

与 4.2 同一模式，应用于 WebSocket 上下文。

**startDriver() 改动**:

```cpp
// 新增 started 信号连接
connect(m_process.get(), &QProcess::started, this, [this, queryMeta]() {
    sendJson(QJsonObject{
        {"type", "driver.started"},
        {"pid", static_cast<qint64>(m_process->processId())}
    });
    if (queryMeta) {
        m_process->write("{\"cmd\":\"meta.describe\",\"data\":{}}\n");
        QTimer::singleShot(5000, this, [this]() {
            if (!m_metaSent && m_socket &&
                m_socket->state() == QAbstractSocket::ConnectedState) {
                sendJson(QJsonObject{
                    {"type", "error"},
                    {"message", "meta query timeout"}
                });
            }
        });
    }
});

m_process->start(m_program, args);
// 移除: waitForStarted(5000) 及后续同步代码块
// errorOccurred(FailedToStart) 已由 onDriverErrorOccurred() 处理
```

**stopDriver() 改动**:

```cpp
void DriverLabWsConnection::stopDriver() {
    if (!m_process) return;
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        // 移除: m_process->waitForFinished(1000);
        // kill(SIGKILL) 后进程立即终止，随后 m_process.reset() 调用
        // QProcess 析构函数内部的 waitForFinished 耗时在微秒级（进程已死）。
    }
    m_process.reset();
}
```

> 注：QProcess 析构函数在进程仍运行时会调用 `waitForFinished()`。
> 但此处已先执行 `kill()`（SIGKILL 不可捕获，进程立即终止），
> 析构函数的等待实际耗时可忽略。这是有意为之的设计，非遗漏。

---

### 4.4 DriverManagerScanner — QtConcurrent 工作线程

**修改文件**:
- `src/stdiolink_server/CMakeLists.txt`
- `src/stdiolink_server/server_manager.h`
- `src/stdiolink_server/server_manager.cpp`
- `src/stdiolink_server/http/api_router.h`
- `src/stdiolink_server/http/api_router.cpp`

最严重的阻塞点：每个 driver 最多 10 秒，N 个 driver 串行。`tryExportMeta()` 使用栈上局部 `QProcess` + `waitForFinished()`，天然适合线程池。

**CMakeLists.txt** (声明最低 Qt 6.6，当前项目实际为 6.10.0):

```cmake
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Network HttpServer WebSockets Concurrent)
target_link_libraries(stdiolink_server PRIVATE ... Qt6::Concurrent)
```

**ServerManager 新增异步方法**:

```cpp
// server_manager.h
#include <QFuture>
QFuture<DriverManagerScanner::ScanStats> rescanDriversAsync(bool refreshMeta = true);

// server_manager.cpp
#include <QtConcurrent>
#include <QFuture>  // 显式引入，不依赖 QtConcurrent 的传递包含

QFuture<DriverManagerScanner::ScanStats> ServerManager::rescanDriversAsync(bool refreshMeta) {
    const QString driversDir = m_dataRoot + "/drivers";
    if (!QDir(driversDir).exists()) {
        m_driverCatalog.clear();
        // Qt 6.6+ 使用 makeReadyValueFuture（makeReadyFuture 已废弃）
        return QtFuture::makeReadyValueFuture(DriverManagerScanner::ScanStats{});
    }

    DriverManagerScanner scanner = m_driverScanner;

    return QtConcurrent::run([scanner, driversDir, refreshMeta]() mutable {
        DriverManagerScanner::ScanStats stats;
        auto drivers = scanner.scan(driversDir, refreshMeta, &stats);
        return std::make_pair(drivers, stats);
    }).then(this, [this](auto result) {
        // .then(this, ...) 回到主线程执行，保证线程安全
        m_driverCatalog.replaceAll(result.first);
        return result.second;
    });
}
```

**HTTP handler 返回 QFuture** (Qt 6.4+ 原生支持，本方案因 `makeReadyValueFuture` 实际要求 6.6+):

```cpp
QFuture<QHttpServerResponse> ApiRouter::handleDriverScan(const QHttpServerRequest& req) {
    QJsonObject body;
    QString error;
    if (!parseJsonObjectBody(req, body, error)) {
        return QtFuture::makeReadyValueFuture(
            errorResponse(QHttpServerResponse::StatusCode::BadRequest, error));
    }

    bool refreshMeta = body.value("refreshMeta").toBool(true);

    return m_manager->rescanDriversAsync(refreshMeta)
        .then([](DriverManagerScanner::ScanStats stats) {
            return jsonResponse(QJsonObject{
                {"scanned", stats.scanned},
                {"updated", stats.updated},
                {"newlyFailed", stats.newlyFailed},
                {"skippedFailed", stats.skippedFailed}
            });
        });
}
```

**初始化路径**: `ServerManager::initialize()` 在 HTTP 服务器启动前调用，同步扫描可接受，无需改动。

---

### 4.5 ProcessGuardServer probe — UUID 名称跳过探测

**修改文件**: `src/stdiolink/guard/process_guard_server.cpp`

当前无参 `start()` 调用 `start(m_name)`，而 `m_name` 在构造函数中已生成为 UUID 字符串（非空），导致 `nameOverride.isEmpty()` 判断永远为 false，探测永远执行。

修复方式：拆分两个 `start()` 的实现路径，无参版本跳过探测。提取公共 `listenInternal()` 保留 `AddressInUseError` 下的 stale socket 恢复能力：

```cpp
// ---- private 方法：listen + stale socket 重试 ----
bool ProcessGuardServer::listenInternal() {
    m_server = new QLocalServer();
    m_server->setSocketOptions(QLocalServer::WorldAccessOption);

    bool listenOk = m_server->listen(m_name);
    if (!listenOk) {
        // stale socket 恢复：探测说没有活跃 server，但 socket 文件残留
        if (m_server->serverError() == QAbstractSocket::AddressInUseError) {
            QLocalServer::removeServer(m_name);
            listenOk = m_server->listen(m_name);
        }
        if (!listenOk) {
            delete m_server;
            m_server = nullptr;
            return false;
        }
    }

    QObject::connect(m_server, &QLocalServer::newConnection, [this]() {
        while (m_server->hasPendingConnections()) {
            QLocalSocket* sock = m_server->nextPendingConnection();
            if (sock) {
                m_connections.append(sock);
            }
        }
    });

    return true;
}

// ---- 无参版本：UUID 名称，跳过探测 ----
bool ProcessGuardServer::start() {
    if (m_server) stop();
    // m_name 在构造函数中已生成 UUID，冲突概率可忽略
    return listenInternal();
}

// ---- 指定名称版本：保留探测 ----
bool ProcessGuardServer::start(const QString& nameOverride) {
    if (m_server) stop();
    m_name = nameOverride;

    // 用户指定名称可能冲突，保留探测
    QLocalSocket probe;
    probe.connectToServer(m_name);
    if (probe.waitForConnected(200)) {
        probe.disconnectFromServer();
        return false;
    }

    return listenInternal();
}
```

> `listenInternal()` 保留了原有的 `AddressInUseError → removeServer → retry` 逻辑，
> 确保进程崩溃后残留的 socket 文件不会阻止重新启动。

---

## 5. 实例状态机

改造后的完整状态流转：

```
startInstance()
      │
      ▼
  [starting] ──QProcess::started──→ [running] ──kill()──→ [finished 信号] ──→ [stopped/failed]
      │                                                          ▲
      ├──errorOccurred(FailedToStart)──→ [failed] ──(手动补发)────→ instanceFinished
      │                                                          │
      └──QTimer 5s 超时──→ kill() ─────────────────────────────────┘
```

状态值：`starting` / `running` / `stopped` / `failed`

---

## 6. SSE 事件清单

| 事件 | 触发时机 | 状态 |
|------|---------|------|
| `instance.started` | 已有，QProcess::started 信号触发 | 改为异步触发 |
| `instance.startFailed` | **新增**，启动失败/超时/starting阶段退出（去重） | 新增 |
| `instance.finished` | 已有，QProcess::finished 信号触发；FailedToStart 时手动补发（exitCode=-1, CrashExit） | 始终发射（含 FailedToStart 及 starting 阶段退出） |

---

## 7. 线程模型约束

本次改造引入 `QtConcurrent` 工作线程（仅用于 driver scan），需遵守以下规则：

1. **主线程单写原则**: `m_services` / `m_projects` / `m_driverCatalog` / 调度状态 / 实例表只在主线程更新。
2. **QObject 归属**: 所有 QObject 子类（InstanceManager、ScheduleEngine、EventBus 等）归属主线程，不跨线程直接调用其方法。
3. **QProcess 线程安全**: QProcess 由所属线程创建并管理。`DriverManagerScanner::tryExportMeta()` 中的 QProcess 是栈上局部对象，在工作线程创建和销毁，不涉及跨线程。
4. **跨线程结果回传**: 通过 `QFuture::then(this, ...)` 将续体调度回主线程执行，等价于 `Qt::QueuedConnection`。
5. **工作线程只做纯计算/IO**: 不直接修改主线程对象，不发信号给主线程 QObject（通过 QFuture 机制间接回传）。

---

## 8. 受影响的 HTTP Handler 汇总

| Handler | 当前阻塞来源 | 改造后行为 |
|---------|-------------|-----------|
| `handleProjectStart` | `startInstance()` 内 waitForStarted 5s | 立即返回 `{instanceId, status:"starting"}` |
| `handleProjectStop` | `terminateByProject()` 内 waitForFinished 1s×N | 立即返回，kill 后异步清理 |
| `handleProjectUpdate` | 同上 | 同上 |
| `handleProjectDelete` | 同上 | 同上 |
| `handleProjectEnabled` | 同上 | 同上 |
| `handleInstanceTerminate` | `terminateInstance()` 内 waitForFinished 1s | 立即返回 |
| `handleDriverScan` | `rescanDrivers()` 内 waitForFinished 10s×N | 返回 `QFuture`，异步完成 |
| `handleServiceScan` | 同步文件 IO（亚毫秒级） | 保持同步，不改 |

---

## 9. 验证方案

1. **启动/停止验证**: 启动实例，确认 HTTP 立即返回 `status:"starting"`；通过 SSE 确认收到 `instance.started` 事件（含 PID）或 `instance.startFailed` 事件。
2. **DriverLab 验证**: WebUI 连接 driver，确认 `driver.started` 消息通过 WebSocket 正确送达。
3. **Scanner 验证**: 调用 `POST /api/drivers/scan`，同时请求其他 API，确认不被阻塞。
4. **回归测试**: `build/bin/stdiolink_tests` 全部通过。
5. **并发验证**: 同时发起多个 startInstance + SSE 连接 + 普通查询，确认服务器无卡顿。
6. **FailedToStart 回归**: 构造启动失败场景（如不存在的可执行文件），验证：仅收到一次 `instance.startFailed` 事件；收到一次 `instance.finished`（exitCode=-1, CrashExit）；ScheduleEngine daemon 重启逻辑正常触发。
7. **前端兼容性**: 确认 WebUI 对 `exitCode=-1` 的展示（实例列表、日志面板）和统计逻辑（失败计数、重启次数）符合预期，无异常渲染或 NaN 等问题。
