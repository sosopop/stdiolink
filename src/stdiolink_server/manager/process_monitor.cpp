#include "process_monitor.h"

#include <QDateTime>
#include <QTimeZone>

#ifdef Q_OS_MACOS
#include <libproc.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef Q_OS_LINUX
#include <QDir>
#include <QFile>
#include <unistd.h>
#endif

namespace stdiolink_server {

// ── Cross-platform logic ────────────────────────────────────────────

ProcessTreeNode ProcessMonitor::getProcessTree(qint64 rootPid) {
    QSet<qint64> visited;
    auto tree = buildTree(rootPid, visited);

    // Cleanup stale CPU samples
    cleanupSamples(visited);

    return tree;
}

ProcessInfo ProcessMonitor::getProcessInfo(qint64 pid) {
    return readProcessInfo(pid);
}

QVector<ProcessInfo> ProcessMonitor::getProcessFamily(qint64 rootPid, bool includeChildren) {
    QVector<ProcessInfo> result;
    QSet<qint64> visited;

    auto rootInfo = readProcessInfo(rootPid);
    if (!rootInfo.isValid())
        return result;

    result.append(rootInfo);
    visited.insert(rootPid);

    if (includeChildren)
        collectDescendants(rootPid, result, visited);

    cleanupSamples(visited);
    return result;
}

ProcessTreeSummary ProcessMonitor::summarize(const ProcessTreeNode& tree) {
    ProcessTreeSummary s;
    s.totalProcesses = 1;
    s.totalCpuPercent = tree.info.cpuPercent;
    s.totalMemoryRssBytes = tree.info.memoryRssBytes;
    s.totalThreads = tree.info.threadCount;

    for (const auto& child : tree.children) {
        auto cs = summarize(child);
        s.totalProcesses += cs.totalProcesses;
        s.totalCpuPercent += cs.totalCpuPercent;
        s.totalMemoryRssBytes += cs.totalMemoryRssBytes;
        s.totalThreads += cs.totalThreads;
    }
    return s;
}

ProcessTreeSummary ProcessMonitor::summarize(const QVector<ProcessInfo>& processes) {
    ProcessTreeSummary s;
    s.totalProcesses = processes.size();
    for (const auto& p : processes) {
        s.totalCpuPercent += p.cpuPercent;
        s.totalMemoryRssBytes += p.memoryRssBytes;
        s.totalThreads += p.threadCount;
    }
    return s;
}

double ProcessMonitor::calculateCpuPercent(qint64 pid, qint64 currentCpuTimeMs) {
    auto now = QDateTime::currentDateTimeUtc();

    auto it = m_cpuSamples.find(pid);
    if (it == m_cpuSamples.end()) {
        // First sample — store and return 0
        m_cpuSamples[pid] = {currentCpuTimeMs, now};
        return 0.0;
    }

    qint64 wallDeltaMs = it->timestamp.msecsTo(now);
    if (wallDeltaMs <= 0) {
        it->cpuTimeMs = currentCpuTimeMs;
        it->timestamp = now;
        return 0.0;
    }

    qint64 cpuDeltaMs = currentCpuTimeMs - it->cpuTimeMs;
    double cpuPercent = (static_cast<double>(cpuDeltaMs) / wallDeltaMs) * 100.0;

    // Update sample
    it->cpuTimeMs = currentCpuTimeMs;
    it->timestamp = now;

    return qMax(0.0, cpuPercent);
}

void ProcessMonitor::cleanupSamples(const QSet<qint64>& alivePids) {
    auto it = m_cpuSamples.begin();
    while (it != m_cpuSamples.end()) {
        if (!alivePids.contains(it.key()))
            it = m_cpuSamples.erase(it);
        else
            ++it;
    }
}

ProcessTreeNode ProcessMonitor::buildTree(qint64 pid, QSet<qint64>& visited) {
    ProcessTreeNode node;
    node.info = readProcessInfo(pid);
    visited.insert(pid);

    if (!node.info.isValid())
        return node;

    auto childPids = getChildPids(pid);
    for (qint64 childPid : childPids) {
        if (visited.contains(childPid))
            continue;
        node.children.append(buildTree(childPid, visited));
    }
    return node;
}

void ProcessMonitor::collectDescendants(qint64 pid, QVector<ProcessInfo>& out,
                                        QSet<qint64>& visited) {
    auto childPids = getChildPids(pid);
    for (qint64 childPid : childPids) {
        if (visited.contains(childPid))
            continue;
        visited.insert(childPid);
        auto info = readProcessInfo(childPid);
        if (info.isValid()) {
            out.append(info);
            collectDescendants(childPid, out, visited);
        }
    }
}

// ── macOS implementation ────────────────────────────────────────────

#ifdef Q_OS_MACOS

static QDateTime macosBootTime() {
    struct timeval boottime;
    size_t len = sizeof(boottime);
    int mib[2] = {CTL_KERN, KERN_BOOTTIME};
    if (sysctl(mib, 2, &boottime, &len, nullptr, 0) == 0) {
        return QDateTime::fromSecsSinceEpoch(boottime.tv_sec, QTimeZone::utc());
    }
    return {};
}

static QString macosProcessCommandLine(pid_t pid) {
    // Use sysctl KERN_PROCARGS2 to get the full command line
    int mib[3] = {CTL_KERN, KERN_PROCARGS2, pid};
    size_t size = 0;

    // First call to get size
    if (sysctl(mib, 3, nullptr, &size, nullptr, 0) != 0)
        return {};

    if (size == 0 || size > 1024 * 1024) // sanity limit
        return {};

    QByteArray buf(static_cast<int>(size), '\0');
    if (sysctl(mib, 3, buf.data(), &size, nullptr, 0) != 0)
        return {};

    // First 4 bytes = argc
    if (size < sizeof(int))
        return {};

    int argc = 0;
    memcpy(&argc, buf.constData(), sizeof(int));

    // Skip past argc, then skip the exec_path (null-terminated), then skip padding nulls
    const char* ptr = buf.constData() + sizeof(int);
    const char* end = buf.constData() + size;

    // Skip exec_path
    while (ptr < end && *ptr != '\0')
        ++ptr;
    // Skip trailing nulls after exec_path
    while (ptr < end && *ptr == '\0')
        ++ptr;

    // Now collect argc arguments (null-separated)
    QStringList args;
    for (int i = 0; i < argc && ptr < end; ++i) {
        QString arg = QString::fromUtf8(ptr);
        args.append(arg);
        ptr += strlen(ptr) + 1;
    }

    return args.join(' ');
}

ProcessInfo ProcessMonitor::readProcessInfo(qint64 pid) {
    ProcessInfo info;
    info.pid = pid;

    struct proc_taskinfo ti;
    int ret = proc_pidinfo(static_cast<int>(pid), PROC_PIDTASKINFO, 0, &ti, sizeof(ti));
    if (ret != sizeof(ti)) {
        // Process doesn't exist or no permission
        return info;
    }

    // Name from proc_name
    char nameBuf[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_name(static_cast<int>(pid), nameBuf, sizeof(nameBuf)) > 0) {
        info.name = QString::fromUtf8(nameBuf);
    }

    // Command line
    info.commandLine = macosProcessCommandLine(static_cast<pid_t>(pid));

    // Parent PID via proc_pidinfo with PROC_PIDTBSDINFO
    struct proc_bsdinfo bsdinfo;
    if (proc_pidinfo(static_cast<int>(pid), PROC_PIDTBSDINFO, 0, &bsdinfo, sizeof(bsdinfo)) ==
        static_cast<int>(sizeof(bsdinfo))) {
        info.parentPid = bsdinfo.pbi_ppid;

        // Process status
        switch (bsdinfo.pbi_status) {
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
        if (bsdinfo.pbi_start_tvsec > 0) {
            info.startedAt = QDateTime::fromSecsSinceEpoch(
                static_cast<qint64>(bsdinfo.pbi_start_tvsec), QTimeZone::utc());
            info.uptimeSeconds = QDateTime::currentDateTimeUtc().toSecsSinceEpoch() -
                                 static_cast<qint64>(bsdinfo.pbi_start_tvsec);
        }
    }

    // Memory
    info.memoryRssBytes = static_cast<qint64>(ti.pti_resident_size);
    info.memoryVmsBytes = static_cast<qint64>(ti.pti_virtual_size);

    // Threads
    info.threadCount = static_cast<int>(ti.pti_threadnum);

    // CPU time (user + system, in nanoseconds → milliseconds)
    qint64 cpuTimeMs = static_cast<qint64>((ti.pti_total_user + ti.pti_total_system) / 1000000ULL);
    info.cpuPercent = calculateCpuPercent(pid, cpuTimeMs);

    return info;
}

QVector<qint64> ProcessMonitor::getChildPids(qint64 pid) {
    QVector<qint64> children;

    // Use sysctl KERN_PROC_ALL to enumerate all processes, then filter by ppid.
    // proc_listchildpids() is unreliable on macOS (returns 0 children in practice).
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
    size_t size = 0;
    if (sysctl(mib, 4, nullptr, &size, nullptr, 0) != 0)
        return children;

    QByteArray buf(static_cast<int>(size), '\0');
    if (sysctl(mib, 4, buf.data(), &size, nullptr, 0) != 0)
        return children;

    int count = static_cast<int>(size / sizeof(struct kinfo_proc));
    auto* procs = reinterpret_cast<struct kinfo_proc*>(buf.data());

    for (int i = 0; i < count; ++i) {
        if (static_cast<qint64>(procs[i].kp_eproc.e_ppid) == pid) {
            children.append(static_cast<qint64>(procs[i].kp_proc.p_pid));
        }
    }
    return children;
}

#endif // Q_OS_MACOS

// ── Linux implementation ────────────────────────────────────────────

#ifdef Q_OS_LINUX

static long getClockTicksPerSec() {
    static long ticks = sysconf(_SC_CLK_TCK);
    return ticks > 0 ? ticks : 100;
}

static long getPageSize() {
    static long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? ps : 4096;
}

static QDateTime linuxBootTime() {
    QFile f("/proc/stat");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    while (!f.atEnd()) {
        QByteArray line = f.readLine();
        if (line.startsWith("btime ")) {
            bool ok;
            qint64 btime = line.mid(6).trimmed().toLongLong(&ok);
            if (ok)
                return QDateTime::fromSecsSinceEpoch(btime, QTimeZone::utc());
        }
    }
    return {};
}

ProcessInfo ProcessMonitor::readProcessInfo(qint64 pid) {
    ProcessInfo info;
    info.pid = pid;

    // Read /proc/{pid}/stat
    QFile statFile(QString("/proc/%1/stat").arg(pid));
    if (!statFile.open(QIODevice::ReadOnly))
        return info;

    QByteArray statData = statFile.readAll();
    statFile.close();

    // Parse: find last ')' to handle comm with spaces/parens
    int lastParen = statData.lastIndexOf(')');
    if (lastParen < 0)
        return info;

    QByteArray afterComm = statData.mid(lastParen + 2); // skip ") "
    QList<QByteArray> fields = afterComm.split(' ');
    // fields[0] = state, fields[1] = ppid, ...
    // Indices relative to afterComm: state=0, ppid=1, ...
    // stat field numbers: state=3, ppid=4, utime=14, stime=15, num_threads=20, starttime=22,
    // vsize=23, rss=24 After removing pid and comm: state=0, ppid=1, utime=11, stime=12,
    // num_threads=17, starttime=19, vsize=20, rss=21

    if (fields.size() < 22)
        return info;

    // State
    char stateChar = fields[0].isEmpty() ? '?' : fields[0][0];
    switch (stateChar) {
    case 'R':
        info.status = "running";
        break;
    case 'S':
    case 'D':
        info.status = "sleeping";
        break;
    case 'Z':
        info.status = "zombie";
        break;
    case 'T':
    case 't':
        info.status = "stopped";
        break;
    default:
        info.status = "unknown";
        break;
    }

    info.parentPid = fields[1].toLongLong();

    // CPU time
    long ticks = getClockTicksPerSec();
    qint64 utime = fields[11].toLongLong();
    qint64 stime = fields[12].toLongLong();
    qint64 cpuTimeMs = ((utime + stime) * 1000) / ticks;
    info.cpuPercent = calculateCpuPercent(pid, cpuTimeMs);

    info.threadCount = fields[17].toInt();

    // Start time
    qint64 startTicks = fields[19].toLongLong();
    static QDateTime bootTime = linuxBootTime();
    if (bootTime.isValid()) {
        qint64 startSecs = bootTime.toSecsSinceEpoch() + startTicks / ticks;
        info.startedAt = QDateTime::fromSecsSinceEpoch(startSecs, QTimeZone::utc());
        info.uptimeSeconds = QDateTime::currentDateTimeUtc().toSecsSinceEpoch() - startSecs;
    }

    info.memoryVmsBytes = fields[20].toLongLong();
    info.memoryRssBytes = fields[21].toLongLong() * getPageSize();

    // Name from /proc/{pid}/comm
    QFile commFile(QString("/proc/%1/comm").arg(pid));
    if (commFile.open(QIODevice::ReadOnly))
        info.name = QString::fromUtf8(commFile.readAll()).trimmed();

    // Command line from /proc/{pid}/cmdline
    QFile cmdFile(QString("/proc/%1/cmdline").arg(pid));
    if (cmdFile.open(QIODevice::ReadOnly)) {
        QByteArray cmdData = cmdFile.readAll();
        info.commandLine = QString::fromUtf8(cmdData.replace('\0', ' ')).trimmed();
    }

    // I/O from /proc/{pid}/io (may require same-user or root)
    QFile ioFile(QString("/proc/%1/io").arg(pid));
    if (ioFile.open(QIODevice::ReadOnly)) {
        while (!ioFile.atEnd()) {
            QByteArray line = ioFile.readLine().trimmed();
            if (line.startsWith("read_bytes:"))
                info.ioReadBytes = line.mid(12).trimmed().toLongLong();
            else if (line.startsWith("write_bytes:"))
                info.ioWriteBytes = line.mid(13).trimmed().toLongLong();
        }
    }

    return info;
}

QVector<qint64> ProcessMonitor::getChildPids(qint64 pid) {
    QVector<qint64> children;

    // Try /proc/{pid}/task/{pid}/children first (kernel >= 3.5)
    QFile childrenFile(QString("/proc/%1/task/%1/children").arg(pid));
    if (childrenFile.open(QIODevice::ReadOnly)) {
        QString data = QString::fromUtf8(childrenFile.readAll()).trimmed();
        if (!data.isEmpty()) {
            for (const auto& token : data.split(' ', Qt::SkipEmptyParts)) {
                bool ok;
                qint64 childPid = token.toLongLong(&ok);
                if (ok)
                    children.append(childPid);
            }
        }
        return children;
    }

    // Fallback: scan /proc/*/stat for matching ppid
    QDir procDir("/proc");
    for (const auto& entry : procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok;
        qint64 childPid = entry.toLongLong(&ok);
        if (!ok)
            continue;

        QFile statFile(QString("/proc/%1/stat").arg(childPid));
        if (!statFile.open(QIODevice::ReadOnly))
            continue;

        QByteArray data = statFile.readAll();
        int lastParen = data.lastIndexOf(')');
        if (lastParen < 0)
            continue;

        QList<QByteArray> fields = data.mid(lastParen + 2).split(' ');
        if (fields.size() > 1 && fields[1].toLongLong() == pid)
            children.append(childPid);
    }
    return children;
}

#endif // Q_OS_LINUX

// ── Windows stub ────────────────────────────────────────────────────

#ifdef Q_OS_WIN

ProcessInfo ProcessMonitor::readProcessInfo(qint64 pid) {
    qWarning("ProcessMonitor: not fully implemented on Windows");
    ProcessInfo info;
    info.pid = pid;
    info.name = "unknown";
    info.status = "unknown";
    return info;
}

QVector<qint64> ProcessMonitor::getChildPids(qint64 pid) {
    Q_UNUSED(pid);
    qWarning("ProcessMonitor: getChildPids not implemented on Windows");
    return {};
}

#endif // Q_OS_WIN

} // namespace stdiolink_server
