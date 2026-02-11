#pragma once

#include <QHash>
#include <QString>

#include "stdiolink/host/driver_catalog.h"

namespace stdiolink_server {

class DriverManagerScanner {
public:
    struct ScanStats {
        int scanned = 0;
        int updated = 0;
        int newlyFailed = 0;
        int skippedFailed = 0;
    };

    QHash<QString, stdiolink::DriverConfig> scan(const QString& driversDir,
                                                  bool refreshMeta = true,
                                                  ScanStats* stats = nullptr) const;

private:
    static constexpr int kExportTimeoutMs = 10000;

    bool tryExportMeta(const QString& executable,
                       const QString& metaPath) const;
    static QString findDriverExecutable(const QString& dirPath);
    static bool loadMetaFile(const QString& metaPath,
                             stdiolink::DriverConfig& config);
    static QString computeMetaHash(const QByteArray& data);
    static bool isFailedDir(const QString& dirName);
    static bool markFailed(const QString& dirPath);
};

} // namespace stdiolink_server
