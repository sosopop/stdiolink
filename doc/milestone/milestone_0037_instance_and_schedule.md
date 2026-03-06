# 里程碑 37：InstanceManager 与 ScheduleEngine

> **前置条件**: 里程碑 36（ProjectManager）已完成
> **目标**: 实现 Instance 生命周期管理、三种调度策略引擎、Manager 优雅关闭流程

---

## 1. 目标

- 实现 `InstanceManager`，管理 `stdiolink_service` 子进程的创建、监控、终止
- 实现 `ScheduleEngine`，根据 Project 的 Schedule 配置自动调度 Instance
- 支持三种调度模式：`manual`、`fixed_rate`、`daemon`
- 实现 Manager 优雅关闭流程（SIGTERM/SIGINT 处理）
- 日志重定向到 `logs/{projectId}.log`
- 在 `main.cpp` 中集成调度启动步骤

---

## 2. 技术要点

### 2.1 Instance 生命周期

```
启动 Instance:
  1. 读取 Project 配置
  2. 使用 `ProjectManager` 的预校验结果（可选二次校验用于即时错误提示）
  3. 解析 stdiolink_service 路径（优先级）:
     1) config.json.serviceProgram
     2) 与 stdiolink_server 同目录下的 stdiolink_service
     3) PATH 中的 stdiolink_service
  4. 生成临时配置文件（QTemporaryFile）
     - 写入 Project 原始 config（非 mergedConfig）
     - 由 stdiolink_service 在启动时做最终 merge+validate
  5. 启动子进程:
     <stdiolink_service_path> <service_dir> --config-file=<temp_config>
  6. 重定向日志到 logs/{projectId}.log
  7. 记录 Instance 到内存

监控 Instance:
  1. 监听 QProcess::finished 信号
  2. 记录退出码
  3. 根据调度类型决定是否重启:
     - daemon + 异常退出（`CrashExit` 或 `exitCode!=0`）→ 延迟后重启
     - 其他 → 不重启
  4. 从内存移除 Instance 对象
  5. 清理临时配置文件
```

### 2.2 调度策略

| 模式 | 启动时机 | 退出后行为 |
|------|---------|-----------|
| `manual` | 仅 API 手动触发 | 不重启 |
| `fixed_rate` | 定时器周期触发 | 不重启，等待下次定时 |
| `daemon` | 立即启动 | 异常退出延迟重启，正常退出不重启 |

说明：为精确区分崩溃退出，建议在 `instanceFinished` 事件中同时携带 `QProcess::ExitStatus`。

### 2.3 Manager 关闭流程

```
收到 SIGTERM/SIGINT:
  1. 设置 shuttingDown 标记（停止接受新请求）
  2. 停止所有调度器（fixed_rate 定时器、daemon 自动重启）
  3. 向所有 running Instance 发送 terminate()
  4. 等待实例退出（graceTimeoutMs，默认 5000）
  5. 超时未退出的实例执行 kill()
  6. 清理临时配置文件并退出
```

注：`std::signal` 示例仅用于说明流程，生产实现建议使用 self-pipe/signalfd（Unix）或平台等价方案，避免在异步信号上下文直接触发复杂逻辑。

### 2.4 stdiolink_service 路径查找

`InstanceManager` 需要定位 `stdiolink_service` 可执行文件，查找优先级：

1. `ServerConfig.serviceProgram`（用户显式配置）
2. 与 `stdiolink_server` 同目录下的 `stdiolink_service`
3. `PATH` 环境变量中的 `stdiolink_service`

---

## 3. 实现步骤

### 3.1 Instance 数据结构

```cpp
// src/stdiolink_server/model/instance.h
#pragma once

#include <QDateTime>
#include <QProcess>
#include <QString>
#include <QTemporaryFile>
#include <memory>

namespace stdiolink_server {

struct Instance {
    QString id;
    QString projectId;
    QString serviceId;
    QProcess* process = nullptr;
    QDateTime startedAt;
    qint64 pid = 0;
    QString status;  // "starting", "running", "stopped", "failed"
    std::unique_ptr<QTemporaryFile> tempConfigFile;
};

} // namespace stdiolink_server
```

### 3.2 InstanceManager 头文件

```cpp
// src/stdiolink_server/manager/instance_manager.h
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include "model/instance.h"
#include "model/project.h"
#include "config/server_config.h"

namespace stdiolink_server {

class InstanceManager : public QObject {
    Q_OBJECT
public:
    explicit InstanceManager(const QString& dataRoot,
                             const ServerConfig& config,
                             QObject* parent = nullptr);

    /// 启动一个 Project 的 Instance，返回 instanceId
    QString startInstance(const Project& project,
                          const QString& serviceDir,
                          QString& error);

    /// 终止指定 Instance
    void terminateInstance(const QString& instanceId);

    /// 终止某 Project 的所有 Instance
    void terminateByProject(const QString& projectId);

    /// 终止所有 Instance（关闭流程）
    void terminateAll();

    /// 等待所有 Instance 退出，超时后 kill
    void waitAllFinished(int graceTimeoutMs = 5000);

    /// 查询
    QList<Instance*> getInstances(
        const QString& projectId = QString()) const;
    Instance* getInstance(const QString& instanceId) const;
    int instanceCount(const QString& projectId) const;

signals:
    void instanceStarted(const QString& instanceId,
                         const QString& projectId);
    void instanceFinished(const QString& instanceId,
                          const QString& projectId,
                          int exitCode);

private:
    QString findServiceProgram() const;
    QString generateInstanceId() const;
    void onProcessFinished(const QString& instanceId,
                           int exitCode,
                           QProcess::ExitStatus status);

    QString m_dataRoot;
    ServerConfig m_config;
    QHash<QString, std::unique_ptr<Instance>> m_instances;
};

} // namespace stdiolink_server
```

### 3.3 InstanceManager 实现（核心方法）

**`findServiceProgram()` 路径查找**：

```cpp
// src/stdiolink_server/manager/instance_manager.cpp
#include "instance_manager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>

namespace stdiolink_server {

InstanceManager::InstanceManager(const QString& dataRoot,
                                 const ServerConfig& config,
                                 QObject* parent)
    : QObject(parent), m_dataRoot(dataRoot), m_config(config)
{
}

QString InstanceManager::findServiceProgram() const {
    // 1. 用户显式配置
    if (!m_config.serviceProgram.isEmpty()) {
        if (QFileInfo(m_config.serviceProgram).isExecutable())
            return m_config.serviceProgram;
    }

    // 2. 与 stdiolink_server 同目录
    QString appDir = QCoreApplication::applicationDirPath();
    QString sameDir = appDir + "/stdiolink_service";
#ifdef Q_OS_WIN
    sameDir += ".exe";
#endif
    if (QFileInfo(sameDir).isExecutable())
        return sameDir;

    // 3. PATH 中查找
    QString inPath = QStandardPaths::findExecutable(
        "stdiolink_service");
    return inPath;
}

QString InstanceManager::generateInstanceId() const {
    return "inst_" + QUuid::createUuid().toString(
        QUuid::WithoutBraces).left(8);
}
```

**`startInstance()` 启动逻辑**：

```cpp
QString InstanceManager::startInstance(
    const Project& project,
    const QString& serviceDir,
    QString& error)
{
    QString program = findServiceProgram();
    if (program.isEmpty()) {
        error = "stdiolink_service not found";
        return {};
    }

    // 生成临时配置文件
    auto tempFile = std::make_unique<QTemporaryFile>();
    tempFile->setAutoRemove(true);
    if (!tempFile->open()) {
        error = "cannot create temp config file";
        return {};
    }
    QJsonDocument doc(project.config);
    tempFile->write(doc.toJson());
    tempFile->flush();

    // 构建命令行参数
    QStringList args;
    args << serviceDir
         << ("--config-file=" + tempFile->fileName());

    // 创建 Instance
    QString instId = generateInstanceId();
    auto inst = std::make_unique<Instance>();
    inst->id = instId;
    inst->projectId = project.id;
    inst->serviceId = project.serviceId;
    inst->startedAt = QDateTime::currentDateTimeUtc();
    inst->status = "starting";
    inst->tempConfigFile = std::move(tempFile);

    // 启动子进程
    auto* proc = new QProcess(this);
    inst->process = proc;

    // 日志重定向
    QString logPath = m_dataRoot + "/logs/" + project.id + ".log";
    proc->setStandardOutputFile(logPath, QIODevice::Append);
    proc->setStandardErrorFile(logPath, QIODevice::Append);

    // 监听退出信号
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(
                &QProcess::finished),
            this, [this, instId](int exitCode,
                                 QProcess::ExitStatus status) {
                onProcessFinished(instId, exitCode, status);
            });

    proc->start(program, args);
    if (!proc->waitForStarted(5000)) {
        error = "process failed to start: "
                + proc->errorString();
        delete proc;
        return {};
    }

    inst->pid = proc->processId();
    inst->status = "running";

    Instance* rawPtr = inst.get();
    m_instances.insert(instId, std::move(inst));

    emit instanceStarted(instId, project.id);
    Q_UNUSED(rawPtr);
    return instId;
}
```

**终止与关闭方法**：

```cpp
void InstanceManager::terminateInstance(const QString& instanceId) {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) return;
    Instance* inst = it->get();
    if (inst->process && inst->process->state() != QProcess::NotRunning) {
        inst->process->terminate();
    }
}

void InstanceManager::terminateByProject(const QString& projectId) {
    for (auto& [id, inst] : m_instances) {
        if (inst->projectId == projectId) {
            terminateInstance(id);
        }
    }
}

void InstanceManager::terminateAll() {
    for (auto& [id, inst] : m_instances) {
        if (inst->process &&
            inst->process->state() != QProcess::NotRunning) {
            inst->process->terminate();
        }
    }
}
```

**`waitAllFinished()` 优雅关闭**：

```cpp
void InstanceManager::waitAllFinished(int graceTimeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < graceTimeoutMs) {
        bool allDone = true;
        for (auto& [id, inst] : m_instances) {
            if (inst->process &&
                inst->process->state() != QProcess::NotRunning) {
                allDone = false;
                break;
            }
        }
        if (allDone) return;
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 100);
    }
    // 超时：强制 kill
    for (auto& [id, inst] : m_instances) {
        if (inst->process &&
            inst->process->state() != QProcess::NotRunning) {
            qWarning("Force killing instance: %s",
                     qUtf8Printable(id));
            inst->process->kill();
        }
    }
}
```

**`onProcessFinished()` 退出回调**：

```cpp
void InstanceManager::onProcessFinished(
    const QString& instanceId,
    int exitCode,
    QProcess::ExitStatus status)
{
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) return;

    Instance* inst = it->get();
    const bool abnormal =
        (status == QProcess::CrashExit) || (exitCode != 0);
    inst->status = abnormal ? "failed" : "stopped";
    QString projectId = inst->projectId;

    qInfo("Instance %s (project=%s) exited with code %d",
          qUtf8Printable(instanceId),
          qUtf8Printable(projectId),
          exitCode);

    emit instanceFinished(instanceId, projectId, exitCode);

    // 清理：删除进程和临时文件
    inst->process->deleteLater();
    inst->process = nullptr;
    inst->tempConfigFile.reset();
    m_instances.erase(it);
}

} // namespace stdiolink_server
```

### 3.4 ScheduleEngine 头文件

```cpp
// src/stdiolink_server/manager/schedule_engine.h
#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>
#include "model/project.h"
#include "manager/instance_manager.h"
#include "scanner/service_scanner.h"

namespace stdiolink_server {

class ScheduleEngine : public QObject {
    Q_OBJECT
public:
    explicit ScheduleEngine(
        InstanceManager* instanceMgr,
        QObject* parent = nullptr);

    /// 为所有 enabled+valid 的 Project 启动调度
    void startAll(
        const QMap<QString, Project>& projects,
        const QMap<QString, ServiceInfo>& services);

    /// 停止所有调度器
    void stopAll();

    /// 停止单个 Project 的调度
    void stopProject(const QString& projectId);

    bool isShuttingDown() const { return m_shuttingDown; }
    void setShuttingDown(bool v) { m_shuttingDown = v; }

private slots:
    void onInstanceFinished(const QString& instanceId,
                            const QString& projectId,
                            int exitCode);

private:
    void startDaemon(const Project& project,
                     const QString& serviceDir);
    void startFixedRate(const Project& project,
                        const QString& serviceDir);

    InstanceManager* m_instanceMgr;
    QMap<QString, ServiceInfo> m_services;
    QMap<QString, Project> m_projects;
    QHash<QString, QTimer*> m_timers;
    QHash<QString, int> m_consecutiveFailures;
    bool m_shuttingDown = false;
};

} // namespace stdiolink_server
```

### 3.5 ScheduleEngine 实现（核心方法）

**`startAll()` 调度启动**：

```cpp
// src/stdiolink_server/manager/schedule_engine.cpp
#include "schedule_engine.h"

namespace stdiolink_server {

ScheduleEngine::ScheduleEngine(
    InstanceManager* instanceMgr, QObject* parent)
    : QObject(parent), m_instanceMgr(instanceMgr)
{
    connect(m_instanceMgr,
            &InstanceManager::instanceFinished,
            this,
            &ScheduleEngine::onInstanceFinished);
}

void ScheduleEngine::startAll(
    const QMap<QString, Project>& projects,
    const QMap<QString, ServiceInfo>& services)
{
    m_projects = projects;
    m_services = services;

    for (auto it = projects.begin(); it != projects.end(); ++it) {
        const Project& p = it.value();
        if (!p.valid || !p.enabled) continue;

        const QString serviceDir =
            m_services.contains(p.serviceId)
                ? m_services[p.serviceId].serviceDir
                : QString();
        if (serviceDir.isEmpty()) continue;

        switch (p.schedule.type) {
        case ScheduleType::Manual:
            // 不自动启动
            break;
        case ScheduleType::FixedRate:
            startFixedRate(p, serviceDir);
            break;
        case ScheduleType::Daemon:
            startDaemon(p, serviceDir);
            break;
        }
    }
}
```

**`startDaemon()` 和 `startFixedRate()`**：

```cpp
void ScheduleEngine::startDaemon(
    const Project& project,
    const QString& serviceDir)
{
    QString error;
    QString instId = m_instanceMgr->startInstance(
        project, serviceDir, error);
    if (instId.isEmpty()) {
        qWarning("Daemon start failed for %s: %s",
                 qUtf8Printable(project.id),
                 qUtf8Printable(error));
    }
}
```

```cpp
void ScheduleEngine::startFixedRate(
    const Project& project,
    const QString& serviceDir)
{
    auto* timer = new QTimer(this);
    timer->setInterval(project.schedule.intervalMs);
    connect(timer, &QTimer::timeout, this,
            [this, id = project.id, serviceDir]() {
        if (m_shuttingDown) return;
        if (!m_projects.contains(id)) return;
        const Project& p = m_projects[id];
        int running = m_instanceMgr->instanceCount(id);
        if (running >= p.schedule.maxConcurrent) return;
        QString error;
        m_instanceMgr->startInstance(p, serviceDir, error);
        if (!error.isEmpty()) {
            qWarning("FixedRate trigger failed for %s: %s",
                     qUtf8Printable(id),
                     qUtf8Printable(error));
        }
    });
    m_timers.insert(project.id, timer);
    timer->start();
}
```

**`onInstanceFinished()` daemon 重启逻辑**：

```cpp
void ScheduleEngine::onInstanceFinished(
    const QString& instanceId,
    const QString& projectId,
    int exitCode)
{
    Q_UNUSED(instanceId);
    if (m_shuttingDown) return;
    if (!m_projects.contains(projectId)) return;

    const Project& p = m_projects[projectId];
    if (p.schedule.type != ScheduleType::Daemon) return;
    // 若后续扩展 instanceFinished 事件参数，可优先用 ExitStatus 判定崩溃退出
    if (exitCode == 0) {
        m_consecutiveFailures.remove(projectId);
        return; // 正常退出，不重启
    }

    // 异常退出
    int& failures = m_consecutiveFailures[projectId];
    failures++;
    if (failures >= p.schedule.maxConsecutiveFailures) {
        qWarning("Project %s: crash loop detected (%d failures)",
                 qUtf8Printable(projectId), failures);
        return; // 停止自动重启
    }

    // 延迟重启
    const QString serviceDir =
        m_services.contains(p.serviceId)
            ? m_services[p.serviceId].serviceDir
            : QString();
    QTimer::singleShot(
        p.schedule.restartDelayMs, this,
        [this, projectId, serviceDir]() {
            if (m_shuttingDown) return;
            if (!m_projects.contains(projectId)) return;
            startDaemon(m_projects[projectId], serviceDir);
        });
}
```

**`stopAll()` 和 `stopProject()`**：

```cpp
void ScheduleEngine::stopAll() {
    for (auto it = m_timers.begin(); it != m_timers.end(); ++it) {
        it.value()->stop();
        it.value()->deleteLater();
    }
    m_timers.clear();
    m_consecutiveFailures.clear();
}

void ScheduleEngine::stopProject(const QString& projectId) {
    if (m_timers.contains(projectId)) {
        m_timers[projectId]->stop();
        m_timers[projectId]->deleteLater();
        m_timers.remove(projectId);
    }
    m_consecutiveFailures.remove(projectId);
}

} // namespace stdiolink_server
```

### 3.6 main.cpp 集成

在里程碑 36 的 `main.cpp` 中，Project 加载之后插入调度启动和信号处理：

```cpp
// main.cpp — 新增部分
#include "manager/instance_manager.h"
#include "manager/schedule_engine.h"
#include <csignal>

// ... Project 加载之后 ...

// 创建 InstanceManager 和 ScheduleEngine
InstanceManager instanceMgr(dataRoot, config);
ScheduleEngine scheduleEngine(&instanceMgr);

// 启动调度
scheduleEngine.startAll(projects, services);

// 信号处理：优雅关闭
auto shutdown = [&]() {
    qInfo("Shutting down...");
    scheduleEngine.setShuttingDown(true);
    scheduleEngine.stopAll();
    instanceMgr.terminateAll();
    instanceMgr.waitAllFinished(5000);
    QCoreApplication::quit();
};

// 注册 SIGTERM/SIGINT（示例：生产实现建议采用更安全的 signal-bridge）
std::signal(SIGINT, [](int) {
    QMetaObject::invokeMethod(
        qApp, []() { qApp->quit(); },
        Qt::QueuedConnection);
});
std::signal(SIGTERM, [](int) {
    QMetaObject::invokeMethod(
        qApp, []() { qApp->quit(); },
        Qt::QueuedConnection);
});

QObject::connect(&app, &QCoreApplication::aboutToQuit,
                 shutdown);

return app.exec();
```

---

## 4. 文件清单

| 操作 | 文件 | 说明 |
|------|------|------|
| 新增 | `src/stdiolink_server/model/instance.h` | Instance 数据结构 |
| 新增 | `src/stdiolink_server/manager/instance_manager.h` | InstanceManager 头文件 |
| 新增 | `src/stdiolink_server/manager/instance_manager.cpp` | InstanceManager 实现 |
| 新增 | `src/stdiolink_server/manager/schedule_engine.h` | ScheduleEngine 头文件 |
| 新增 | `src/stdiolink_server/manager/schedule_engine.cpp` | ScheduleEngine 实现 |
| 修改 | `src/stdiolink_server/main.cpp` | 集成调度启动和信号处理 |
| 修改 | `src/stdiolink_server/CMakeLists.txt` | 新增源文件 |

---

## 5. 验收标准

1. `InstanceManager` 能正确启动 `stdiolink_service` 子进程
2. 子进程命令行参数格式正确：`<service_dir> --config-file=<temp>`
3. 临时配置文件包含 Project 原始 config
4. 日志正确重定向到 `logs/{projectId}.log`
5. `stdiolink_service` 路径查找按优先级生效
6. 子进程退出后 Instance 从内存移除，临时文件清理
7. `instanceStarted` / `instanceFinished` 信号正确触发
8. `manual` 模式不自动启动 Instance
9. `fixed_rate` 模式按 `intervalMs` 周期触发，达到 `maxConcurrent` 时跳过
10. `daemon` 模式立即启动，异常退出（含 `CrashExit`）后延迟 `restartDelayMs` 重启
11. `daemon` 连续失败达到 `maxConsecutiveFailures` 时停止自动重启
12. `daemon` 正常退出（exitCode=0）不重启，重置失败计数
13. `terminateAll()` 向所有 Instance 发送 terminate
14. `waitAllFinished()` 超时后强制 kill
15. SIGTERM/SIGINT 触发优雅关闭流程

---

## 6. 单元测试用例

### 6.1 InstanceManager 测试

```cpp
#include <gtest/gtest.h>
#include "manager/instance_manager.h"
#include "config/server_config.h"

using namespace stdiolink_server;

TEST(InstanceManagerTest, GenerateUniqueIds) {
    ServerConfig config;
    InstanceManager mgr("/tmp/test", config);
    // 通过启动两个 Instance 验证 ID 唯一性
    // （需要 mock stdiolink_service 或使用测试替身）
}

TEST(InstanceManagerTest, InstanceCountByProject) {
    ServerConfig config;
    InstanceManager mgr("/tmp/test", config);
    EXPECT_EQ(mgr.instanceCount("nonexistent"), 0);
}
```

### 6.2 ScheduleEngine 测试

```cpp
#include <gtest/gtest.h>
#include "manager/schedule_engine.h"

using namespace stdiolink_server;

TEST(ScheduleEngineTest, ManualNotAutoStarted) {
    // manual 模式的 Project 不应自动启动
    // 验证 startAll() 后 instanceCount == 0
}

TEST(ScheduleEngineTest, DaemonCrashLoopDetection) {
    // 模拟连续 maxConsecutiveFailures 次异常退出
    // 验证停止自动重启
}

TEST(ScheduleEngineTest, DaemonNormalExitNoRestart) {
    // daemon 模式 exitCode=0 时不重启
}

TEST(ScheduleEngineTest, StopAllClearsTimers) {
    // stopAll() 后所有定时器被清理
}
```

---

## 7. 依赖关系

### 7.1 前置依赖

| 依赖项 | 说明 |
|--------|------|
| 里程碑 34（Server 脚手架） | `main.cpp`、`ServerConfig` |
| 里程碑 36（ProjectManager） | `Project`、`Schedule` 数据结构 |
| `stdiolink_service` | 被启动的子进程可执行文件 |

### 7.2 后置影响

| 后续里程碑 | 依赖内容 |
|-----------|---------|
| 里程碑 38（HTTP API） | `InstanceManager` 的 start/stop/terminate 操作、`ScheduleEngine` 的 stopProject |
