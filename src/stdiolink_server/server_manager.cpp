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

} // namespace stdiolink_server
