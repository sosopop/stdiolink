#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

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
    struct ServiceRescanStats {
        ServiceScanner::ScanStats scanStats;
        int added = 0;
        int removed = 0;
        int updated = 0;
        int unchanged = 0;
        int revalidatedProjects = 0;
        int becameValid = 0;
        int becameInvalid = 0;
        int remainedInvalid = 0;
        bool schedulingRestarted = false;
        QStringList invalidProjectIds;
    };

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
    ServiceRescanStats rescanServices(bool revalidateProjects = true,
                                      bool restartScheduling = true,
                                      bool stopInvalidProjects = false);

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
