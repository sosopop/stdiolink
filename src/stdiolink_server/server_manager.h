#pragma once

#include <QMap>
#include <QObject>
#include <QString>

#include "config/server_config.h"
#include "manager/instance_manager.h"
#include "manager/project_manager.h"
#include "manager/schedule_engine.h"
#include "scanner/driver_manager_scanner.h"
#include "scanner/service_scanner.h"
#include "stdiolink/host/driver_catalog.h"

namespace stdiolink_server {

class ServerManager : public QObject {
    Q_OBJECT
public:
    explicit ServerManager(const QString& dataRoot,
                           const ServerConfig& config,
                           QObject* parent = nullptr);

    bool initialize(QString& error);
    void startScheduling();
    void shutdown();

    const QMap<QString, ServiceInfo>& services() const { return m_services; }
    QMap<QString, Project>& projects() { return m_projects; }
    const QMap<QString, Project>& projects() const { return m_projects; }

    InstanceManager* instanceManager() { return m_instanceManager; }
    ScheduleEngine* scheduleEngine() { return m_scheduleEngine; }
    ProjectManager* projectManager() { return &m_projectManager; }
    stdiolink::DriverCatalog* driverCatalog() { return &m_driverCatalog; }

    DriverManagerScanner::ScanStats rescanDrivers(bool refreshMeta = true);

    QString dataRoot() const { return m_dataRoot; }

private:
    QString m_dataRoot;
    ServerConfig m_config;

    ServiceScanner m_serviceScanner;
    DriverManagerScanner m_driverScanner;
    ProjectManager m_projectManager;

    InstanceManager* m_instanceManager = nullptr;
    ScheduleEngine* m_scheduleEngine = nullptr;

    QMap<QString, ServiceInfo> m_services;
    QMap<QString, Project> m_projects;
    stdiolink::DriverCatalog m_driverCatalog;
};

} // namespace stdiolink_server
