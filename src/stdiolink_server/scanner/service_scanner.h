#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>

#include "config/service_config_schema.h"
#include "config/service_manifest.h"

namespace stdiolink_server {

struct ServiceInfo {
    QString id;
    QString name;
    QString version;
    QString serviceDir;
    stdiolink_service::ServiceManifest manifest;
    stdiolink_service::ServiceConfigSchema configSchema;
    QJsonObject rawConfigSchema;
    bool hasSchema = true;
    bool valid = true;
    QString error;
};

class ServiceScanner {
public:
    struct ScanStats {
        int scannedDirs = 0;
        int loadedServices = 0;
        int failedServices = 0;
    };

    QMap<QString, ServiceInfo> scan(const QString& servicesDir,
                                    ScanStats* stats = nullptr) const;

private:
    ServiceInfo loadService(const QString& serviceDir) const;
};

} // namespace stdiolink_server
