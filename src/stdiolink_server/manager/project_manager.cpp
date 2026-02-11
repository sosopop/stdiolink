#include "project_manager.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>

#include "config/service_config_validator.h"

using namespace stdiolink_service;

namespace stdiolink_server {

bool ProjectManager::isValidProjectId(const QString& id) {
    static const QRegularExpression re("^[A-Za-z0-9_-]+$");
    return !id.isEmpty() && re.match(id).hasMatch();
}

Project ProjectManager::loadOne(const QString& filePath,
                                const QString& id) {
    Project project;
    project.id = id;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        project.valid = false;
        project.error = "cannot open file: " + filePath;
        return project;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        project.valid = false;
        project.error = "JSON parse error: " + parseErr.errorString();
        return project;
    }

    if (!doc.isObject()) {
        project.valid = false;
        project.error = "project file must contain a JSON object";
        return project;
    }

    QString parseError;
    project = Project::fromJson(id, doc.object(), parseError);
    if (!parseError.isEmpty()) {
        project.valid = false;
        project.error = parseError;
        return project;
    }

    project.valid = true;
    project.error.clear();
    return project;
}

QMap<QString, Project> ProjectManager::loadAll(const QString& projectsDir,
                                                const QMap<QString, ServiceInfo>& services,
                                                LoadStats* stats) {
    QMap<QString, Project> result;

    QDir dir(projectsDir);
    if (!dir.exists()) {
        return result;
    }

    const auto entries = dir.entryList({"*.json"}, QDir::Files);
    for (const QString& entry : entries) {
        const QString id = entry.chopped(5);
        if (!isValidProjectId(id)) {
            qWarning("ProjectManager: skip invalid id filename: %s", qUtf8Printable(entry));
            continue;
        }

        Project project = loadOne(dir.absoluteFilePath(entry), id);
        if (project.valid) {
            (void)validateProject(project, services);
        }

        if (project.valid) {
            if (stats) {
                stats->loaded++;
            }
        } else {
            if (stats) {
                stats->invalid++;
            }
            qWarning("ProjectManager: %s invalid: %s",
                     qUtf8Printable(id),
                     qUtf8Printable(project.error));
        }

        result.insert(id, project);
    }

    return result;
}

bool ProjectManager::validateProject(Project& project,
                                     const QMap<QString, ServiceInfo>& services) {
    if (!services.contains(project.serviceId)) {
        project.valid = false;
        project.error = "service not found: " + project.serviceId;
        return false;
    }

    const ServiceInfo& service = services[project.serviceId];

    QJsonObject merged;
    auto result = ServiceConfigValidator::mergeAndValidate(
        service.configSchema,
        QJsonObject{},
        project.config,
        UnknownFieldPolicy::Reject,
        merged);

    if (!result.valid) {
        project.valid = false;
        project.error = result.toString();
        return false;
    }

    project.config = merged;
    project.valid = true;
    project.error.clear();
    return true;
}

bool ProjectManager::saveProject(const QString& projectsDir,
                                 const Project& project,
                                 QString& error) {
    if (!isValidProjectId(project.id)) {
        error = "invalid project id: " + project.id;
        return false;
    }

    if (!QDir().mkpath(projectsDir)) {
        error = "cannot create projects directory: " + projectsDir;
        return false;
    }

    const QString filePath = projectsDir + "/" + project.id + ".json";
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = "cannot write file: " + filePath;
        return false;
    }

    const QJsonDocument doc(project.toJson());
    file.write(doc.toJson(QJsonDocument::Indented));

    error.clear();
    return true;
}

bool ProjectManager::removeProject(const QString& projectsDir,
                                   const QString& id,
                                   QString& error) {
    const QString filePath = projectsDir + "/" + id + ".json";
    if (!QFile::exists(filePath)) {
        error = "project not found: " + id;
        return false;
    }
    if (!QFile::remove(filePath)) {
        error = "cannot remove file: " + filePath;
        return false;
    }

    error.clear();
    return true;
}

} // namespace stdiolink_server
