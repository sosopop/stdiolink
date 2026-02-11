#include "server_manager.h"

#include <QDir>

namespace stdiolink_server {

ServerManager::ServerManager(const QString& dataRoot,
                             const ServerConfig& config,
                             QObject* parent)
    : QObject(parent)
    , m_dataRoot(dataRoot)
    , m_config(config) {
    m_instanceManager = new InstanceManager(dataRoot, config, this);
    m_scheduleEngine = new ScheduleEngine(m_instanceManager, this);
}

bool ServerManager::initialize(QString& error) {
    QDir root(m_dataRoot);
    if (!root.exists()) {
        error = "data root does not exist: " + m_dataRoot;
        return false;
    }

    ServiceScanner::ScanStats svcStats;
    m_services = m_serviceScanner.scan(m_dataRoot + "/services", &svcStats);
    qInfo("Services: %d loaded, %d failed", svcStats.loadedServices, svcStats.failedServices);

    DriverManagerScanner::ScanStats driverStats;
    const QString driversDir = m_dataRoot + "/drivers";
    if (QDir(driversDir).exists()) {
        const auto drivers = m_driverScanner.scan(driversDir, true, &driverStats);
        m_driverCatalog.replaceAll(drivers);
    } else {
        m_driverCatalog.clear();
    }
    qInfo("Drivers: %d updated, %d failed, %d skipped",
          driverStats.updated,
          driverStats.newlyFailed,
          driverStats.skippedFailed);

    ProjectManager::LoadStats projectStats;
    m_projects = m_projectManager.loadAll(m_dataRoot + "/projects", m_services, &projectStats);
    qInfo("Projects: %d loaded, %d invalid", projectStats.loaded, projectStats.invalid);

    error.clear();
    return true;
}

void ServerManager::startScheduling() {
    m_scheduleEngine->startAll(m_projects, m_services);
}

void ServerManager::shutdown() {
    m_scheduleEngine->setShuttingDown(true);
    m_scheduleEngine->stopAll();
    m_instanceManager->terminateAll();
    m_instanceManager->waitAllFinished(5000);
}

DriverManagerScanner::ScanStats ServerManager::rescanDrivers(bool refreshMeta) {
    DriverManagerScanner::ScanStats stats;
    const QString driversDir = m_dataRoot + "/drivers";
    if (!QDir(driversDir).exists()) {
        m_driverCatalog.clear();
        return stats;
    }

    const auto drivers = m_driverScanner.scan(driversDir, refreshMeta, &stats);
    m_driverCatalog.replaceAll(drivers);
    return stats;
}

ServerManager::ServiceRescanStats ServerManager::rescanServices(bool revalidateProjects,
                                                                bool restartScheduling,
                                                                bool stopInvalidProjects) {
    ServiceRescanStats stats;
    const QMap<QString, ServiceInfo> oldServices = m_services;

    m_services = m_serviceScanner.scan(m_dataRoot + "/services", &stats.scanStats);

    for (auto it = m_services.begin(); it != m_services.end(); ++it) {
        const QString& id = it.key();
        const ServiceInfo& cur = it.value();
        auto oldIt = oldServices.find(id);
        if (oldIt == oldServices.end()) {
            stats.added++;
            continue;
        }

        const ServiceInfo& prev = oldIt.value();
        const bool changed = prev.name != cur.name
                             || prev.version != cur.version
                             || prev.serviceDir != cur.serviceDir
                             || prev.rawConfigSchema != cur.rawConfigSchema;
        if (changed) {
            stats.updated++;
        } else {
            stats.unchanged++;
        }
    }

    for (auto it = oldServices.begin(); it != oldServices.end(); ++it) {
        if (!m_services.contains(it.key())) {
            stats.removed++;
        }
    }

    if (revalidateProjects) {
        for (auto it = m_projects.begin(); it != m_projects.end(); ++it) {
            Project& project = it.value();
            const bool wasValid = project.valid;
            const bool nowValid = m_projectManager.validateProject(project, m_services);
            stats.revalidatedProjects++;

            if (nowValid) {
                if (!wasValid) {
                    stats.becameValid++;
                }
                continue;
            }

            if (wasValid) {
                stats.becameInvalid++;
            } else {
                stats.remainedInvalid++;
            }
            stats.invalidProjectIds.push_back(project.id);

            if (stopInvalidProjects) {
                m_scheduleEngine->stopProject(project.id);
                m_instanceManager->terminateByProject(project.id);
            }
        }
    }

    if (restartScheduling) {
        m_scheduleEngine->startAll(m_projects, m_services);
        stats.schedulingRestarted = true;
    }

    return stats;
}

} // namespace stdiolink_server
