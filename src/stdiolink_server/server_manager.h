#pragma once

#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

#include "config/server_config.h"
#include "http/event_bus.h"
#include "http/event_stream_handler.h"
#include "http/static_file_server.h"
#include "manager/instance_manager.h"
#include "manager/process_monitor.h"
#include "manager/project_manager.h"
#include "manager/schedule_engine.h"
#include "scanner/driver_manager_scanner.h"
#include "scanner/service_scanner.h"
#include "stdiolink/host/driver_catalog.h"

class QHttpServer;

namespace stdiolink_server {

class DriverLabWsHandler;

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

    struct ServerStatus {
        QString version;
        QDateTime startedAt;
        qint64 uptimeMs = 0;
        QString host;
        int port = 0;
        QString dataRoot;
        QString serviceProgram;

        int serviceCount = 0;
        int projectTotal = 0;
        int projectValid = 0;
        int projectInvalid = 0;
        int projectEnabled = 0;
        int projectDisabled = 0;
        int instanceTotal = 0;
        int instanceRunning = 0;
        int driverCount = 0;

        QString platform;
        int cpuCores = 0;
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

    void registerWebSocket(QHttpServer& server);
    DriverLabWsHandler* driverLabWsHandler() { return m_driverLabWsHandler; }

    DriverManagerScanner::ScanStats rescanDrivers(bool refreshMeta = true);
    ServiceRescanStats rescanServices(bool revalidateProjects = true,
                                      bool restartScheduling = true,
                                      bool stopInvalidProjects = false);

    ProcessMonitor* processMonitor() { return &m_processMonitor; }
    EventBus* eventBus() { return m_eventBus; }
    EventStreamHandler* eventStreamHandler() { return m_eventStreamHandler; }
    StaticFileServer* staticFileServer() { return m_staticFileServer.get(); }

    struct ServiceCreateRequest {
        QString id;
        QString name;
        QString version;
        QString description;
        QString author;
        QString templateType;  // "empty" / "basic" / "driver_demo"
        QString indexJs;
        QJsonObject configSchema;
        bool hasIndexJs = false;
        bool hasConfigSchema = false;
    };

    struct ServiceCreateResult {
        bool success = false;
        QString error;
        ServiceInfo serviceInfo;
    };

    ServiceCreateResult createService(const ServiceCreateRequest& request);
    bool deleteService(const QString& id, bool force, QString& error);
    bool reloadService(const QString& id, QString& error);

    ServerStatus serverStatus() const;

    QString dataRoot() const { return m_dataRoot; }
    const ServerConfig& config() const { return m_config; }

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
    ProcessMonitor m_processMonitor;
    EventBus* m_eventBus = nullptr;
    EventStreamHandler* m_eventStreamHandler = nullptr;
    QDateTime m_startedAt;
    DriverLabWsHandler* m_driverLabWsHandler = nullptr;
    std::unique_ptr<StaticFileServer> m_staticFileServer;
};

} // namespace stdiolink_server
