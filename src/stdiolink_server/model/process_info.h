#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace stdiolink_server {

struct ProcessInfo {
    qint64 pid = 0;
    qint64 parentPid = 0;
    QString name;
    QString commandLine;
    QString status; // "running" / "sleeping" / "zombie" / "stopped" / "unknown"
    QDateTime startedAt;

    double cpuPercent = 0.0;
    qint64 memoryRssBytes = 0;
    qint64 memoryVmsBytes = 0;
    int threadCount = 0;
    qint64 uptimeSeconds = 0;
    qint64 ioReadBytes = 0;
    qint64 ioWriteBytes = 0;

    bool isValid() const { return pid > 0 && !name.isEmpty(); }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["pid"] = pid;
        obj["parentPid"] = parentPid;
        obj["name"] = name;
        obj["commandLine"] = commandLine;
        obj["status"] = status;
        if (startedAt.isValid())
            obj["startedAt"] = startedAt.toString(Qt::ISODate);

        QJsonObject res;
        res["cpuPercent"] = cpuPercent;
        res["memoryRssBytes"] = memoryRssBytes;
        res["memoryVmsBytes"] = memoryVmsBytes;
        res["threadCount"] = threadCount;
        res["uptimeSeconds"] = uptimeSeconds;
        res["ioReadBytes"] = ioReadBytes;
        res["ioWriteBytes"] = ioWriteBytes;
        obj["resources"] = res;

        return obj;
    }

    /// Flat resource-only JSON (for /resources endpoint)
    QJsonObject toResourceJson() const {
        QJsonObject obj;
        obj["pid"] = pid;
        obj["name"] = name;
        obj["cpuPercent"] = cpuPercent;
        obj["memoryRssBytes"] = memoryRssBytes;
        obj["threadCount"] = threadCount;
        obj["uptimeSeconds"] = uptimeSeconds;
        obj["ioReadBytes"] = ioReadBytes;
        obj["ioWriteBytes"] = ioWriteBytes;
        return obj;
    }
};

struct ProcessTreeNode {
    ProcessInfo info;
    QVector<ProcessTreeNode> children;

    QJsonObject toJson() const {
        QJsonObject obj = info.toJson();
        QJsonArray childArr;
        for (const auto& child : children)
            childArr.append(child.toJson());
        obj["children"] = childArr;
        return obj;
    }
};

struct ProcessTreeSummary {
    int totalProcesses = 0;
    double totalCpuPercent = 0.0;
    qint64 totalMemoryRssBytes = 0;
    int totalThreads = 0;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["totalProcesses"] = totalProcesses;
        obj["totalCpuPercent"] = totalCpuPercent;
        obj["totalMemoryRssBytes"] = totalMemoryRssBytes;
        obj["totalThreads"] = totalThreads;
        return obj;
    }
};

} // namespace stdiolink_server
