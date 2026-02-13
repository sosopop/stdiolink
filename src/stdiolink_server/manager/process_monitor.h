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

    /// Get the full process tree rooted at the given PID (with resource info)
    ProcessTreeNode getProcessTree(qint64 rootPid);

    /// Get a single process's info
    ProcessInfo getProcessInfo(qint64 pid);

    /// Get a flat list of the process and optionally all descendants
    QVector<ProcessInfo> getProcessFamily(qint64 rootPid, bool includeChildren = true);

    /// Compute summary statistics from a tree
    static ProcessTreeSummary summarize(const ProcessTreeNode& tree);

    /// Compute summary statistics from a flat list
    static ProcessTreeSummary summarize(const QVector<ProcessInfo>& processes);

private:
    /// Platform-specific: get direct child PIDs
    QVector<qint64> getChildPids(qint64 pid);

    /// Platform-specific: read raw process info
    ProcessInfo readProcessInfo(qint64 pid);

    /// CPU sampling cache
    struct CpuSample {
        qint64 cpuTimeMs = 0;
        QDateTime timestamp;
    };
    QMap<qint64, CpuSample> m_cpuSamples;

    /// Calculate CPU% from two samples
    double calculateCpuPercent(qint64 pid, qint64 currentCpuTimeMs);

    /// Remove stale entries from the CPU sample cache
    void cleanupSamples(const QSet<qint64>& alivePids);

    /// Recursive helper for building the tree
    ProcessTreeNode buildTree(qint64 pid, QSet<qint64>& visited);

    /// Recursive helper for collecting flat descendants
    void collectDescendants(qint64 pid, QVector<ProcessInfo>& out, QSet<qint64>& visited);
};

} // namespace stdiolink_server
