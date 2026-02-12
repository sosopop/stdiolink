#pragma once

#include "model/process_info.h"

#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QVector>

namespace stdiolink_server {

class ProcessMonitor {
public:
    ProcessMonitor() = default;

    /// Get the full process tree rooted at rootPid (with resource info)
    ProcessTreeNode getProcessTree(qint64 rootPid);

    /// Get info for a single process
    ProcessInfo getProcessInfo(qint64 pid);

    /// Get a flat list of the process and all its descendants
    QVector<ProcessInfo> getProcessFamily(qint64 rootPid,
                                           bool includeChildren = true);

    /// Compute summary statistics from a tree
    static ProcessTreeSummary summarize(const ProcessTreeNode& tree);

    /// Compute summary statistics from a flat list
    static ProcessTreeSummary summarize(const QVector<ProcessInfo>& processes);

private:
    /// Get child PIDs of a process (platform-specific)
    QVector<qint64> getChildPids(qint64 pid);

    /// Read raw process info (platform-specific)
    ProcessInfo readProcessInfo(qint64 pid);

    /// CPU sample cache
    struct CpuSample {
        qint64 cpuTimeMs = 0;
        QDateTime timestamp;
    };
    QMap<qint64, CpuSample> m_cpuSamples;

    /// Calculate CPU% from two samples
    double calculateCpuPercent(qint64 pid, qint64 currentCpuTimeMs);

    /// Remove stale entries from the CPU sample cache
    void cleanupSamples(const QSet<qint64>& alivePids);

    /// Build tree recursively
    ProcessTreeNode buildTree(qint64 pid, QSet<qint64>& visited);

    /// Collect all descendants into a flat list
    void collectFamily(qint64 pid, QVector<ProcessInfo>& out, QSet<qint64>& visited);
};

} // namespace stdiolink_server
