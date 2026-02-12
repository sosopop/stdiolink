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
#include "cors_middleware.h"
#include "event_stream_handler.h"
#include "service_file_handler.h"
#include "manager/process_monitor.h"
#include "manager/project_manager.h"
#include "server_manager.h"
#include "stdiolink_service/config/service_config_schema.h"
#include "stdiolink_service/config/service_config_validator.h"

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
    if (!project.enabled) {
        return "disabled";
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

QString scheduleTypeToString(ScheduleType type) {
    switch (type) {
    case ScheduleType::Manual:
        return "manual";
    case ScheduleType::FixedRate:
        return "fixed_rate";
    case ScheduleType::Daemon:
        return "daemon";
    }
    return "manual";
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
    server.route("/api/server/status", Method::Get, [this](const QHttpServerRequest& req) {
        return handleServerStatus(req);
    });

    server.route("/api/services", Method::Get, [this](const QHttpServerRequest& req) {
        return handleServiceList(req);
    });
    server.route("/api/services", Method::Post, [this](const QHttpServerRequest& req) {
        return handleServiceCreate(req);
    });
    server.route("/api/services/scan", Method::Post, [this](const QHttpServerRequest& req) {
        return handleServiceScan(req);
    });
    server.route("/api/services/<arg>", Method::Get, [this](const QString& id, const QHttpServerRequest& req) {
        return handleServiceDetail(id, req);
    });
    server.route("/api/services/<arg>", Method::Delete, [this](const QString& id, const QHttpServerRequest& req) {
        return handleServiceDelete(id, req);
    });
    server.route("/api/services/<arg>/validate-schema", Method::Post, [this](const QString& id, const QHttpServerRequest& req) {
        return handleValidateSchema(id, req);
    });
    server.route("/api/services/<arg>/generate-defaults", Method::Post, [this](const QString& id, const QHttpServerRequest& req) {
        return handleGenerateDefaults(id, req);
    });
    server.route("/api/services/<arg>/validate-config", Method::Post, [this](const QString& id, const QHttpServerRequest& req) {
        return handleValidateConfig(id, req);
    });
    server.route("/api/services/<arg>/files/content", Method::Get, [this](const QString& id, const QHttpServerRequest& req) {
        return handleServiceFileRead(id, req);
    });
    server.route("/api/services/<arg>/files/content", Method::Put, [this](const QString& id, const QHttpServerRequest& req) {
        return handleServiceFileWrite(id, req);
    });
    server.route("/api/services/<arg>/files/content", Method::Post, [this](const QString& id, const QHttpServerRequest& req) {
        return handleServiceFileCreate(id, req);
    });
    server.route("/api/services/<arg>/files/content", Method::Delete, [this](const QString& id, const QHttpServerRequest& req) {
        return handleServiceFileDelete(id, req);
    });
    server.route("/api/services/<arg>/files", Method::Get, [this](const QString& id, const QHttpServerRequest& req) {
        return handleServiceFiles(id, req);
    });

    server.route("/api/projects", Method::Get, [this](const QHttpServerRequest& req) {
        return handleProjectList(req);
    });
    server.route("/api/projects", Method::Post, [this](const QHttpServerRequest& req) {
        return handleProjectCreate(req);
    });
    server.route("/api/projects/runtime", Method::Get, [this](const QHttpServerRequest& req) {
        return handleProjectRuntimeBatch(req);
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
    server.route("/api/projects/<arg>/runtime", Method::Get, [this](const QString& id, const QHttpServerRequest& req) {
        return handleProjectRuntime(id, req);
    });
    server.route("/api/projects/<arg>/enabled", Method::Patch, [this](const QString& id, const QHttpServerRequest& req) {
        return handleProjectEnabled(id, req);
    });
    server.route("/api/projects/<arg>/logs", Method::Get, [this](const QString& id, const QHttpServerRequest& req) {
        return handleProjectLogs(id, req);
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
    server.route("/api/instances/<arg>/process-tree", Method::Get, [this](const QString& id, const QHttpServerRequest& req) {
        return handleInstanceProcessTree(id, req);
    });
    server.route("/api/instances/<arg>/resources", Method::Get, [this](const QString& id, const QHttpServerRequest& req) {
        return handleInstanceResources(id, req);
    });
    server.route("/api/instances/<arg>", Method::Get, [this](const QString& id, const QHttpServerRequest& req) {
        return handleInstanceDetail(id, req);
    });

    server.route("/api/drivers", Method::Get, [this](const QHttpServerRequest& req) {
        return handleDriverList(req);
    });
    server.route("/api/drivers/scan", Method::Post, [this](const QHttpServerRequest& req) {
        return handleDriverScan(req);
    });
    server.route("/api/drivers/<arg>", Method::Get, [this](const QString& id, const QHttpServerRequest& req) {
        return handleDriverDetail(id, req);
    });

    server.route("/api/events/stream", Method::Get, this,
                 [this](const QHttpServerRequest& req, QHttpServerResponder& responder) {
                     handleEventStream(req, responder);
                 });

    server.setMissingHandler(this, [corsOrigin = m_manager->config().corsOrigin](
                                        const QHttpServerRequest&, QHttpServerResponder& responder) {
        const QByteArray body = QJsonDocument(QJsonObject{{"error", "not found"}})
                                    .toJson(QJsonDocument::Compact);
        QHttpHeaders headers = CorsMiddleware::buildCorsHeaders(corsOrigin);
        headers.append(QHttpHeaders::WellKnownHeader::ContentType, "application/json");
        responder.write(body,
                        headers,
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
    result["configSchemaFields"] = service.configSchema.toFieldMetaArray();
    result["projects"] = projectIds;
    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleServiceScan(const QHttpServerRequest& req) {
    QJsonObject body;
    QString error;
    if (!parseJsonObjectBody(req, body, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    bool revalidateProjects = true;
    if (body.contains("revalidateProjects")) {
        if (!body.value("revalidateProjects").isBool()) {
            return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                                 "field 'revalidateProjects' must be a bool");
        }
        revalidateProjects = body.value("revalidateProjects").toBool(true);
    }

    bool restartScheduling = true;
    if (body.contains("restartScheduling")) {
        if (!body.value("restartScheduling").isBool()) {
            return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                                 "field 'restartScheduling' must be a bool");
        }
        restartScheduling = body.value("restartScheduling").toBool(true);
    }

    bool stopInvalidProjects = false;
    if (body.contains("stopInvalidProjects")) {
        if (!body.value("stopInvalidProjects").isBool()) {
            return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                                 "field 'stopInvalidProjects' must be a bool");
        }
        stopInvalidProjects = body.value("stopInvalidProjects").toBool(false);
    }

    const ServerManager::ServiceRescanStats stats =
        m_manager->rescanServices(revalidateProjects, restartScheduling, stopInvalidProjects);

    QJsonArray invalidProjects;
    for (const QString& id : stats.invalidProjectIds) {
        invalidProjects.append(id);
    }

    QJsonObject result;
    result["scannedDirs"] = stats.scanStats.scannedDirs;
    result["loadedServices"] = stats.scanStats.loadedServices;
    result["failedServices"] = stats.scanStats.failedServices;
    result["added"] = stats.added;
    result["removed"] = stats.removed;
    result["updated"] = stats.updated;
    result["unchanged"] = stats.unchanged;
    result["revalidatedProjects"] = stats.revalidatedProjects;
    result["becameValid"] = stats.becameValid;
    result["becameInvalid"] = stats.becameInvalid;
    result["remainedInvalid"] = stats.remainedInvalid;
    result["schedulingRestarted"] = stats.schedulingRestarted;
    result["invalidProjects"] = invalidProjects;
    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleServiceCreate(const QHttpServerRequest& req) {
    QJsonObject body;
    QString error;
    if (!parseJsonObjectBody(req, body, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    ServerManager::ServiceCreateRequest createReq;

    if (!body.value("id").isString() || body.value("id").toString().isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                             "missing required string field: id");
    }
    createReq.id = body.value("id").toString();

    if (!body.value("name").isString() || body.value("name").toString().isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                             "missing required string field: name");
    }
    createReq.name = body.value("name").toString();

    if (!body.value("version").isString() || body.value("version").toString().isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                             "missing required string field: version");
    }
    createReq.version = body.value("version").toString();

    createReq.description = body.value("description").toString();
    createReq.author = body.value("author").toString();
    createReq.templateType = body.value("template").toString("empty");

    if (createReq.templateType != "empty"
        && createReq.templateType != "basic"
        && createReq.templateType != "driver_demo") {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                             "invalid template: must be 'empty', 'basic', or 'driver_demo'");
    }

    if (body.contains("indexJs")) {
        if (!body.value("indexJs").isString()) {
            return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                                 "field 'indexJs' must be a string");
        }
        createReq.indexJs = body.value("indexJs").toString();
        createReq.hasIndexJs = true;
    }
    if (body.contains("configSchema")) {
        if (!body.value("configSchema").isObject()) {
            return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                                 "field 'configSchema' must be an object");
        }
        createReq.configSchema = body.value("configSchema").toObject();
        createReq.hasConfigSchema = true;
    }

    auto result = m_manager->createService(createReq);
    if (!result.success) {
        if (result.error.contains("already exists")
            || result.error == "service directory already exists") {
            return errorResponse(QHttpServerResponse::StatusCode::Conflict, result.error);
        }
        if (result.error.startsWith("invalid")
            || result.error.startsWith("missing")) {
            return errorResponse(QHttpServerResponse::StatusCode::BadRequest, result.error);
        }
        return errorResponse(QHttpServerResponse::StatusCode::InternalServerError, result.error);
    }

    const ServiceInfo& svc = result.serviceInfo;
    QJsonObject out;
    out["id"] = svc.id;
    out["name"] = svc.name;
    out["version"] = svc.version;
    out["serviceDir"] = svc.serviceDir;
    out["hasSchema"] = svc.hasSchema;
    out["created"] = true;
    return jsonResponse(out, QHttpServerResponse::StatusCode::Created);
}

QHttpServerResponse ApiRouter::handleServiceDelete(const QString& id,
                                                    const QHttpServerRequest& req) {
    const QUrlQuery query(req.url());
    const bool force = query.queryItemValue("force") == "true";

    QString error;
    if (!m_manager->deleteService(id, force, error)) {
        if (error == "service not found") {
            return errorResponse(QHttpServerResponse::StatusCode::NotFound, error);
        }
        if (error.startsWith("service has associated")) {
            QJsonObject errObj;
            errObj["error"] = error;

            QJsonArray projectIds;
            for (auto it = m_manager->projects().begin(); it != m_manager->projects().end(); ++it) {
                if (it.value().serviceId == id) {
                    projectIds.append(it.key());
                }
            }
            errObj["associatedProjects"] = projectIds;
            return jsonResponse(errObj, QHttpServerResponse::StatusCode::Conflict);
        }
        return errorResponse(QHttpServerResponse::StatusCode::InternalServerError, error);
    }

    return noContentResponse();
}

QHttpServerResponse ApiRouter::handleServiceFiles(const QString& id,
                                                   const QHttpServerRequest& req) {
    Q_UNUSED(req);

    auto svcIt = m_manager->services().find(id);
    if (svcIt == m_manager->services().end()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "service not found");
    }

    const QString serviceDir = svcIt->serviceDir;
    const QVector<FileInfo> files = ServiceFileHandler::listFiles(serviceDir);

    QJsonArray filesArray;
    for (const FileInfo& fi : files) {
        QJsonObject obj;
        obj["name"] = fi.name;
        obj["path"] = fi.path;
        obj["size"] = fi.size;
        obj["modifiedAt"] = fi.modifiedAt;
        obj["type"] = fi.type;
        filesArray.append(obj);
    }

    QJsonObject result;
    result["serviceId"] = id;
    result["serviceDir"] = serviceDir;
    result["files"] = filesArray;
    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleServiceFileRead(const QString& id,
                                                      const QHttpServerRequest& req) {
    auto svcIt = m_manager->services().find(id);
    if (svcIt == m_manager->services().end()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "service not found");
    }

    const QUrlQuery query(req.url());
    const QString relativePath = query.queryItemValue("path");
    if (relativePath.isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, "missing 'path' query parameter");
    }

    QString error;
    const QString absPath = ServiceFileHandler::resolveSafePath(svcIt->serviceDir, relativePath, error);
    if (absPath.isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    QFileInfo fi(absPath);
    if (!fi.exists() || !fi.isFile()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "file not found");
    }

    if (fi.size() > ServiceFileHandler::kMaxFileSize) {
        return errorResponse(static_cast<QHttpServerResponse::StatusCode>(413),
                             "file too large (max 1MB)");
    }

    QFile file(absPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return errorResponse(QHttpServerResponse::StatusCode::InternalServerError,
                             "failed to read file");
    }

    const QByteArray content = file.readAll();

    QJsonObject result;
    result["path"] = relativePath;
    result["content"] = QString::fromUtf8(content);
    result["size"] = fi.size();
    result["modifiedAt"] = fi.lastModified().toUTC().toString(Qt::ISODate);
    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleServiceFileWrite(const QString& id,
                                                       const QHttpServerRequest& req) {
    auto svcIt = m_manager->services().find(id);
    if (svcIt == m_manager->services().end()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "service not found");
    }

    const QUrlQuery query(req.url());
    const QString relativePath = query.queryItemValue("path");
    if (relativePath.isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, "missing 'path' query parameter");
    }

    QString error;
    const QString absPath = ServiceFileHandler::resolveSafePath(svcIt->serviceDir, relativePath, error);
    if (absPath.isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    if (!QFileInfo::exists(absPath)) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "file not found");
    }

    QJsonObject body;
    if (!parseJsonObjectBody(req, body, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    if (!body.contains("content") || !body.value("content").isString()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                             "missing required string field: content");
    }

    const QByteArray content = body.value("content").toString().toUtf8();

    if (content.size() > ServiceFileHandler::kMaxFileSize) {
        return errorResponse(static_cast<QHttpServerResponse::StatusCode>(413),
                             "content too large (max 1MB)");
    }

    // Special validation for manifest.json
    if (relativePath == "manifest.json") {
        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(content, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                                 "manifest.json must be valid JSON object");
        }
        QString manifestErr;
        stdiolink_service::ServiceManifest::fromJson(doc.object(), manifestErr);
        if (!manifestErr.isEmpty()) {
            return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                                 "invalid manifest: " + manifestErr);
        }
    }

    // Special validation for config.schema.json
    if (relativePath == "config.schema.json") {
        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(content, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                                 "config.schema.json must be valid JSON object");
        }
    }

    if (!ServiceFileHandler::atomicWrite(absPath, content, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::InternalServerError, error);
    }

    // Reload service in memory if core file was updated
    if (relativePath == "manifest.json" || relativePath == "config.schema.json") {
        QString reloadError;
        if (!m_manager->reloadService(id, reloadError)) {
            return errorResponse(QHttpServerResponse::StatusCode::InternalServerError,
                                 "file written but reload failed: " + reloadError);
        }
    }

    QJsonObject result;
    result["path"] = relativePath;
    result["size"] = static_cast<qint64>(content.size());
    result["updated"] = true;
    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleServiceFileCreate(const QString& id,
                                                        const QHttpServerRequest& req) {
    auto svcIt = m_manager->services().find(id);
    if (svcIt == m_manager->services().end()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "service not found");
    }

    const QUrlQuery query(req.url());
    const QString relativePath = query.queryItemValue("path");
    if (relativePath.isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, "missing 'path' query parameter");
    }

    QString error;
    const QString absPath = ServiceFileHandler::resolveSafePath(svcIt->serviceDir, relativePath, error);
    if (absPath.isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    if (QFileInfo::exists(absPath)) {
        return errorResponse(QHttpServerResponse::StatusCode::Conflict, "file already exists");
    }

    QJsonObject body;
    if (!parseJsonObjectBody(req, body, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    const QByteArray content = body.value("content").toString().toUtf8();

    if (content.size() > ServiceFileHandler::kMaxFileSize) {
        return errorResponse(static_cast<QHttpServerResponse::StatusCode>(413),
                             "content too large (max 1MB)");
    }

    // Auto-create intermediate directories
    const QFileInfo fi(absPath);
    if (!QDir().mkpath(fi.absolutePath())) {
        return errorResponse(QHttpServerResponse::StatusCode::InternalServerError,
                             "failed to create directory");
    }

    if (!ServiceFileHandler::atomicWrite(absPath, content, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::InternalServerError, error);
    }

    QJsonObject result;
    result["path"] = relativePath;
    result["size"] = static_cast<qint64>(content.size());
    result["created"] = true;
    return jsonResponse(result, QHttpServerResponse::StatusCode::Created);
}

QHttpServerResponse ApiRouter::handleServiceFileDelete(const QString& id,
                                                        const QHttpServerRequest& req) {
    auto svcIt = m_manager->services().find(id);
    if (svcIt == m_manager->services().end()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "service not found");
    }

    const QUrlQuery query(req.url());
    const QString relativePath = query.queryItemValue("path");
    if (relativePath.isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, "missing 'path' query parameter");
    }

    if (ServiceFileHandler::coreFiles().contains(relativePath)) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                             "cannot delete core file: " + relativePath);
    }

    QString error;
    const QString absPath = ServiceFileHandler::resolveSafePath(svcIt->serviceDir, relativePath, error);
    if (absPath.isEmpty()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    if (!QFileInfo::exists(absPath)) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "file not found");
    }

    if (!QFile::remove(absPath)) {
        return errorResponse(QHttpServerResponse::StatusCode::InternalServerError,
                             "failed to delete file");
    }

    return noContentResponse();
}

QHttpServerResponse ApiRouter::handleValidateSchema(const QString& id,
                                                     const QHttpServerRequest& req) {
    if (!m_manager->services().contains(id)) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "service not found");
    }

    QJsonObject body;
    QString error;
    if (!parseJsonObjectBody(req, body, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    if (!body.contains("schema") || !body.value("schema").isObject()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                             "missing required object field: schema");
    }

    const QJsonObject schemaObj = body.value("schema").toObject();
    auto schema = stdiolink_service::ServiceConfigSchema::fromJsonObject(schemaObj, error);
    if (!error.isEmpty()) {
        return jsonResponse(QJsonObject{
            {"valid", false},
            {"error", error}
        });
    }

    return jsonResponse(QJsonObject{
        {"valid", true},
        {"fields", schema.toFieldMetaArray()}
    });
}

QHttpServerResponse ApiRouter::handleGenerateDefaults(const QString& id,
                                                       const QHttpServerRequest& req) {
    Q_UNUSED(req);

    auto svcIt = m_manager->services().find(id);
    if (svcIt == m_manager->services().end()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "service not found");
    }

    const auto& schema = svcIt->configSchema;

    QJsonArray requiredFields;
    for (const QString& name : schema.requiredFieldNames()) {
        requiredFields.append(name);
    }

    QJsonArray optionalFields;
    for (const QString& name : schema.optionalFieldNames()) {
        optionalFields.append(name);
    }

    QJsonObject result;
    result["serviceId"] = id;
    result["config"] = schema.generateDefaults();
    result["requiredFields"] = requiredFields;
    result["optionalFields"] = optionalFields;
    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleValidateConfig(const QString& id,
                                                     const QHttpServerRequest& req) {
    auto svcIt = m_manager->services().find(id);
    if (svcIt == m_manager->services().end()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "service not found");
    }

    QJsonObject body;
    QString error;
    if (!parseJsonObjectBody(req, body, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    if (!body.contains("config") || !body.value("config").isObject()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                             "missing required object field: config");
    }

    const QJsonObject config = body.value("config").toObject();
    const auto vr = stdiolink_service::ServiceConfigValidator::validate(
        svcIt->configSchema, config);

    if (vr.valid) {
        return jsonResponse(QJsonObject{{"valid", true}});
    }

    QJsonArray errors;
    errors.append(QJsonObject{
        {"field", vr.errorField},
        {"message", vr.errorMessage}
    });

    return jsonResponse(QJsonObject{
        {"valid", false},
        {"errors", errors}
    });
}

QHttpServerResponse ApiRouter::handleProjectList(const QHttpServerRequest& req) {
    const QUrlQuery query(req.url());

    const QString filterServiceId = query.queryItemValue("serviceId");
    const QString filterStatus = query.queryItemValue("status");
    const QString filterEnabled = query.queryItemValue("enabled");

    int page = query.hasQueryItem("page") ? qMax(1, query.queryItemValue("page").toInt()) : 1;
    int pageSize = query.hasQueryItem("pageSize")
                       ? qBound(1, query.queryItemValue("pageSize").toInt(), 100)
                       : 20;

    auto* instanceManager = m_manager->instanceManager();

    QVector<const Project*> filtered;
    for (auto it = m_manager->projects().begin(); it != m_manager->projects().end(); ++it) {
        const Project& p = it.value();
        if (!filterServiceId.isEmpty() && p.serviceId != filterServiceId) {
            continue;
        }
        if (!filterEnabled.isEmpty()) {
            const bool en = (filterEnabled == "true");
            if (p.enabled != en) {
                continue;
            }
        }
        if (!filterStatus.isEmpty()) {
            const int running = instanceManager->instanceCount(p.id);
            if (projectStatus(p, running) != filterStatus) {
                continue;
            }
        }
        filtered.append(&p);
    }

    const int total = filtered.size();
    const int offset = (page - 1) * pageSize;

    QJsonArray projects;
    for (int i = offset; i < total && i < offset + pageSize; ++i) {
        projects.append(projectToJson(*filtered[i], instanceManager));
    }

    QJsonObject result;
    result["projects"] = projects;
    result["total"] = total;
    result["page"] = page;
    result["pageSize"] = pageSize;
    return jsonResponse(result);
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

QHttpServerResponse ApiRouter::handleProjectRuntime(const QString& id,
                                                    const QHttpServerRequest& req) {
    Q_UNUSED(req);

    auto pIt = m_manager->projects().find(id);
    if (pIt == m_manager->projects().end()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "project not found");
    }

    const Project& project = pIt.value();
    auto* instanceManager = m_manager->instanceManager();
    const int runningCount = instanceManager->instanceCount(id);

    QJsonArray instances;
    for (const Instance* inst : instanceManager->getInstances(id)) {
        instances.append(instanceToJson(*inst));
    }

    const ScheduleEngine::ProjectRuntimeState runtime =
        m_manager->scheduleEngine()->projectRuntimeState(id);

    QJsonObject schedule;
    schedule["type"] = scheduleTypeToString(project.schedule.type);
    schedule["timerActive"] = runtime.timerActive;
    schedule["restartSuppressed"] = runtime.restartSuppressed;
    schedule["consecutiveFailures"] = runtime.consecutiveFailures;
    schedule["shuttingDown"] = runtime.shuttingDown;
    schedule["autoRestarting"] =
        project.schedule.type == ScheduleType::Daemon
        && project.enabled
        && project.valid
        && !runtime.shuttingDown
        && !runtime.restartSuppressed;

    QJsonObject result;
    result["id"] = project.id;
    result["enabled"] = project.enabled;
    result["valid"] = project.valid;
    if (!project.error.isEmpty()) {
        result["error"] = project.error;
    }
    result["status"] = projectStatus(project, runningCount);
    result["runningInstances"] = runningCount;
    result["instances"] = instances;
    result["schedule"] = schedule;

    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleProjectEnabled(const QString& id,
                                                     const QHttpServerRequest& req) {
    auto& projects = m_manager->projects();
    auto it = projects.find(id);
    if (it == projects.end()) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "project not found");
    }

    QJsonObject body;
    QString error;
    if (!parseJsonObjectBody(req, body, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest, error);
    }

    if (!body.contains("enabled") || !body.value("enabled").isBool()) {
        return errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                             "field 'enabled' is required and must be a bool");
    }

    const bool newEnabled = body.value("enabled").toBool();
    Project updated = it.value();
    updated.enabled = newEnabled;

    if (!ProjectManager::saveProject(m_manager->dataRoot() + "/projects", updated, error)) {
        return errorResponse(QHttpServerResponse::StatusCode::InternalServerError, error);
    }
    *it = updated;

    if (!newEnabled) {
        m_manager->scheduleEngine()->stopProject(id);
        m_manager->instanceManager()->terminateByProject(id);
    } else {
        m_manager->scheduleEngine()->startProject(*it, m_manager->services());
    }

    return jsonResponse(projectToJson(*it, m_manager->instanceManager()));
}

QHttpServerResponse ApiRouter::handleProjectLogs(const QString& id,
                                                  const QHttpServerRequest& req) {
    if (!m_manager->projects().contains(id)) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "project not found");
    }

    QUrlQuery query(req.url());
    bool ok = false;
    int lines = query.queryItemValue("lines").toInt(&ok);
    if (!ok || lines < 1) {
        lines = 100;
    }
    if (lines > 5000) {
        lines = 5000;
    }

    const QString logPath = m_manager->dataRoot() + "/logs/" + id + ".log";

    QJsonArray linesArray;
    if (QFile::exists(logPath)) {
        linesArray = readTailLines(logPath, lines);
    }

    QJsonObject result;
    result["projectId"] = id;
    result["lines"] = linesArray;
    result["logPath"] = logPath;
    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleProjectRuntimeBatch(const QHttpServerRequest& req) {
    const QUrlQuery query(req.url());
    const QString idsParam = query.queryItemValue("ids");

    QStringList requestedIds;
    if (!idsParam.isEmpty()) {
        requestedIds = idsParam.split(',', Qt::SkipEmptyParts);
    }

    auto* instanceManager = m_manager->instanceManager();
    auto* scheduleEngine = m_manager->scheduleEngine();

    QJsonArray runtimes;
    for (auto it = m_manager->projects().begin(); it != m_manager->projects().end(); ++it) {
        if (!requestedIds.isEmpty() && !requestedIds.contains(it.key())) {
            continue;
        }

        const Project& project = it.value();
        const int runningCount = instanceManager->instanceCount(project.id);

        QJsonArray instances;
        for (const Instance* inst : instanceManager->getInstances(project.id)) {
            instances.append(instanceToJson(*inst));
        }

        const ScheduleEngine::ProjectRuntimeState runtime =
            scheduleEngine->projectRuntimeState(project.id);

        QJsonObject schedule;
        schedule["type"] = scheduleTypeToString(project.schedule.type);
        schedule["timerActive"] = runtime.timerActive;
        schedule["restartSuppressed"] = runtime.restartSuppressed;
        schedule["consecutiveFailures"] = runtime.consecutiveFailures;
        schedule["shuttingDown"] = runtime.shuttingDown;
        schedule["autoRestarting"] =
            project.schedule.type == ScheduleType::Daemon
            && project.enabled
            && project.valid
            && !runtime.shuttingDown
            && !runtime.restartSuppressed;

        QJsonObject entry;
        entry["id"] = project.id;
        entry["enabled"] = project.enabled;
        entry["valid"] = project.valid;
        if (!project.error.isEmpty()) {
            entry["error"] = project.error;
        }
        entry["status"] = projectStatus(project, runningCount);
        entry["runningInstances"] = runningCount;
        entry["instances"] = instances;
        entry["schedule"] = schedule;

        runtimes.append(entry);
    }

    return jsonResponse(QJsonObject{{"runtimes", runtimes}});
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

QHttpServerResponse ApiRouter::handleServerStatus(const QHttpServerRequest& req) {
    Q_UNUSED(req);

    const ServerManager::ServerStatus s = m_manager->serverStatus();

    QJsonObject counts;
    counts["services"] = s.serviceCount;
    counts["projects"] = QJsonObject{
        {"total", s.projectTotal},
        {"valid", s.projectValid},
        {"invalid", s.projectInvalid},
        {"enabled", s.projectEnabled},
        {"disabled", s.projectDisabled}
    };
    counts["instances"] = QJsonObject{
        {"total", s.instanceTotal},
        {"running", s.instanceRunning}
    };
    counts["drivers"] = s.driverCount;

    QJsonObject system;
    system["platform"] = s.platform;
    system["cpuCores"] = s.cpuCores;

    QJsonObject result;
    result["status"] = QStringLiteral("ok");
    result["version"] = s.version;
    result["uptimeMs"] = s.uptimeMs;
    result["startedAt"] = s.startedAt.toString(Qt::ISODate);
    result["host"] = s.host;
    result["port"] = s.port;
    result["dataRoot"] = s.dataRoot;
    result["serviceProgram"] = s.serviceProgram;
    result["counts"] = counts;
    result["system"] = system;

    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleInstanceDetail(const QString& id,
                                                     const QHttpServerRequest& req) {
    Q_UNUSED(req);

    const Instance* inst = m_manager->instanceManager()->getInstance(id);
    if (!inst) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "instance not found");
    }

    QJsonObject result = instanceToJson(*inst);
    result["workingDirectory"] = inst->workingDirectory;
    result["logPath"] = inst->logPath;

    QJsonArray cmdLine;
    for (const QString& arg : inst->commandLine) {
        cmdLine.append(arg);
    }
    result["commandLine"] = cmdLine;

    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleDriverDetail(const QString& id,
                                                   const QHttpServerRequest& req) {
    Q_UNUSED(req);

    auto* catalog = m_manager->driverCatalog();
    if (!catalog->hasDriver(id)) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "driver not found");
    }

    const auto cfg = catalog->getConfig(id);

    QJsonObject result;
    result["id"] = cfg.id;
    result["program"] = cfg.program;
    result["metaHash"] = cfg.metaHash;
    if (cfg.meta) {
        result["meta"] = cfg.meta->toJson();
    } else {
        result["meta"] = QJsonValue::Null;
    }

    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleInstanceProcessTree(const QString& id,
                                                          const QHttpServerRequest& req) {
    Q_UNUSED(req);

    const Instance* inst = m_manager->instanceManager()->getInstance(id);
    if (!inst) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "instance not found");
    }
    if (inst->status != "running") {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "instance not running");
    }

    ProcessTreeNode tree = m_manager->processMonitor()->getProcessTree(inst->pid);
    ProcessTreeSummary summary = ProcessMonitor::summarize(tree);

    QJsonObject result;
    result["instanceId"] = id;
    result["rootPid"] = static_cast<qint64>(inst->pid);
    result["tree"] = tree.toJson();
    result["summary"] = summary.toJson();
    return jsonResponse(result);
}

QHttpServerResponse ApiRouter::handleInstanceResources(const QString& id,
                                                        const QHttpServerRequest& req) {
    const Instance* inst = m_manager->instanceManager()->getInstance(id);
    if (!inst) {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "instance not found");
    }
    if (inst->status != "running") {
        return errorResponse(QHttpServerResponse::StatusCode::NotFound, "instance not running");
    }

    const QUrlQuery query(req.url());
    bool includeChildren = true;
    if (query.hasQueryItem("includeChildren")) {
        includeChildren = query.queryItemValue("includeChildren") != "false";
    }

    const QVector<ProcessInfo> processes =
        m_manager->processMonitor()->getProcessFamily(inst->pid, includeChildren);
    const ProcessTreeSummary summary = ProcessMonitor::summarize(processes);

    QJsonArray processArray;
    for (const auto& p : processes) {
        processArray.append(p.toJson());
    }

    QJsonObject result;
    result["instanceId"] = id;
    result["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    result["processes"] = processArray;
    result["summary"] = summary.toJson();
    return jsonResponse(result);
}

void ApiRouter::handleEventStream(const QHttpServerRequest& req,
                                   QHttpServerResponder& responder) {
    auto* handler = m_manager->eventStreamHandler();

    QSet<QString> filters;
    const QUrlQuery query(req.url());
    const QString filterParam = query.queryItemValue("filter");
    if (!filterParam.isEmpty()) {
        const QStringList parts = filterParam.split(',', Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            filters.insert(part.trimmed());
        }
    }

    handler->addConnection(std::move(responder), filters);
}

} // namespace stdiolink_server
