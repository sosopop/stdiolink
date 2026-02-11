#include "project.h"

#include <QSet>

namespace stdiolink_server {

Project Project::fromJson(const QString& id,
                          const QJsonObject& obj,
                          QString& parseError) {
    Project project;
    project.id = id;

    static const QSet<QString> knownFields = {
        "id", "name", "serviceId", "enabled", "schedule", "config"
    };
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!knownFields.contains(it.key())) {
            parseError = "unknown field in project config: " + it.key();
            return project;
        }
    }

    if (obj.contains("id")) {
        if (!obj.value("id").isString()) {
            parseError = "project field 'id' must be a string";
            return project;
        }
        const QString bodyId = obj.value("id").toString();
        if (!bodyId.isEmpty() && bodyId != id) {
            parseError = "project id mismatch: body=" + bodyId + ", path=" + id;
            return project;
        }
    }

    if (!obj.contains("name") || !obj.value("name").isString()) {
        parseError = "missing required string field: name";
        return project;
    }
    project.name = obj.value("name").toString();
    if (project.name.isEmpty()) {
        parseError = "project name cannot be empty";
        return project;
    }

    if (!obj.contains("serviceId") || !obj.value("serviceId").isString()) {
        parseError = "missing required string field: serviceId";
        return project;
    }
    project.serviceId = obj.value("serviceId").toString();
    if (project.serviceId.isEmpty()) {
        parseError = "project serviceId cannot be empty";
        return project;
    }

    if (obj.contains("enabled")) {
        if (!obj.value("enabled").isBool()) {
            parseError = "project field 'enabled' must be a bool";
            return project;
        }
        project.enabled = obj.value("enabled").toBool(true);
    }

    if (obj.contains("schedule")) {
        if (!obj.value("schedule").isObject()) {
            parseError = "project field 'schedule' must be an object";
            return project;
        }
        QString scheduleErr;
        project.schedule = Schedule::fromJson(obj.value("schedule").toObject(), scheduleErr);
        if (!scheduleErr.isEmpty()) {
            parseError = scheduleErr;
            return project;
        }
    }

    if (obj.contains("config")) {
        if (!obj.value("config").isObject()) {
            parseError = "project field 'config' must be an object";
            return project;
        }
        project.config = obj.value("config").toObject();
    }

    parseError.clear();
    return project;
}

QJsonObject Project::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["serviceId"] = serviceId;
    obj["enabled"] = enabled;
    obj["schedule"] = schedule.toJson();
    obj["config"] = config;
    return obj;
}

} // namespace stdiolink_server
