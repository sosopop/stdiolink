#pragma once

#include <QDateTime>
#include <QProcess>
#include <QString>
#include <QTemporaryFile>

#include <memory>

#include "stdiolink/guard/process_guard_server.h"

namespace stdiolink_server {

struct Instance {
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

    std::unique_ptr<QTemporaryFile> tempConfigFile;
    std::unique_ptr<stdiolink::ProcessGuardServer> guard;

    bool startFailedEmitted = false;  // 防止 startFailed 事件重复发射
};

} // namespace stdiolink_server
