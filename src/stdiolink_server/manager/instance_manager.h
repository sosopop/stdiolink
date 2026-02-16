#pragma once

#include <QObject>
#include <QString>

#include <map>
#include <memory>

#include "config/server_config.h"
#include "model/instance.h"
#include "model/project.h"

namespace stdiolink_server {

class InstanceManager : public QObject {
    Q_OBJECT
public:
    explicit InstanceManager(const QString& dataRoot,
                             const ServerConfig& config,
                             QObject* parent = nullptr);

    QString startInstance(const Project& project,
                          const QString& serviceDir,
                          QString& error);

    void terminateInstance(const QString& instanceId);
    void terminateByProject(const QString& projectId);
    void terminateAll();

    void waitAllFinished(int graceTimeoutMs = 5000);

    QList<const Instance*> getInstances(const QString& projectId = QString()) const;
    const Instance* getInstance(const QString& instanceId) const;
    int instanceCount(const QString& projectId = QString()) const;

    QString findServiceProgram() const;

signals:
    void instanceStarted(const QString& instanceId,
                         const QString& projectId);
    void instanceStartFailed(const QString& instanceId,
                             const QString& projectId,
                             const QString& error);
    void instanceFinished(const QString& instanceId,
                          const QString& projectId,
                          int exitCode,
                          QProcess::ExitStatus exitStatus);

public:
    void setGuardNameForTesting(const QString& name) { m_guardNameOverride = name; }

private:
    QString generateInstanceId() const;
    void onProcessFinished(const QString& instanceId,
                           int exitCode,
                           QProcess::ExitStatus status);

    QString m_dataRoot;
    ServerConfig m_config;
    std::map<QString, std::unique_ptr<Instance>> m_instances;
    QString m_guardNameOverride;
};

} // namespace stdiolink_server
