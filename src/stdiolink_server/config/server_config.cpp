#include "server_config.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace stdiolink_server {

namespace {

bool isValidLogLevel(const QString& level) {
    return level == "debug" || level == "info"
        || level == "warn" || level == "error";
}

} // namespace

ServerConfig ServerConfig::loadFromFile(const QString& filePath, QString& error) {
    ServerConfig cfg;

    if (!QFileInfo::exists(filePath)) {
        error.clear();
        return cfg;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        error = "cannot open config file: " + filePath;
        return cfg;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        error = "config.json parse error: " + parseErr.errorString();
        return cfg;
    }
    if (!doc.isObject()) {
        error = "config.json must contain a JSON object";
        return cfg;
    }

    const QJsonObject obj = doc.object();
    static const QSet<QString> known = {"port", "host", "logLevel", "serviceProgram", "corsOrigin", "webuiDir",
                                        "logMaxBytes", "logMaxFiles"};
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!known.contains(it.key())) {
            error = "unknown field in config.json: " + it.key();
            return cfg;
        }
    }

    if (obj.contains("port")) {
        if (!obj.value("port").isDouble()) {
            error = "config field 'port' must be an integer";
            return cfg;
        }
        cfg.port = obj.value("port").toInt();
        if (cfg.port < 1 || cfg.port > 65535) {
            error = "config field 'port' out of range";
            return cfg;
        }
    }

    if (obj.contains("host")) {
        if (!obj.value("host").isString()) {
            error = "config field 'host' must be a string";
            return cfg;
        }
        cfg.host = obj.value("host").toString();
        if (cfg.host.isEmpty()) {
            error = "config field 'host' cannot be empty";
            return cfg;
        }
    }

    if (obj.contains("logLevel")) {
        if (!obj.value("logLevel").isString()) {
            error = "config field 'logLevel' must be a string";
            return cfg;
        }
        cfg.logLevel = obj.value("logLevel").toString();
        if (!isValidLogLevel(cfg.logLevel)) {
            error = "invalid config logLevel: " + cfg.logLevel;
            return cfg;
        }
    }

    if (obj.contains("serviceProgram")) {
        if (!obj.value("serviceProgram").isString()) {
            error = "config field 'serviceProgram' must be a string";
            return cfg;
        }
        cfg.serviceProgram = obj.value("serviceProgram").toString();
    }

    if (obj.contains("corsOrigin")) {
        if (!obj.value("corsOrigin").isString()) {
            error = "config field 'corsOrigin' must be a string";
            return cfg;
        }
        cfg.corsOrigin = obj.value("corsOrigin").toString();
        if (cfg.corsOrigin.isEmpty()) {
            error = "config field 'corsOrigin' cannot be empty";
            return cfg;
        }
    }

    if (obj.contains("webuiDir")) {
        if (!obj.value("webuiDir").isString()) {
            error = "config field 'webuiDir' must be a string";
            return cfg;
        }
        cfg.webuiDir = obj.value("webuiDir").toString();
    }

    if (obj.contains("logMaxBytes")) {
        if (!obj.value("logMaxBytes").isDouble()) {
            error = "config field 'logMaxBytes' must be a number";
            return cfg;
        }
        cfg.logMaxBytes = static_cast<qint64>(obj.value("logMaxBytes").toDouble());
        if (cfg.logMaxBytes < 1024 * 1024) {
            error = "config field 'logMaxBytes' must be >= 1048576 (1MB)";
            return cfg;
        }
    }

    if (obj.contains("logMaxFiles")) {
        if (!obj.value("logMaxFiles").isDouble()) {
            error = "config field 'logMaxFiles' must be a number";
            return cfg;
        }
        cfg.logMaxFiles = obj.value("logMaxFiles").toInt();
        if (cfg.logMaxFiles < 1 || cfg.logMaxFiles > 100) {
            error = "config field 'logMaxFiles' must be between 1 and 100";
            return cfg;
        }
    }

    error.clear();
    return cfg;
}

void ServerConfig::applyArgs(const ServerArgs& args) {
    if (args.hasPort) {
        port = args.port;
    }
    if (args.hasHost) {
        host = args.host;
    }
    if (args.hasLogLevel) {
        logLevel = args.logLevel;
    }
    if (args.hasWebuiDir) {
        webuiDir = args.webuiDir;
    }
}

} // namespace stdiolink_server
