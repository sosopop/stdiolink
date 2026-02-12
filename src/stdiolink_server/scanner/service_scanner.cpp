#include "service_scanner.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>

#include "config/service_directory.h"

using namespace stdiolink_service;

namespace stdiolink_server {

QMap<QString, ServiceInfo> ServiceScanner::scan(const QString& servicesDir,
                                                 ScanStats* stats) const {
    QMap<QString, ServiceInfo> result;
    QDir dir(servicesDir);
    if (!dir.exists()) {
        return result;
    }

    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        if (stats) {
            stats->scannedDirs++;
        }

        ServiceInfo info = loadService(dir.absoluteFilePath(entry));
        if (!info.valid) {
            if (stats) {
                stats->failedServices++;
            }
            qWarning("ServiceScanner: skip %s: %s",
                     qUtf8Printable(entry),
                     qUtf8Printable(info.error));
            continue;
        }

        if (result.contains(info.id)) {
            if (stats) {
                stats->failedServices++;
            }
            qWarning("ServiceScanner: duplicate service id '%s' at %s",
                     qUtf8Printable(info.id),
                     qUtf8Printable(info.serviceDir));
            continue;
        }

        if (stats) {
            stats->loadedServices++;
        }
        result.insert(info.id, info);
    }

    return result;
}

ServiceInfo ServiceScanner::loadService(const QString& serviceDir) const {
    ServiceInfo info;
    info.serviceDir = serviceDir;

    ServiceDirectory svcDir(serviceDir);
    QString dirErr;
    if (!svcDir.validate(dirErr)) {
        info.valid = false;
        info.error = dirErr;
        return info;
    }

    QString manifestErr;
    info.manifest = ServiceManifest::loadFromFile(svcDir.manifestPath(), manifestErr);
    if (!manifestErr.isEmpty()) {
        info.valid = false;
        info.error = manifestErr;
        return info;
    }

    info.id = info.manifest.id;
    info.name = info.manifest.name;
    info.version = info.manifest.version;

    QString schemaErr;
    info.configSchema = ServiceConfigSchema::fromJsonFile(svcDir.configSchemaPath(), schemaErr);
    if (!schemaErr.isEmpty()) {
        info.valid = false;
        info.error = schemaErr;
        return info;
    }

    QFile schemaFile(svcDir.configSchemaPath());
    if (!schemaFile.open(QIODevice::ReadOnly)) {
        info.valid = false;
        info.error = "cannot open config schema file: " + svcDir.configSchemaPath();
        return info;
    }

    QJsonParseError parseErr;
    const QJsonDocument rawSchemaDoc = QJsonDocument::fromJson(schemaFile.readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !rawSchemaDoc.isObject()) {
        info.valid = false;
        info.error = "config.schema.json parse error: " + parseErr.errorString();
        return info;
    }

    info.rawConfigSchema = rawSchemaDoc.object();
    return info;
}

std::optional<ServiceInfo> ServiceScanner::loadSingle(const QString& serviceDir,
                                                       QString& error) const {
    ServiceInfo info = loadService(serviceDir);
    if (!info.valid) {
        error = info.error;
        return std::nullopt;
    }
    error.clear();
    return info;
}

} // namespace stdiolink_server
