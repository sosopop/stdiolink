#include "schedule_engine.h"

#include <QTimer>

namespace stdiolink_server {

ScheduleEngine::ScheduleEngine(InstanceManager* instanceMgr,
                               QObject* parent)
    : QObject(parent)
    , m_instanceMgr(instanceMgr) {
    connect(m_instanceMgr,
            &InstanceManager::instanceFinished,
            this,
            &ScheduleEngine::onInstanceFinished);
}

void ScheduleEngine::startAll(const QMap<QString, Project>& projects,
                              const QMap<QString, ServiceInfo>& services) {
    stopAll();
    m_projects = projects;
    m_services = services;

    for (auto it = m_projects.begin(); it != m_projects.end(); ++it) {
        const Project& project = it.value();
        if (!project.enabled || !project.valid) {
            continue;
        }

        auto svcIt = m_services.find(project.serviceId);
        if (svcIt == m_services.end()) {
            continue;
        }
        const QString serviceDir = svcIt.value().serviceDir;

        switch (project.schedule.type) {
        case ScheduleType::Manual:
            break;
        case ScheduleType::FixedRate:
            startFixedRate(project, serviceDir);
            break;
        case ScheduleType::Daemon:
            startDaemon(project, serviceDir);
            break;
        }
    }
}

void ScheduleEngine::startDaemon(const Project& project,
                                 const QString& serviceDir) {
    if (m_shuttingDown || m_restartSuppressed.contains(project.id)) {
        return;
    }

    if (m_instanceMgr->instanceCount(project.id) > 0) {
        return;
    }

    QString error;
    (void)m_instanceMgr->startInstance(project, serviceDir, error);
    if (!error.isEmpty()) {
        qWarning("ScheduleEngine: daemon start failed for %s: %s",
                 qUtf8Printable(project.id),
                 qUtf8Printable(error));
    }
}

void ScheduleEngine::startFixedRate(const Project& project,
                                    const QString& serviceDir) {
    auto* timer = new QTimer(this);
    timer->setInterval(project.schedule.intervalMs);

    connect(timer, &QTimer::timeout, this, [this, projectId = project.id, serviceDir]() {
        if (m_shuttingDown) {
            return;
        }

        auto projectIt = m_projects.find(projectId);
        if (projectIt == m_projects.end()) {
            return;
        }

        const Project& project = projectIt.value();
        if (!project.enabled || !project.valid || project.schedule.type != ScheduleType::FixedRate) {
            return;
        }

        if (m_instanceMgr->instanceCount(projectId) >= project.schedule.maxConcurrent) {
            return;
        }

        QString error;
        (void)m_instanceMgr->startInstance(project, serviceDir, error);
        if (!error.isEmpty()) {
            qWarning("ScheduleEngine: fixed_rate trigger failed for %s: %s",
                     qUtf8Printable(projectId),
                     qUtf8Printable(error));
        }
    });

    m_timers.insert(project.id, timer);
    timer->start();
}

void ScheduleEngine::stopAll() {
    for (auto it = m_timers.begin(); it != m_timers.end(); ++it) {
        it.value()->stop();
        it.value()->deleteLater();
    }
    m_timers.clear();
    m_consecutiveFailures.clear();
    m_restartSuppressed.clear();
}

void ScheduleEngine::stopProject(const QString& projectId) {
    auto timerIt = m_timers.find(projectId);
    if (timerIt != m_timers.end()) {
        timerIt.value()->stop();
        timerIt.value()->deleteLater();
        m_timers.erase(timerIt);
    }

    m_restartSuppressed.insert(projectId);
    m_consecutiveFailures.remove(projectId);
}

void ScheduleEngine::resumeProject(const QString& projectId) {
    m_restartSuppressed.remove(projectId);
    m_consecutiveFailures.remove(projectId);
}

ScheduleEngine::ProjectRuntimeState ScheduleEngine::projectRuntimeState(const QString& projectId) const {
    ProjectRuntimeState state;
    state.shuttingDown = m_shuttingDown;
    state.restartSuppressed = m_restartSuppressed.contains(projectId);
    state.timerActive = m_timers.contains(projectId);
    state.consecutiveFailures = m_consecutiveFailures.value(projectId, 0);
    return state;
}

void ScheduleEngine::onInstanceFinished(const QString& instanceId,
                                        const QString& projectId,
                                        int exitCode,
                                        QProcess::ExitStatus exitStatus) {
    Q_UNUSED(instanceId);

    if (m_shuttingDown || m_restartSuppressed.contains(projectId)) {
        return;
    }

    auto it = m_projects.find(projectId);
    if (it == m_projects.end()) {
        return;
    }

    const Project& project = it.value();
    if (!project.enabled || !project.valid || project.schedule.type != ScheduleType::Daemon) {
        return;
    }

    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        m_consecutiveFailures.remove(projectId);
        return;
    }

    int failures = m_consecutiveFailures.value(projectId, 0) + 1;
    m_consecutiveFailures.insert(projectId, failures);

    if (failures >= project.schedule.maxConsecutiveFailures) {
        m_restartSuppressed.insert(projectId);
        qWarning("ScheduleEngine: daemon project %s entered crash loop (%d)",
                 qUtf8Printable(projectId),
                 failures);
        return;
    }

    const auto serviceIt = m_services.find(project.serviceId);
    if (serviceIt == m_services.end()) {
        return;
    }

    const QString serviceDir = serviceIt.value().serviceDir;
    QTimer::singleShot(project.schedule.restartDelayMs,
                       this,
                       [this, projectId, serviceDir]() {
                           if (m_shuttingDown || m_restartSuppressed.contains(projectId)) {
                               return;
                           }
                           auto pIt = m_projects.find(projectId);
                           if (pIt == m_projects.end()) {
                               return;
                           }
                           startDaemon(pIt.value(), serviceDir);
                       });
}

} // namespace stdiolink_server
