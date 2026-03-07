#pragma once

#include <QDateTime>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QTemporaryFile>

#include <memory>

#include "stdiolink/guard/process_guard_server.h"

namespace stdiolink_server {

class InstanceLogWriter;

struct Instance {
    ~Instance();

    QString id;
    QString projectId;
    QString serviceId;

    QProcess* process = nullptr;
    QDateTime startedAt;
    qint64 pid = 0;
    QString status;

    QString workingDirectory;
    QString logPath;
    QStringList commandLine;
    int runTimeoutMs = 0;
    bool timedOut = false;
    QString timeoutReason;

    std::unique_ptr<QTemporaryFile> tempConfigFile;
    std::unique_ptr<QTimer> runTimeoutTimer;
    std::unique_ptr<stdiolink::ProcessGuardServer> guard;
    std::unique_ptr<InstanceLogWriter> logWriter;

    bool startFailedEmitted = false;  // 防止 startFailed 事件重复发射
};

} // namespace stdiolink_server
