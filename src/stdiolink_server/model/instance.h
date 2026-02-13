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
    QString status;

    // Extended fields (M50)
    QString workingDirectory;
    QString logPath;
    QStringList commandLine;

    std::unique_ptr<QTemporaryFile> tempConfigFile;
};

} // namespace stdiolink_server
