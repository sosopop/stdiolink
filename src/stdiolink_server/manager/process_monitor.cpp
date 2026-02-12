#include "process_monitor.h"

#include <QTimeZone>

#ifdef Q_OS_MACOS
#include <libproc.h>
#include <sys/sysctl.h>
#include <mach/mach_time.h>
#include <unistd.h>
#endif

#ifdef Q_OS_LINUX
#include <QDir>
#include <QFile>
#include <unistd.h>
#endif

namespace stdiolink_server {

// ---- Common logic ----

ProcessTreeNode ProcessMonitor::getProcessTree(qint64 rootPid) {
    QSet<qint64> visited;
    ProcessTreeNode tree = buildTree(rootPid, visited);

    // Cleanup stale CPU samples
    cleanupSamples(visited);

    return tree;
}

ProcessInfo ProcessMonitor::getProcessInfo(qint64 pid) {
    ProcessInfo info = readProcessInfo(pid);
    info.cpuPercent = calculateCpuPercent(pid, info.cpuPercent);
    return info;
}

QVector<ProcessInfo> ProcessMonitor::getProcessFamily(qint64 rootPid,
                                                       bool includeChildren) {
    QVector<ProcessInfo> result;
    QSet<qint64> visited;

    ProcessInfo root = readProcessInfo(rootPid);
    root.cpuPercent = calculateCpuPercent(rootPid, root.cpuPercent);
    result.append(root);
    visited.insert(rootPid);

    if (includeChildren) {
        collectFamily(rootPid, result, visited);
    }

    cleanupSamples(visited);
    return result;
}

ProcessTreeSummary ProcessMonitor::summarize(const ProcessTreeNode& tree) {
    ProcessTreeSummary summary;

    // Use a stack to avoid recursion
    QVector<const ProcessTreeNode*> stack;
    stack.append(&tree);

    while (!stack.isEmpty()) {
        const ProcessTreeNode* node = stack.takeLast();
        summary.totalProcesses++;
        summary.totalCpuPercent += node->info.cpuPercent;
        summary.totalMemoryRssBytes += node->info.memoryRssBytes;
        summary.totalThreads += node->info.threadCount;

        for (const auto& child : node->children) {
            stack.append(&child);
        }
    }

    return summary;
}

ProcessTreeSummary ProcessMonitor::summarize(const QVector<ProcessInfo>& processes) {
    ProcessTreeSummary summary;
    summary.totalProcesses = processes.size();
    for (const auto& p : processes) {
        summary.totalCpuPercent += p.cpuPercent;
        summary.totalMemoryRssBytes += p.memoryRssBytes;
        summary.totalThreads += p.threadCount;
    }
    return summary;
}

double ProcessMonitor::calculateCpuPercent(qint64 pid, qint64 currentCpuTimeMs) {
    const QDateTime now = QDateTime::currentDateTimeUtc();

    auto it = m_cpuSamples.find(pid);
    if (it == m_cpuSamples.end()) {
        // First sample: store and return 0
        m_cpuSamples.insert(pid, CpuSample{currentCpuTimeMs, now});
        return 0.0;
    }

    const qint64 cpuDelta = currentCpuTimeMs - it->cpuTimeMs;
    const qint64 wallDelta = it->timestamp.msecsTo(now);

    // Update sample
    it->cpuTimeMs = currentCpuTimeMs;
    it->timestamp = now;

    if (wallDelta <= 0) {
        return 0.0;
    }

    return (static_cast<double>(cpuDelta) / static_cast<double>(wallDelta)) * 100.0;
}

void ProcessMonitor::cleanupSamples(const QSet<qint64>& alivePids) {
    for (auto it = m_cpuSamples.begin(); it != m_cpuSamples.end();) {
        if (!alivePids.contains(it.key())) {
            it = m_cpuSamples.erase(it);
        } else {
            ++it;
        }
    }
}

ProcessTreeNode ProcessMonitor::buildTree(qint64 pid, QSet<qint64>& visited) {
    ProcessTreeNode node;
    node.info = readProcessInfo(pid);
    // Store raw CPU time in cpuPercent temporarily, then convert
    node.info.cpuPercent = calculateCpuPercent(pid, static_cast<qint64>(node.info.cpuPercent));
    visited.insert(pid);

    const QVector<qint64> children = getChildPids(pid);
    for (qint64 childPid : children) {
        if (!visited.contains(childPid)) {
            node.children.append(buildTree(childPid, visited));
        }
    }

    return node;
}

void ProcessMonitor::collectFamily(qint64 pid, QVector<ProcessInfo>& out,
                                    QSet<qint64>& visited) {
    const QVector<qint64> children = getChildPids(pid);
    for (qint64 childPid : children) {
        if (visited.contains(childPid)) {
            continue;
        }
        visited.insert(childPid);

        ProcessInfo info = readProcessInfo(childPid);
        info.cpuPercent = calculateCpuPercent(childPid, static_cast<qint64>(info.cpuPercent));
        out.append(info);

        collectFamily(childPid, out, visited);
    }
}

// ---- Platform-specific implementations ----

#if defined(Q_OS_MACOS)

ProcessInfo ProcessMonitor::readProcessInfo(qint64 pid) {
    ProcessInfo info;
    info.pid = pid;
    info.status = "unknown";

    struct proc_taskinfo taskInfo;
    const int ret = proc_pidinfo(static_cast<int>(pid), PROC_PIDTASKINFO, 0,
                                  &taskInfo, sizeof(taskInfo));
    if (ret <= 0) {
        return info;
    }

    info.memoryRssBytes = static_cast<qint64>(taskInfo.pti_resident_size);
    info.memoryVmsBytes = static_cast<qint64>(taskInfo.pti_virtual_size);
    info.threadCount = static_cast<int>(taskInfo.pti_threadnum);

    // CPU time in nanoseconds → store as milliseconds in cpuPercent field
    // (will be converted to percentage by calculateCpuPercent)
    const qint64 cpuTimeNs = static_cast<qint64>(taskInfo.pti_total_user + taskInfo.pti_total_system);
    info.cpuPercent = static_cast<double>(cpuTimeNs) / 1000000.0;  // ms, stored temporarily

    // Process name
    char nameBuf[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_name(static_cast<int>(pid), nameBuf, sizeof(nameBuf)) > 0) {
        info.name = QString::fromUtf8(nameBuf);
    }

    // Command line via sysctl
    int mib[3] = {CTL_KERN, KERN_PROCARGS2, static_cast<int>(pid)};
    size_t argmax = 0;
    if (sysctl(mib, 3, nullptr, &argmax, nullptr, 0) == 0 && argmax > 0) {
        QByteArray buf(static_cast<int>(argmax), '\0');
        if (sysctl(mib, 3, buf.data(), &argmax, nullptr, 0) == 0) {
            // First 4 bytes = argc, then executable path (NUL-terminated),
            // then args (NUL-separated)
            if (argmax > sizeof(int)) {
                const char* ptr = buf.constData() + sizeof(int);
                const char* end = buf.constData() + argmax;
                QStringList args;
                while (ptr < end) {
                    if (*ptr == '\0') {
                        ptr++;
                        continue;
                    }
                    const QString arg = QString::fromUtf8(ptr);
                    args.append(arg);
                    ptr += qstrlen(ptr) + 1;
                }
                info.commandLine = args.join(' ');
            }
        }
    }

    // Process status via proc_pidinfo with PROC_PIDTBSDINFO
    struct proc_bsdinfo bsdInfo;
    if (proc_pidinfo(static_cast<int>(pid), PROC_PIDTBSDINFO, 0,
                     &bsdInfo, sizeof(bsdInfo)) > 0) {
        info.parentPid = bsdInfo.pbi_ppid;
        switch (bsdInfo.pbi_status) {
        case SRUN:
            info.status = "running";
            break;
        case SSLEEP:
            info.status = "sleeping";
            break;
        case SSTOP:
            info.status = "stopped";
            break;
        case SZOMB:
            info.status = "zombie";
            break;
        default:
            info.status = "unknown";
            break;
        }

        // Start time
        info.startedAt = QDateTime::fromSecsSinceEpoch(bsdInfo.pbi_start_tvsec, QTimeZone::utc());
        if (info.startedAt.isValid()) {
            info.uptimeSeconds = info.startedAt.secsTo(QDateTime::currentDateTimeUtc());
        }
    }

    return info;
}

QVector<qint64> ProcessMonitor::getChildPids(qint64 pid) {
    QVector<qint64> children;

    // proc_listchildpids is unreliable on some macOS versions (e.g. Sequoia).
    // Use proc_listpids(PROC_PPID_ONLY) which correctly filters by parent PID.
    constexpr int kMaxChildren = 512;
    QVector<int> pids(kMaxChildren);
    const int actual = proc_listpids(PROC_PPID_ONLY,
                                      static_cast<uint32_t>(pid),
                                      pids.data(),
                                      kMaxChildren * static_cast<int>(sizeof(int)));
    if (actual <= 0) {
        return children;
    }

    const int numPids = actual / static_cast<int>(sizeof(int));
    for (int i = 0; i < numPids; ++i) {
        if (pids[i] > 0) {
            children.append(pids[i]);
        }
    }

    return children;
}

#elif defined(Q_OS_LINUX)

ProcessInfo ProcessMonitor::readProcessInfo(qint64 pid) {
    ProcessInfo info;
    info.pid = pid;
    info.status = "unknown";

    // Read /proc/{pid}/comm
    QFile commFile(QString("/proc/%1/comm").arg(pid));
    if (commFile.open(QIODevice::ReadOnly)) {
        info.name = QString::fromUtf8(commFile.readAll()).trimmed();
    }

    // Read /proc/{pid}/cmdline
    QFile cmdFile(QString("/proc/%1/cmdline").arg(pid));
    if (cmdFile.open(QIODevice::ReadOnly)) {
        QByteArray cmdData = cmdFile.readAll();
        info.commandLine = QString::fromUtf8(cmdData.replace('\0', ' ')).trimmed();
    }

    // Read /proc/{pid}/stat
    QFile statFile(QString("/proc/%1/stat").arg(pid));
    if (statFile.open(QIODevice::ReadOnly)) {
        const QByteArray statData = statFile.readAll();
        const QString statStr = QString::fromUtf8(statData);

        // Find the last ')' to safely skip the comm field (which can contain spaces/parens)
        const int lastParen = statStr.lastIndexOf(')');
        if (lastParen >= 0 && lastParen + 2 < statStr.size()) {
            const QStringList fields = statStr.mid(lastParen + 2).split(' ', Qt::SkipEmptyParts);
            // fields[0] = state (index 2 in original stat)
            // fields[1] = ppid (index 3)
            // fields[11] = utime (index 13)
            // fields[12] = stime (index 14)
            // fields[17] = num_threads (index 19)
            // fields[19] = starttime (index 21)
            // fields[20] = vsize (index 22)
            // fields[21] = rss (index 23)

            if (fields.size() > 0) {
                const QChar state = fields[0].isEmpty() ? QChar() : fields[0][0];
                if (state == 'R') info.status = "running";
                else if (state == 'S' || state == 'D') info.status = "sleeping";
                else if (state == 'Z') info.status = "zombie";
                else if (state == 'T') info.status = "stopped";
            }

            if (fields.size() > 1) {
                info.parentPid = fields[1].toLongLong();
            }

            if (fields.size() > 12) {
                const qint64 utime = fields[11].toLongLong();
                const qint64 stime = fields[12].toLongLong();
                const long ticksPerSec = sysconf(_SC_CLK_TCK);
                // Store CPU time in ms via cpuPercent (temporary)
                info.cpuPercent = static_cast<double>((utime + stime) * 1000 / ticksPerSec);
            }

            if (fields.size() > 17) {
                info.threadCount = fields[17].toInt();
            }

            if (fields.size() > 21) {
                info.memoryVmsBytes = fields[20].toLongLong();
                const long pageSize = sysconf(_SC_PAGESIZE);
                info.memoryRssBytes = fields[21].toLongLong() * pageSize;
            }

            if (fields.size() > 19) {
                // starttime is in clock ticks since boot
                const qint64 startTicks = fields[19].toLongLong();
                const long ticksPerSec = sysconf(_SC_CLK_TCK);

                // Get boot time
                QFile uptimeFile("/proc/uptime");
                if (uptimeFile.open(QIODevice::ReadOnly)) {
                    const double uptimeSecs = QString::fromUtf8(uptimeFile.readAll())
                                                  .split(' ').value(0).toDouble();
                    const double startSecs = static_cast<double>(startTicks) / ticksPerSec;
                    const double ageSecs = uptimeSecs - startSecs;
                    info.uptimeSeconds = static_cast<qint64>(ageSecs);
                    info.startedAt = QDateTime::currentDateTimeUtc().addSecs(-info.uptimeSeconds);
                }
            }
        }
    }

    // Read /proc/{pid}/io (may require same-user permission)
    QFile ioFile(QString("/proc/%1/io").arg(pid));
    if (ioFile.open(QIODevice::ReadOnly)) {
        const QString ioData = QString::fromUtf8(ioFile.readAll());
        for (const QString& line : ioData.split('\n')) {
            if (line.startsWith("read_bytes:")) {
                info.ioReadBytes = line.mid(12).trimmed().toLongLong();
            } else if (line.startsWith("write_bytes:")) {
                info.ioWriteBytes = line.mid(13).trimmed().toLongLong();
            }
        }
    }

    return info;
}

QVector<qint64> ProcessMonitor::getChildPids(qint64 pid) {
    QVector<qint64> children;

    // Try /proc/{pid}/task/{pid}/children first (kernel >= 3.5)
    QFile childrenFile(QString("/proc/%1/task/%1/children").arg(pid));
    if (childrenFile.open(QIODevice::ReadOnly)) {
        const QString data = QString::fromUtf8(childrenFile.readAll()).trimmed();
        if (!data.isEmpty()) {
            for (const auto& token : data.split(' ', Qt::SkipEmptyParts)) {
                bool ok;
                const qint64 childPid = token.toLongLong(&ok);
                if (ok) {
                    children.append(childPid);
                }
            }
        }
        return children;
    }

    // Fallback: scan /proc/*/stat for matching ppid
    QDir procDir("/proc");
    for (const QString& entry : procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok;
        const qint64 childPid = entry.toLongLong(&ok);
        if (!ok) {
            continue;
        }

        QFile statFile(QString("/proc/%1/stat").arg(childPid));
        if (!statFile.open(QIODevice::ReadOnly)) {
            continue;
        }

        const QString statStr = QString::fromUtf8(statFile.readAll());
        const int lastParen = statStr.lastIndexOf(')');
        if (lastParen < 0) {
            continue;
        }

        const QStringList fields = statStr.mid(lastParen + 2).split(' ', Qt::SkipEmptyParts);
        if (fields.size() > 1) {
            const qint64 ppid = fields[1].toLongLong();
            if (ppid == pid) {
                children.append(childPid);
            }
        }
    }

    return children;
}

#else
// Windows / other platforms: stub implementation

ProcessInfo ProcessMonitor::readProcessInfo(qint64 pid) {
    ProcessInfo info;
    info.pid = pid;
    info.name = "unknown";
    info.status = "unknown";
    qWarning("ProcessMonitor: readProcessInfo not implemented for this platform");
    return info;
}

QVector<qint64> ProcessMonitor::getChildPids(qint64 pid) {
    Q_UNUSED(pid);
    qWarning("ProcessMonitor: getChildPids not implemented for this platform");
    return {};
}

#endif

} // namespace stdiolink_server
