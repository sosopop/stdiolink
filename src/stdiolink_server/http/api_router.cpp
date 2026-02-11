#include "api_router.h"

#include <QDir>
#include <QFile>
#include <QHttpServerResponder>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrlQuery>

#include <algorithm>

#include "http_helpers.h"
#include "manager/project_manager.h"
#include "server_manager.h"

using Method = QHttpServerRequest::Method;

namespace stdiolink_server {

namespace {

bool parseJsonObjectBody(const QHttpServerRequest& req,
                         QJsonObject& out,
                         QString& error) {
    const QByteArray body = req.body();
    if (body.trimmed().isEmpty()) {
        out = QJsonObject();
        error.clear();
        return true;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        error = "request body must be a JSON object";
        return false;
    }

    out = doc.object();
    error.clear();
    return true;
}

QJsonObject manifestToJson(const stdiolink_service::ServiceManifest& manifest) {
    QJsonObject out;
    out["manifestVersion"] = manifest.manifestVersion;
    out["id"] = manifest.id;
    out["name"] = manifest.name;
    out["version"] = manifest.version;
    if (!manifest.description.isEmpty()) {
        out["description"] = manifest.description;
    }
    if (!manifest.author.isEmpty()) {
        out["author"] = manifest.author;
    }
    return out;
}

QString projectStatus(const Project& project, int runningCount) {
    if (!project.valid) {
        return "invalid";
    }
    if (runningCount > 0) {
        return "running";
    }
    return "stopped";
}

QJsonObject instanceToJson(const Instance& inst) {
    QJsonObject out;
    out["id"] = inst.id;
    out["projectId"] = inst.projectId;
    out["serviceId"] = inst.serviceId;
    out["pid"] = static_cast<qint64>(inst.pid);
    out["startedAt"] = inst.startedAt.toString(Qt::ISODate);
    out["status"] = inst.status;
    return out;
}

QJsonObject projectToJson(const Project& project,
                          InstanceManager* instanceManager) {
    const int runningCount = instanceManager->instanceCount(project.id);

    QJsonObject out;
    out["id"] = project.id;
    out["name"] = project.name;
    out["serviceId"] = project.serviceId;
    out["enabled"] = project.enabled;
    out["valid"] = project.valid;
    out["schedule"] = project.schedule.toJson();
    out["config"] = project.config;
    out["instanceCount"] = runningCount;
    out["status"] = projectStatus(project, runningCount);
    if (!project.error.isEmpty()) {
        out["error"] = project.error;
    }

    return out;
}

bool loadProjectFromFile(const QString& filePath,
                         const QString& id,
                         Project& project,
                         QString& error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        error = "cannot open project file: " + filePath;
        return false;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        error = "project file parse error: " + parseErr.errorString();
        return false;
    }

    QString parseProjectErr;
    project = Project::fromJson(id, doc.object(), parseProjectErr);
    if (!parseProjectErr.isEmpty()) {
        error = parseProjectErr;
        return false;
    }

    error.clear();
    return true;
}

QJsonArray readTailLines(const QString& path, int maxLines) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QByteArray raw = file.readAll();
    const QList<QByteArray> all = raw.split('\n');

    const int start = std::max(0, static_cast<int>(all.size()) - maxLines - 1);
    QJsonArray out;
    for (int i = start; i < all.size(); ++i) {
        const QByteArray line = all[i].trimmed();
        if (!line.isEmpty()) {
            out.append(QString::fromUtf8(line));
        }
    }
    return out;
}

} // namespace

ApiRouter::ApiRouter(ServerManager* manager, QObject* parent)
    : QObject(parent)
    , m_manager(manager) {
}

void ApiRouter::registerRoutes(QHttpServer& server) {
    server.route("/api/services", Method::Get, [this](const QHttpServerRequest& req) {
        return handleServiceList(req);
    });
    server.route("/api/services/<arg>", Method::Get, [this](const QString& id, const QHttpServerRequest& req) {
        return handleServiceDetail(id, req);
    });

    server.route("/api/projects", Method::Get, [this](const QHttpServerRequest& req) {
        return handleProjectList(req);
    });
    server.route("/api/projects", Method::Post, [this](const QHttpServerRequest& req) {
        return handleProjectCreate(req);
    });
    server.route("/api/projects/<arg>", Method::Get, [this](const QString& id, const QHttpServerRequest& req) {
        return handleProjectDetail(id, req);
    });
    server.route("/api/projects/<arg>", Method::Put, [this](const QString& id, const QHttpServerRequest& req) {
        return handleProjectUpdate(id, req);
    });
    server.route("/api/projects/<arg>", Method::Delete, [this](const QString& id, const QHttpServerRequest& req) {
        return handleProjectDelete(id, req);
    });

    server.route(
        "/api/projects/<arg>/validate",
        Method::Post,
        [this](const QString& id, const QHttpServerRequest& req) {
            return handleProjectValidate(id, req);
        });
    server.route("/api/projects/<arg>/start", Method::Post, [this](const QString& id, const QHttpServerRequest& req) {
        return handleProjectStart(id, req);
    });
    server.route("/api/projects/<arg>/stop", Method::Post, [this](const QString& id, const QHttpServerRequest& req) {
        return handleProjectStop(id, req);
    });
    server.route(
        "/api/projects/<arg>/reload",
        Method::Post,
        [this](const QString& id, const QHttpServerRequest& req) {
            return handleProjectReload(id, req);
        });

    server.route("/api/instances", Method::Get, [this](const QHttpServerRequest& req) {
        return handleInstanceList(req);
    });
    server.route(
        "/api/instances/<arg>/terminate",
        Method::Post,
        [this](const QString& id, const QHttpServerRequest& req) {
            return handleInstanceTerminate(id, req);
        });
    server.route("/api/instances/<arg>/logs", Method::Get, [this](const QString& id, const QHttpServerRequest& req) {
        return handleInstanceLogs(id, req);
    });

    server.route("/api/drivers", Method::Get, [this](const QHttpServerRequest& req) {
        return handleDriverList(req);
    });
    server.route("/api/drivers/scan", Method::Post, [this](const QHttpServerRequest& req) {
        return handleDriverScan(req);
    });

    server.setMissingHandler(this, [](const QHttpServerRequest&, QHttpServerResponder& responder) {
        const QByteArray body = QJsonDocument(QJsonObject{{"error", "not found"}})
                                    .toJson(QJsonDocument::Compact);
        responder.write(body,
                        "application/json",
                        QHttpServerResponder::StatusCode::NotFound);
    });
}

QHttpServerResponse ApiRouter::handleServiceList(const QHttpServerRequest& req) {
    Q_UNUSED(req);

    QJsonArray services;
    const auto& projects = m_manager->projects();

    for (auto it = m_manager->services().begin(); it != m_manager->services().end(); ++it) {
        const ServiceInfo& service = it.value();

        int projectCount = 0;
        for (auto pIt = projects.begin(); pIt != projects.end(); ++pIt) {
            if (pIt.value().serviceId == service.id) {
                projectCount++;
            }
        }

        QJsonObject obj;
        obj["id"] = service.id;
        obj["name"] = service.name;
        obj["version"] = service.version;
        obj["serviceDir"] = service.serviceDir;
        obj["hasSchema"] = service.hasSchema;
        obj["projectCount"] = projectCount;
        services.append(obj);
    }

    return jsonResponse(QJsonObject{{"services", services}});
}

QHttpServerResponse ApiRouter::handleServiceDetail(const QString& id,
                                                   const QHttpServerRequest& req) {
    Q_UNUSED(req);

    const auto it = m_manager->services().find(id);
    if (it == m_manager->services().end()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "service not found");
    }

    const ServiceInfo& service = it.value();
    QJsonArray projectIds;
    for (auto pIt = m_manager->projects().begin(); pIt != m_manager->projects().end(); ++pIt) {
        if (pIt.value().serviceId == id) {
            projectIds.append(pIt.key());
        }
    }

    QJsonObject result;
    result["id"] = service.id;
    result["name"] = service.name;
    result["version"] = service.version;
    result["serviceDir"] = service.serviceDir;
    result["manifest"] = manifestToJson(service.manifest);
    result["configSchema"] = service.rawConfigSchema;
    result["projects"] = projectIds;
    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleProjectList(const QHttpServerRequest& req) {
    Q_UNUSED(req);

    QJsonArray projects;
    auto* instanceManager = m_manager->instanceManager();
    for (auto it = m_manager->projects().begin(); it != m_manager->projects().end(); ++it) {
        projects.append(projectToJson(it.value(), instanceManager));
    }

    return jsonResponse(QJsonObject{{"projects", projects}});
}

QHttpServerResponse ApiRouter::handleProjectDetail(const QString& id,
                                                   const QHttpServerRequest& req) {
    Q_UNUSED(req);

    auto pIt = m_manager->projects().find(id);
    if (pIt == m_manager->projects().end()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "project not found");
    }

    const Project& project = pIt.value();
    QJsonObject detail = projectToJson(project, m_manager->instanceManager());

    QJsonArray instances;
    for (const Instance* inst : m_manager->instanceManager()->getInstances(id)) {
        instances.append(instanceToJson(*inst));
    }
    detail["instances"] = instances;

    auto svcIt = m_manager->services().find(project.serviceId);
    if (svcIt != m_manager->services().end()) {
        detail["configSchema"] = svcIt.value().rawConfigSchema;
    }

    return jsonResponse(detail);
}

QHttpServerResponse ApiRouter::handleProjectCreate(const QHttpServerRequest& req) {
    QJsonObject body;
    QString error;
    if (!parseJsonObjectBody(req, body, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    if (!body.value("id").isString()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, "missing required string field: id");
    }

    const QString id = body.value("id").toString();
    if (!ProjectManager::isValidProjectId(id)) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, "invalid project id");
    }

    auto& projects = m_manager->projects();
    if (projects.contains(id)) {
        return errorResponse(QHttpServerResponse::StatusCode::Conflict, "project already exists");
    }

    QString parseError;
    Project project = Project::fromJson(id, body, parseError);
    if (!parseError.isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, parseError);
    }

    if (!ProjectManager::validateProject(project, m_manager->services())) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, "project invalid: " + project.error);
    }

    if (!ProjectManager::saveProject(m_manager->dataRoot() + "/projects", project, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::InternalServerError, error);
    }

    projects.insert(id, project);
    m_manager->startScheduling();

    return jsonResponse(projectToJson(project, m_manager->instanceManager()),
                        QHttpServerResponse::StatusCode::Created);
}

QHttpServerResponse ApiRouter::handleProjectUpdate(const QString& id,
                                                   const QHttpServerRequest& req) {
    auto& projects = m_manager->projects();
    if (!projects.contains(id)) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "project not found");
    }

    QJsonObject body;
    QString error;
    if (!parseJsonObjectBody(req, body, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    if (body.contains("id") && body.value("id").isString() && body.value("id").toString() != id) {
        return errorResponse(QHttpServerResponse::StatusCode::Conflict, "project id mismatch");
    }

    QString parseError;
    Project project = Project::fromJson(id, body, parseError);
    if (!parseError.isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, parseError);
    }

    if (!ProjectManager::validateProject(project, m_manager->services())) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, "project invalid: " + project.error);
    }

    if (!ProjectManager::saveProject(m_manager->dataRoot() + "/projects", project, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::InternalServerError, error);
    }

    m_manager->scheduleEngine()->stopProject(id);
    m_manager->instanceManager()->terminateByProject(id);
    projects[id] = project;
    m_manager->startScheduling();

    return jsonResponse(projectToJson(project, m_manager->instanceManager()));
}

QHttpServerResponse ApiRouter::handleProjectDelete(const QString& id,
                                                   const QHttpServerRequest& req) {
    Q_UNUSED(req);

    auto& projects = m_manager->projects();
    if (!projects.contains(id)) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "project not found");
    }

    QString error;
    if (!ProjectManager::removeProject(m_manager->dataRoot() + "/projects", id, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::InternalServerError, error);
    }

    m_manager->scheduleEngine()->stopProject(id);
    m_manager->instanceManager()->terminateByProject(id);
    projects.remove(id);

    return noContentResponse();
}

QHttpServerResponse ApiRouter::handleProjectValidate(const QString& id,
                                                     const QHttpServerRequest& req) {
    auto pIt = m_manager->projects().find(id);
    if (pIt == m_manager->projects().end()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "project not found");
    }

    QJsonObject body;
    QString error;
    if (!parseJsonObjectBody(req, body, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    if (!body.value("config").isObject()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, "field 'config' must be an object");
    }

    Project temp = pIt.value();
    temp.config = body.value("config").toObject();
    const bool valid = ProjectManager::validateProject(temp, m_manager->services());

    QJsonObject result;
    result["valid"] = valid;
    if (!valid) {
        result["error"] = temp.error;
    }
    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleProjectStart(const QString& id,
                                                  const QHttpServerRequest& req) {
    Q_UNUSED(req);

    auto pIt = m_manager->projects().find(id);
    if (pIt == m_manager->projects().end()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "project not found");
    }

    const Project& project = pIt.value();
    if (!project.valid) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, "project invalid: " + project.error);
    }

    const auto svcIt = m_manager->services().find(project.serviceId);
    if (svcIt == m_manager->services().end()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, "service not found");
    }

    auto* instanceManager = m_manager->instanceManager();
    const int running = instanceManager->instanceCount(id);

    if (project.schedule.type == ScheduleType::Manual) {
        if (running > 0) {
            return errorResponse(QHttpServerResponse::StatusCode::Conflict, "already running");
        }
    } else if (project.schedule.type == ScheduleType::FixedRate) {
        if (running >= project.schedule.maxConcurrent) {
            return errorResponse(QHttpServerResponse::StatusCode::Conflict, "max concurrent reached");
        }
    } else if (project.schedule.type == ScheduleType::Daemon) {
        if (running > 0) {
            return jsonResponse(QJsonObject{{"noop", true}});
        }
    }

    m_manager->scheduleEngine()->resumeProject(id);

    QString error;
    const QString instanceId = instanceManager->startInstance(project, svcIt.value().serviceDir, error);
    if (instanceId.isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::InternalServerError, error);
    }

    const Instance* inst = instanceManager->getInstance(instanceId);
    QJsonObject result;
    result["instanceId"] = instanceId;
    result["pid"] = inst ? static_cast<qint64>(inst->pid) : 0;
    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleProjectStop(const QString& id,
                                                 const QHttpServerRequest& req) {
    Q_UNUSED(req);

    if (!m_manager->projects().contains(id)) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "project not found");
    }

    m_manager->scheduleEngine()->stopProject(id);
    m_manager->instanceManager()->terminateByProject(id);

    return jsonResponse(QJsonObject{{"stopped", true}});
}

QHttpServerResponse ApiRouter::handleProjectReload(const QString& id,
                                                   const QHttpServerRequest& req) {
    Q_UNUSED(req);

    const QString filePath = m_manager->dataRoot() + "/projects/" + id + ".json";
    if (!QFile::exists(filePath)) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "project file not found");
    }

    Project project;
    QString error;
    if (!loadProjectFromFile(filePath, id, project, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    if (!ProjectManager::validateProject(project, m_manager->services())) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, "project invalid: " + project.error);
    }

    m_manager->scheduleEngine()->stopProject(id);
    m_manager->instanceManager()->terminateByProject(id);
    m_manager->projects()[id] = project;
    m_manager->startScheduling();

    return jsonResponse(projectToJson(project, m_manager->instanceManager()));
}

QHttpServerResponse ApiRouter::handleInstanceList(const QHttpServerRequest& req) {
    QUrlQuery query(req.url());
    const QString projectId = query.queryItemValue("projectId");

    QJsonArray instances;
    for (const Instance* inst : m_manager->instanceManager()->getInstances(projectId)) {
        instances.append(instanceToJson(*inst));
    }

    return jsonResponse(QJsonObject{{"instances", instances}});
}

QHttpServerResponse ApiRouter::handleInstanceTerminate(const QString& id,
                                                       const QHttpServerRequest& req) {
    Q_UNUSED(req);

    if (!m_manager->instanceManager()->getInstance(id)) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "instance not found");
    }

    m_manager->instanceManager()->terminateInstance(id);
    return jsonResponse(QJsonObject{{"terminated", true}});
}

QHttpServerResponse ApiRouter::handleInstanceLogs(const QString& id,
                                                  const QHttpServerRequest& req) {
    QUrlQuery query(req.url());

    bool ok = false;
    int lines = query.queryItemValue("lines").toInt(&ok);
    if (!ok) {
        lines = 100;
    }
    if (lines < 1 || lines > 5000) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, "lines out of range");
    }

    QString projectId;
    const Instance* inst = m_manager->instanceManager()->getInstance(id);
    if (inst) {
        projectId = inst->projectId;
    } else if (m_manager->projects().contains(id)) {
        projectId = id;
    }

    if (projectId.isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "instance not found");
    }

    const QString logPath = m_manager->dataRoot() + "/logs/" + projectId + ".log";
    if (!QFile::exists(logPath)) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "log file not found");
    }

    return jsonResponse(QJsonObject{{"projectId", projectId},
                                    {"lines", readTailLines(logPath, lines)}});
}

QHttpServerResponse ApiRouter::handleDriverList(const QHttpServerRequest& req) {
    Q_UNUSED(req);

    auto* catalog = m_manager->driverCatalog();
    QStringList ids = catalog->listDrivers();
    std::sort(ids.begin(), ids.end());

    QJsonArray drivers;
    for (const QString& id : ids) {
        const auto cfg = catalog->getConfig(id);

        QJsonObject obj;
        obj["id"] = cfg.id;
        obj["program"] = cfg.program;
        obj["metaHash"] = cfg.metaHash;
        if (cfg.meta) {
            obj["name"] = cfg.meta->info.name;
            obj["version"] = cfg.meta->info.version;
        }
        drivers.append(obj);
    }

    return jsonResponse(QJsonObject{{"drivers", drivers}});
}

QHttpServerResponse ApiRouter::handleDriverScan(const QHttpServerRequest& req) {
    QJsonObject body;
    QString error;
    if (!parseJsonObjectBody(req, body, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    bool refreshMeta = true;
    if (body.contains("refreshMeta")) {
        if (!body.value("refreshMeta").isBool()) {
            return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                                 "field 'refreshMeta' must be a bool");
        }
        refreshMeta = body.value("refreshMeta").toBool(true);
    }

    const DriverManagerScanner::ScanStats stats = m_manager->rescanDrivers(refreshMeta);
    return jsonResponse(QJsonObject{{"scanned", stats.scanned},
                                    {"updated", stats.updated},
                                    {"newlyFailed", stats.newlyFailed},
                                    {"skippedFailed", stats.skippedFailed}});
}

} // namespace stdiolink_server
