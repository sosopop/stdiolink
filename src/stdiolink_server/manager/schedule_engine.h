#pragma once

#include <QHash>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>

#include "manager/instance_manager.h"
#include "model/project.h"
#include "scanner/service_scanner.h"

class QTimer;

namespace stdiolink_server {

class ScheduleEngine : public QObject {
    Q_OBJECT
public:
    struct ProjectRuntimeState {
        bool shuttingDown = false;
        bool restartSuppressed = false;
        bool timerActive = false;
        int consecutiveFailures = 0;
    };

    explicit ScheduleEngine(InstanceManager* instanceMgr,
                            QObject* parent = nullptr);

    void startAll(const QMap<QString, Project>& projects,
                  const QMap<QString, ServiceInfo>& services);
    void startProject(const Project& project,
                      const QMap<QString, ServiceInfo>& services);

    void stopAll();
    void stopProject(const QString& projectId);
    void resumeProject(const QString& projectId);
    ProjectRuntimeState projectRuntimeState(const QString& projectId) const;

    void setShuttingDown(bool value) { m_shuttingDown = value; }
    bool isShuttingDown() const { return m_shuttingDown; }

signals:
    void scheduleTriggered(const QString& projectId, const QString& scheduleType);
    void scheduleSuppressed(const QString& projectId, const QString& reason,
                            int consecutiveFailures);

private slots:
    void onInstanceFinished(const QString& instanceId,
                            const QString& projectId,
                            int exitCode,
                            QProcess::ExitStatus exitStatus);

private:
    void startDaemon(const Project& project,
                     const QString& serviceDir);
    void startFixedRate(const Project& project,
                        const QString& serviceDir);

    InstanceManager* m_instanceMgr = nullptr;
    QMap<QString, ServiceInfo> m_services;
    QMap<QString, Project> m_projects;
    QHash<QString, QTimer*> m_timers;
    QHash<QString, int> m_consecutiveFailures;
    QSet<QString> m_restartSuppressed;
    bool m_shuttingDown = false;
};

} // namespace stdiolink_server
