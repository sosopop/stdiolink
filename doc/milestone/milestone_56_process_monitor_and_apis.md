# 里程碑 56：跨平台 ProcessMonitor 与进程树/资源 API

> **前置条件**: 里程碑 50 已完成（Instance 详情 API 已就绪）
> **目标**: 实现以 Linux 为主的进程树采集和资源监控工具类 `ProcessMonitor`，并暴露 `GET /api/instances/{id}/process-tree` 和 `GET /api/instances/{id}/resources` API；Windows/macOS 先提供安全降级实现

---

## 1. 目标

- 实现 `ProcessMonitor` 工具类（Linux 完整实现，Windows/macOS 先提供不崩溃的降级实现）
- 支持获取进程树（以指定 PID 为根递归获取子进程）
- 支持获取进程资源信息（CPU%、RSS、VMS、线程数、运行时长、I/O）
- 实现 `GET /api/instances/{id}/process-tree` — 进程树 API
- 实现 `GET /api/instances/{id}/resources` — 资源快照 API
- CPU 使用率采用两次采样差值计算，首次可返回 0

---

## 2. 背景与问题

WebUI 需要展示 Instance 的完整进程树及每个进程的资源占用。当前 Instance 仅记录顶层 `QProcess` 的 PID 和状态，缺少子进程枚举和资源采集能力。获取进程信息的 API 在 Linux/macOS/Windows 上完全不同，需要封装跨平台实现。

---

## 3. 技术要点

### 3.1 数据模型

```cpp
// src/stdiolink_server/model/process_info.h

struct ProcessInfo {
    qint64 pid = 0;
    qint64 parentPid = 0;
    QString name;
    QString commandLine;
    QString status;          // "running" / "sleeping" / "zombie" / "stopped"
    QDateTime startedAt;

    double cpuPercent = 0.0;
    qint64 memoryRssBytes = 0;
    qint64 memoryVmsBytes = 0;
    int threadCount = 0;
    qint64 uptimeSeconds = 0;
    qint64 ioReadBytes = 0;
    qint64 ioWriteBytes = 0;
};

struct ProcessTreeNode {
    ProcessInfo info;
    QVector<ProcessTreeNode> children;
};

struct ProcessTreeSummary {
    int totalProcesses = 0;
    double totalCpuPercent = 0.0;
    qint64 totalMemoryRssBytes = 0;
    int totalThreads = 0;
};
```

### 3.2 平台实现

**Linux**（`/proc` 文件系统）：

| 数据 | 来源 |
|------|------|
| 进程名 | `/proc/{pid}/comm` |
| 命令行 | `/proc/{pid}/cmdline`（NUL 分隔 → 空格拼接） |
| 状态 | `/proc/{pid}/stat` 第 3 字段（`R`/`S`/`Z`/`T`） |
| CPU 时间 | `/proc/{pid}/stat` 第 14-17 字段（utime + stime） |
| 内存 RSS | `/proc/{pid}/stat` 第 24 字段 × pagesize |
| 内存 VMS | `/proc/{pid}/stat` 第 23 字段 |
| 线程数 | `/proc/{pid}/stat` 第 20 字段 |
| 启动时间 | `/proc/{pid}/stat` 第 22 字段 + boot time |
| I/O | `/proc/{pid}/io`（`read_bytes`/`write_bytes`） |
| 子进程 | 优先使用 `/proc/{pid}/task/{pid}/children`（内核 ≥ 3.5），仅返回直接子进程 PID，无需遍历整个 `/proc`；如该文件不存在则回退到遍历 `/proc/*/stat` 匹配 ppid |

**Windows**：

| 数据 | 来源 |
|------|------|
| 进程枚举 | `CreateToolhelp32Snapshot` + `Process32First/Next` |
| 内存 | `GetProcessMemoryInfo`（`WorkingSetSize` → RSS） |
| CPU 时间 | `GetProcessTimes`（`KernelTime` + `UserTime`） |
| 线程数 | `PROCESSENTRY32.cntThreads` |
| 命令行 | `NtQueryInformationProcess` 或 `QueryFullProcessImageName` |
| I/O | `GetProcessIoCounters` |

**macOS**：

| 数据 | 来源 |
|------|------|
| 子进程 | `proc_listchildpids()` |
| 进程信息 | `proc_pidinfo(PROC_PIDTASKINFO)` |
| 命令行 | `sysctl(CTL_KERN, KERN_PROCARGS2)` |
| 内存 | `proc_taskinfo.pti_resident_size` |
| CPU 时间 | `proc_taskinfo.pti_total_user + pti_total_system` |

### 3.3 CPU 使用率计算

CPU% 需要两次采样间的差值：

```
cpu% = (cpu_time_delta / wall_time_delta) × 100
```

`ProcessMonitor` 维护一个 `QMap<qint64, CpuSample>` 缓存上次采样时间和 CPU 累计时间。首次采样某 PID 时 CPU% 返回 0。

```cpp
struct CpuSample {
    qint64 cpuTimeMs;     // 累计 CPU 时间（毫秒）
    QDateTime timestamp;   // 采样时间
};
```

### 3.4 `GET /api/instances/{id}/process-tree`

响应（200 OK）：

```json
{
  "instanceId": "inst_abc",
  "rootPid": 12345,
  "tree": {
    "pid": 12345,
    "name": "stdiolink_service",
    "commandLine": "...",
    "status": "running",
    "startedAt": "...",
    "resources": {
      "cpuPercent": 2.5,
      "memoryRssBytes": 52428800,
      "memoryVmsBytes": 134217728,
      "threadCount": 8,
      "uptimeSeconds": 3600
    },
    "children": [ ... ]
  },
  "summary": {
    "totalProcesses": 3,
    "totalCpuPercent": 18.5,
    "totalMemoryRssBytes": 337641472,
    "totalThreads": 23
  }
}
```

错误码：`404`（Instance 不存在——包括已退出的 Instance，因为当前实现在进程退出后立即从内存删除）

> **注意**：当前 `InstanceManager::onProcessFinished` 在进程退出后调用 `m_instances.erase()`，已退出的 Instance 无法通过 ID 查到，统一返回 404。若未来需要保留历史 Instance 记录（如用于审计），可引入 `410 Gone` 区分"从未存在"和"曾存在但已退出"。

### 3.5 `GET /api/instances/{id}/resources`

查询参数：

| 参数 | 类型 | 说明 |
|------|------|------|
| `includeChildren` | bool | 是否包含子进程（默认 true） |

响应（200 OK）：

```json
{
  "instanceId": "inst_abc",
  "timestamp": "2026-02-12T10:30:00Z",
  "processes": [
    { "pid": 12345, "name": "stdiolink_service", "cpuPercent": 2.5,
      "memoryRssBytes": 52428800, "threadCount": 8, "uptimeSeconds": 3600,
      "ioReadBytes": 1048576, "ioWriteBytes": 524288 }
  ],
  "summary": { ... }
}
```

本接口为纯轮询模式，不提供服务端推送。前端建议 2-5 秒间隔轮询。

---

## 4. 实现方案

### 4.1 ProcessMonitor 类

```cpp
// src/stdiolink_server/manager/process_monitor.h
#pragma once

#include "model/process_info.h"
#include <QMap>
#include <QMutex>

namespace stdiolink_server {

class ProcessMonitor {
public:
    ProcessMonitor() = default;

    /// 获取进程的完整子进程树（含资源信息）
    ProcessTreeNode getProcessTree(qint64 rootPid);

    /// 获取单个进程的资源信息
    ProcessInfo getProcessInfo(qint64 pid);

    /// 获取进程及其所有后代的平坦列表
    QVector<ProcessInfo> getProcessFamily(qint64 rootPid,
                                           bool includeChildren = true);

    /// 计算树的汇总统计
    static ProcessTreeSummary summarize(const ProcessTreeNode& tree);
    static ProcessTreeSummary summarize(const QVector<ProcessInfo>& processes);

private:
    /// 获取指定进程的子进程 PID 列表（平台相关实现）
    QVector<qint64> getChildPids(qint64 pid);

    /// 读取单个进程的原始信息（平台相关实现）
    ProcessInfo readProcessInfo(qint64 pid);

    /// CPU 采样缓存
    struct CpuSample {
        qint64 cpuTimeMs = 0;
        QDateTime timestamp;
    };
    QMap<qint64, CpuSample> m_cpuSamples;

    /// 根据两次采样计算 CPU%
    double calculateCpuPercent(qint64 pid, qint64 currentCpuTimeMs);

    /// 清理已退出进程的采样缓存
    void cleanupSamples(const QSet<qint64>& alivePids);
};

} // namespace stdiolink_server
```

### 4.2 Linux 实现（首要平台）

```cpp
// process_monitor_linux.cpp (条件编译)
#ifdef Q_OS_LINUX

ProcessInfo ProcessMonitor::readProcessInfo(qint64 pid) {
    ProcessInfo info;
    info.pid = pid;

    // 读取 /proc/{pid}/stat
    QFile statFile(QString("/proc/%1/stat").arg(pid));
    if (!statFile.open(QIODevice::ReadOnly)) return info;
    QByteArray statData = statFile.readAll();
    // 解析字段时注意：第 2 字段 comm 被括号包围（如 "(my process)"），
    // 且进程名可包含空格、括号、换行符。必须先定位最后一个 ')' 的位置，
    // 以此为分界点解析后续字段。字段 2（comm）由第一个 '(' 到最后一个 ')'
    // 之间的内容确定。简单的空格分割会导致后续字段全部偏移。
    // ...

    // 读取 /proc/{pid}/comm
    QFile commFile(QString("/proc/%1/comm").arg(pid));
    if (commFile.open(QIODevice::ReadOnly))
        info.name = QString(commFile.readAll()).trimmed();

    // 读取 /proc/{pid}/cmdline
    QFile cmdFile(QString("/proc/%1/cmdline").arg(pid));
    if (cmdFile.open(QIODevice::ReadOnly)) {
        QByteArray cmdData = cmdFile.readAll();
        info.commandLine = QString(cmdData.replace('\0', ' ')).trimmed();
    }

    return info;
}

QVector<qint64> ProcessMonitor::getChildPids(qint64 pid) {
    QVector<qint64> children;

    // 优先使用 /proc/{pid}/task/{pid}/children（内核 ≥ 3.5，高效）
    QFile childrenFile(QString("/proc/%1/task/%1/children").arg(pid));
    if (childrenFile.open(QIODevice::ReadOnly)) {
        const QString data = QString(childrenFile.readAll()).trimmed();
        if (!data.isEmpty()) {
            for (const auto& token : data.split(' ', Qt::SkipEmptyParts)) {
                bool ok;
                qint64 childPid = token.toLongLong(&ok);
                if (ok) children.append(childPid);
            }
        }
        return children;
    }

    // 回退：遍历 /proc/*/stat 匹配 ppid
    QDir procDir("/proc");
    for (const auto& entry : procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok;
        qint64 childPid = entry.toLongLong(&ok);
        if (!ok) continue;

        QFile statFile(QString("/proc/%1/stat").arg(childPid));
        if (!statFile.open(QIODevice::ReadOnly)) continue;
        QByteArray data = statFile.readAll();
        // 解析 ppid（第 4 个字段）
        // 如果 ppid == pid，添加到 children
    }
    return children;
}

#endif
```

### 4.3 Windows / macOS 存根

首版为 Windows 和 macOS 提供存根实现（返回空数据 + warning 日志），后续里程碑完善：

```cpp
#if !defined(Q_OS_LINUX)
ProcessInfo ProcessMonitor::readProcessInfo(qint64 pid) {
    qWarning() << "ProcessMonitor: not implemented for this platform";
    ProcessInfo info;
    info.pid = pid;
    info.name = "unknown";
    return info;
}

QVector<qint64> ProcessMonitor::getChildPids(qint64 pid) {
    Q_UNUSED(pid);
    qWarning() << "ProcessMonitor: getChildPids not implemented for this platform";
    return {};
}
#endif
```

### 4.4 ApiRouter handler

```cpp
QHttpServerResponse ApiRouter::handleProcessTree(const QString& id,
                                                  const QHttpServerRequest& req) {
    // 1. 查找 Instance
    auto* inst = m_manager->instanceManager()->findInstance(id);
    if (!inst) return errorResponse(StatusCode::NotFound, "instance not found");
    if (inst->status != "running")
        return errorResponse(StatusCode::NotFound, "instance not running");

    // 2. 获取进程树
    auto tree = m_manager->processMonitor()->getProcessTree(inst->pid);
    auto summary = ProcessMonitor::summarize(tree);

    // 3. 序列化响应
    // ...
}
```

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/stdiolink_server/model/process_info.h` — ProcessInfo/ProcessTreeNode/ProcessTreeSummary 数据模型
- `src/stdiolink_server/manager/process_monitor.h` — ProcessMonitor 声明
- `src/stdiolink_server/manager/process_monitor.cpp` — 通用逻辑 + Linux 实现 + 其他平台存根

### 5.2 修改文件

- `src/stdiolink_server/server_manager.h` — 新增 `ProcessMonitor*` 成员和 getter
- `src/stdiolink_server/server_manager.cpp` — 初始化 ProcessMonitor
- `src/stdiolink_server/http/api_router.h` — 新增 `handleProcessTree`/`handleResources` handler
- `src/stdiolink_server/http/api_router.cpp` — 实现 handler + 路由注册
- `src/stdiolink_server/CMakeLists.txt` — 新增源文件

### 5.3 测试文件

- 新增 `src/tests/test_process_monitor.cpp`
- 修改 `src/tests/test_api_router.cpp` — 进程树/资源 API 测试

---

## 6. 测试与验收

### 6.1 单元测试场景

**ProcessMonitor（test_process_monitor.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 获取当前进程（`getpid()`）的 ProcessInfo | pid 正确，name 非空 |
| 2 | 当前进程 RSS > 0 | memoryRssBytes > 0 |
| 3 | 当前进程线程数 ≥ 1 | threadCount ≥ 1 |
| 4 | 获取不存在的 PID | 返回空/无效 ProcessInfo |
| 5 | 获取当前进程的进程树 | 树根 pid == getpid() |
| 6 | 启动子进程后获取进程树 | children 包含子进程 |
| 7 | 子进程退出后清理采样缓存 | 不遗留已退出 PID 的缓存 |
| 8 | CPU% 首次采样返回 0 | cpuPercent == 0.0 |
| 9 | 两次采样间 CPU% ≥ 0 | 第二次调用 cpuPercent >= 0 |
| 10 | summarize 正确汇总 | totalProcesses/totalMemory/totalThreads 正确 |
| 11 | getProcessFamily 平坦列表 | 包含根进程和所有子进程 |
| 12 | includeChildren=false | 仅返回根进程 |

**注意**：ProcessMonitor 测试依赖平台，CI 在 Linux 上运行。测试中启动 `sleep` 等简单子进程验证进程树采集。macOS/Windows 上使用 `QSKIP("ProcessMonitor not implemented on this platform")` 跳过平台相关测试，仅运行 `summarize()` 等纯逻辑测试。

**API 测试（test_api_router.cpp）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 13 | `GET /process-tree` 运行中 Instance | 200 + tree 含根节点 |
| 14 | `GET /process-tree` tree.summary 字段完整 | totalProcesses ≥ 1 |
| 15 | `GET /process-tree` Instance 不存在 | 404 |
| 16 | `GET /process-tree` Instance 已退出 | 404（当前实现进程退出后从内存删除） |
| 17 | `GET /resources` 运行中 Instance | 200 + processes 数组非空 |
| 18 | `GET /resources?includeChildren=false` | processes 仅含 1 个（根进程） |
| 19 | `GET /resources` summary 字段完整 | totalProcesses/totalCpuPercent 等存在 |

### 6.2 验收标准

- ProcessMonitor 在 Linux 上正确采集进程树和资源信息
- 进程树 API 返回完整树结构 + 汇总统计
- 资源 API 返回平坦列表 + 汇总统计
- CPU 采样机制工作正常
- Windows/macOS 有存根实现（不崩溃，返回空数据）
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：`/proc` 文件系统读取权限不足（容器环境）
  - 控制：读取 `/proc/{pid}/stat` 只需对进程有读权限，通常同用户进程无问题；测试中验证权限
- **风险 2**：进程枚举遍历 `/proc` 性能问题
  - 控制：优先使用 `/proc/{pid}/task/{pid}/children`（内核 ≥ 3.5），直接返回子进程列表无需遍历；仅在该文件不存在时回退到全量扫描。Instance 通常子进程较少，性能影响可控
- **风险 3**：CPU 采样缓存无限增长
  - 控制：`cleanupSamples()` 在每次 getProcessTree/getProcessFamily 调用后清理已退出进程的缓存
- **风险 4**：跨平台差异导致数据语义不一致
  - 控制：首版聚焦 Linux 实现，Windows/macOS 提供存根；数据语义在 API 文档中明确说明

---

## 8. 里程碑完成定义（DoD）

- `ProcessMonitor` 类实现（Linux 完整 + Windows/macOS 存根）
- `ProcessInfo`/`ProcessTreeNode`/`ProcessTreeSummary` 数据模型定义
- `GET /api/instances/{id}/process-tree` 实现
- `GET /api/instances/{id}/resources` 实现
- CPU 采样机制工作正常
- 对应单元测试完成并通过
- 本里程碑文档入库
