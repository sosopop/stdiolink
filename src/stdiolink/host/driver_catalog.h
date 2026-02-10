#pragma once

#include "stdiolink/protocol/meta_types.h"
#include "stdiolink/stdiolink_export.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <memory>

namespace stdiolink {

/**
 * Driver 配置
 */
struct STDIOLINK_API DriverConfig {
    QString id;
    QString program;
    QStringList args;
    std::shared_ptr<meta::DriverMeta> meta;
    QString metaHash;
};

/**
 * Driver 扫描器
 * 从目录读取 driver.meta.json 并构建 DriverConfig 集合
 */
class STDIOLINK_API DriverScanner {
public:
    struct ScanStats {
        int scannedDirectories = 0;
        int loadedDrivers = 0;
        int invalidMetaFiles = 0;
    };

    QHash<QString, DriverConfig> scanDirectory(const QString& path, ScanStats* stats = nullptr) const;

private:
    bool loadMetaFromFile(const QString& path, DriverConfig& config) const;
    static QString computeMetaHash(const QByteArray& data);
    static QString findExecutableInDirectory(const QString& dirPath);
};

/**
 * Driver 目录快照
 * 非单例、无全局状态，由调用方持有
 */
class STDIOLINK_API DriverCatalog {
public:
    void replaceAll(const QHash<QString, DriverConfig>& drivers);
    void clear();

    QStringList listDrivers() const;
    DriverConfig getConfig(const QString& id) const;
    bool hasDriver(const QString& id) const;

    bool healthCheck(const QString& id) const;
    void healthCheckAll() const;

private:
    QHash<QString, DriverConfig> m_drivers;
};

} // namespace stdiolink
