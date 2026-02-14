#include "server_manager.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTextStream>
#include <QThread>

#include "http/driverlab_ws_handler.h"

namespace stdiolink_server {

namespace {

bool isValidServiceId(const QString& id) {
    if (id.isEmpty() || id.size() > 128) {
        return false;
    }
    static const QRegularExpression re(QStringLiteral("^[a-zA-Z0-9_-]+$"));
    return re.match(id).hasMatch();
}

bool writeTextFile(const QString& path, const QString& content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QTextStream out(&file);
    out << content;
    return file.error() == QFile::NoError;
}

QString templateIndexJs(const QString& templateType) {
    if (templateType == "basic") {
        return QStringLiteral(
            "import { getConfig, openDriver } from 'stdiolink';\n"
            "import { log } from 'stdiolink/log';\n"
            "\n"
            "const config = getConfig();\n"
            "log.info('service started', { config });\n"
            "\n"
            "// TODO: implement service logic\n");
    }
    if (templateType == "driver_demo") {
        return QStringLiteral(
            "import { getConfig, openDriver } from 'stdiolink';\n"
            "import { log } from 'stdiolink/log';\n"
            "\n"
            "const config = getConfig();\n"
            "const driver = openDriver(config.driverPath);\n"
            "const task = driver.request('meta.describe');\n"
            "const meta = task.wait();\n"
            "log.info('driver meta', meta);\n"
            "driver.close();\n");
    }
    // "empty" or default
    return QStringLiteral(
        "import { getConfig } from 'stdiolink';\n"
        "\n"
        "const config = getConfig();\n");
}

QJsonObject templateConfigSchema(const QString& templateType) {
    if (templateType == "basic") {
        return QJsonObject{
            {"name", QJsonObject{
                {"type", "string"},
                {"required", true},
                {"description", "Service display name"}
            }}
        };
    }
    if (templateType == "driver_demo") {
        return QJsonObject{
            {"driverPath", QJsonObject{
                {"type", "string"},
                {"required", true},
                {"description", "Path to driver executable"}
            }}
        };
    }
    // "empty" or default
    return QJsonObject{};
}

} // namespace

ServerManager::ServerManager(const QString& dataRoot,
                             const ServerConfig& config,
                             QObject* parent)
    : QObject(parent)
    , m_dataRoot(dataRoot)
    , m_config(config) {
    m_instanceManager = new InstanceManager(dataRoot, config, this);
    m_scheduleEngine = new ScheduleEngine(m_instanceManager, this);
    m_eventBus = new EventBus(this);
    m_eventStreamHandler = new EventStreamHandler(m_eventBus, m_config.corsOrigin, this);

    // Wire InstanceManager signals → EventBus
    connect(m_instanceManager, &InstanceManager::instanceStarted,
            this, [this](const QString& instanceId, const QString& projectId) {
                qint64 pid = 0;
                if (auto* inst = m_instanceManager->getInstance(instanceId)) {
                    pid = inst->pid;
                }
                m_eventBus->publish(QStringLiteral("instance.started"), QJsonObject{
                    {"instanceId", instanceId},
                    {"projectId", projectId},
                    {"pid", pid}
                });
            });

    connect(m_instanceManager, &InstanceManager::instanceFinished,
            this, [this](const QString& instanceId, const QString& projectId,
                         int exitCode, QProcess::ExitStatus exitStatus) {
                m_eventBus->publish(QStringLiteral("instance.finished"), QJsonObject{
                    {"instanceId", instanceId},
                    {"projectId", projectId},
                    {"exitCode", exitCode},
                    {"status", exitStatus == QProcess::NormalExit ? "normal" : "crashed"}
                });
            });

    // Wire ScheduleEngine signals → EventBus
    connect(m_scheduleEngine, &ScheduleEngine::scheduleTriggered,
            this, [this](const QString& projectId, const QString& scheduleType) {
                m_eventBus->publish(QStringLiteral("schedule.triggered"), QJsonObject{
                    {"projectId", projectId},
                    {"scheduleType", scheduleType}
                });
            });

    connect(m_scheduleEngine, &ScheduleEngine::scheduleSuppressed,
            this, [this](const QString& projectId, const QString& reason,
                         int consecutiveFailures) {
                m_eventBus->publish(QStringLiteral("schedule.suppressed"), QJsonObject{
                    {"projectId", projectId},
                    {"reason", reason},
                    {"consecutiveFailures", consecutiveFailures}
                });
            });
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

    m_startedAt = QDateTime::currentDateTimeUtc();

    // Initialize static file server
    QString webuiDir = m_config.webuiDir;
    if (webuiDir.isEmpty()) {
        webuiDir = m_dataRoot + "/webui";
    } else if (QDir::isRelativePath(webuiDir)) {
        webuiDir = m_dataRoot + "/" + webuiDir;
    }

    if (QDir(webuiDir).exists()) {
        m_staticFileServer = std::make_unique<StaticFileServer>(webuiDir);
        if (m_staticFileServer->isValid()) {
            qInfo("WebUI: serving from %s", qPrintable(webuiDir));
        } else {
            qInfo("WebUI: directory exists but no index.html found: %s", qPrintable(webuiDir));
        }
    } else {
        qInfo("WebUI: directory not found, static file serving disabled: %s", qPrintable(webuiDir));
    }

    error.clear();
    return true;
}

ServerManager::ServerStatus ServerManager::serverStatus() const {
    ServerStatus s;
    s.version = QStringLiteral("0.1.0");
    s.startedAt = m_startedAt;
    s.uptimeMs = m_startedAt.msecsTo(QDateTime::currentDateTimeUtc());
    s.host = m_config.host;
    s.port = m_config.port;
    s.dataRoot = m_dataRoot;
    s.serviceProgram = m_instanceManager->findServiceProgram();

    s.serviceCount = m_services.size();
    s.driverCount = m_driverCatalog.listDrivers().size();

    s.projectTotal = m_projects.size();
    for (auto it = m_projects.begin(); it != m_projects.end(); ++it) {
        const Project& p = it.value();
        if (p.valid) {
            s.projectValid++;
        } else {
            s.projectInvalid++;
        }
        if (p.enabled) {
            s.projectEnabled++;
        } else {
            s.projectDisabled++;
        }
    }

    s.instanceTotal = m_instanceManager->instanceCount();
    s.instanceRunning = s.instanceTotal;

    s.platform = QSysInfo::productType() + " " + QSysInfo::currentCpuArchitecture();
    s.cpuCores = QThread::idealThreadCount();

    return s;
}

void ServerManager::startScheduling() {
    m_scheduleEngine->startAll(m_projects, m_services);
}

void ServerManager::shutdown() {
    m_scheduleEngine->setShuttingDown(true);
    m_scheduleEngine->stopAll();
    if (m_driverLabWsHandler) {
        m_driverLabWsHandler->closeAll();
    }
    m_instanceManager->terminateAll();
    m_instanceManager->waitAllFinished(5000);
}

void ServerManager::registerWebSocket(QHttpServer& server) {
    m_driverLabWsHandler = new DriverLabWsHandler(&m_driverCatalog, this);
    m_driverLabWsHandler->registerVerifier(server);
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

ServerManager::ServiceCreateResult ServerManager::createService(const ServiceCreateRequest& request) {
    ServiceCreateResult result;

    if (!isValidServiceId(request.id)) {
        result.error = "invalid service id";
        return result;
    }
    if (request.name.isEmpty()) {
        result.error = "missing required field: name";
        return result;
    }
    if (request.version.isEmpty()) {
        result.error = "missing required field: version";
        return result;
    }
    if (request.hasConfigSchema) {
        QString schemaError;
        (void)stdiolink_service::ServiceConfigSchema::fromJsonObject(request.configSchema,
                                                                      schemaError);
        if (!schemaError.isEmpty()) {
            result.error = "invalid configSchema: " + schemaError;
            return result;
        }
    }

    if (m_services.contains(request.id)) {
        result.error = "service already exists";
        return result;
    }

    const QString serviceDir = m_dataRoot + "/services/" + request.id;
    if (QDir(serviceDir).exists()) {
        result.error = "service directory already exists";
        return result;
    }

    if (!QDir().mkpath(serviceDir)) {
        result.error = "failed to create service directory";
        return result;
    }

    // Build manifest
    QJsonObject manifest;
    manifest["manifestVersion"] = QStringLiteral("1");
    manifest["id"] = request.id;
    manifest["name"] = request.name;
    manifest["version"] = request.version;
    if (!request.description.isEmpty()) {
        manifest["description"] = request.description;
    }
    if (!request.author.isEmpty()) {
        manifest["author"] = request.author;
    }

    const QString manifestPath = serviceDir + "/manifest.json";
    if (!writeTextFile(manifestPath, QJsonDocument(manifest).toJson(QJsonDocument::Indented))) {
        QDir(serviceDir).removeRecursively();
        result.error = "failed to write manifest.json";
        return result;
    }

    // Write index.js
    const QString indexJs = request.hasIndexJs ? request.indexJs : templateIndexJs(request.templateType);
    if (!writeTextFile(serviceDir + "/index.js", indexJs)) {
        QDir(serviceDir).removeRecursively();
        result.error = "failed to write index.js";
        return result;
    }

    // Write config.schema.json
    const QJsonObject schema = request.hasConfigSchema ? request.configSchema
                                                       : templateConfigSchema(request.templateType);
    if (!writeTextFile(serviceDir + "/config.schema.json",
                       QJsonDocument(schema).toJson(QJsonDocument::Indented))) {
        QDir(serviceDir).removeRecursively();
        result.error = "failed to write config.schema.json";
        return result;
    }

    // Load into memory
    QString loadError;
    auto loaded = m_serviceScanner.loadSingle(serviceDir, loadError);
    if (!loaded.has_value()) {
        QDir(serviceDir).removeRecursively();
        result.error = "failed to load created service: " + loadError;
        return result;
    }

    m_services.insert(loaded->id, *loaded);
    result.success = true;
    result.serviceInfo = *loaded;
    return result;
}

bool ServerManager::deleteService(const QString& id, bool force, QString& error) {
    if (!m_services.contains(id)) {
        error = "service not found";
        return false;
    }

    // Check for associated projects
    QStringList associatedProjectIds;
    for (auto it = m_projects.begin(); it != m_projects.end(); ++it) {
        if (it.value().serviceId == id) {
            associatedProjectIds.append(it.key());
        }
    }

    if (!associatedProjectIds.isEmpty() && !force) {
        error = "service has associated projects: " + associatedProjectIds.join(", ");
        return false;
    }

    // Force: mark associated projects as invalid
    if (force) {
        for (const QString& projectId : associatedProjectIds) {
            auto pIt = m_projects.find(projectId);
            if (pIt != m_projects.end()) {
                pIt->valid = false;
                pIt->error = "service '" + id + "' has been deleted";
                m_scheduleEngine->stopProject(projectId);
                m_instanceManager->terminateByProject(projectId);
            }
        }
    }

    const QString serviceDir = m_services.value(id).serviceDir;
    if (!QDir(serviceDir).removeRecursively()) {
        error = "failed to remove service directory";
        return false;
    }

    m_services.remove(id);
    error.clear();
    return true;
}

bool ServerManager::reloadService(const QString& id, QString& error) {
    auto it = m_services.find(id);
    if (it == m_services.end()) {
        error = "service not found in memory";
        return false;
    }

    const QString serviceDir = it->serviceDir;
    QString loadError;
    auto loaded = m_serviceScanner.loadSingle(serviceDir, loadError);
    if (!loaded.has_value()) {
        error = "failed to reload service: " + loadError;
        return false;
    }

    m_services[id] = *loaded;
    error.clear();
    return true;
}

} // namespace stdiolink_server
