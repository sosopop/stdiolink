#include "driver_catalog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>

#include "stdiolink/platform/platform_utils.h"

namespace stdiolink {

QHash<QString, DriverConfig> DriverScanner::scanDirectory(const QString& path,
                                                          ScanStats* stats) const {
    QHash<QString, DriverConfig> scanned;
    QDirIterator it(path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        const QString dirPath = it.next();
        if (stats) {
            stats->scannedDirectories++;
        }

        const QString metaPath = dirPath + "/driver.meta.json";
        if (!QFile::exists(metaPath)) {
            continue;
        }

        DriverConfig config;
        if (!loadMetaFromFile(metaPath, config) || config.id.isEmpty()) {
            if (stats) {
                stats->invalidMetaFiles++;
            }
            continue;
        }

        config.program = findExecutableInDirectory(dirPath);
        scanned[config.id] = config;
        if (stats) {
            stats->loadedDrivers++;
        }
    }

    return scanned;
}

bool DriverScanner::loadMetaFromFile(const QString& path, DriverConfig& config) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    config.meta = std::make_shared<meta::DriverMeta>(meta::DriverMeta::fromJson(doc.object()));
    config.id = config.meta->info.id;
    config.metaHash = computeMetaHash(data);
    return true;
}

QString DriverScanner::computeMetaHash(const QByteArray& data) {
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

QString DriverScanner::findExecutableInDirectory(const QString& dirPath) {
    QDir dir(dirPath);
    QStringList exeFilters;
    exeFilters << PlatformUtils::executableFilter();
    const QStringList exeFiles = dir.entryList(exeFilters, QDir::Files | QDir::Executable);
    if (exeFiles.isEmpty()) {
        return {};
    }
    return dir.absoluteFilePath(exeFiles.first());
}

void DriverCatalog::replaceAll(const QHash<QString, DriverConfig>& drivers) {
    m_drivers = drivers;
}

void DriverCatalog::clear() {
    m_drivers.clear();
}

QStringList DriverCatalog::listDrivers() const {
    return m_drivers.keys();
}

DriverConfig DriverCatalog::getConfig(const QString& id) const {
    return m_drivers.value(id);
}

bool DriverCatalog::hasDriver(const QString& id) const {
    return m_drivers.contains(id);
}

bool DriverCatalog::healthCheck(const QString& id) const {
    if (!m_drivers.contains(id)) {
        return false;
    }
    const auto& config = m_drivers[id];
    if (config.program.isEmpty()) {
        return false;
    }
    return QFile::exists(config.program);
}

void DriverCatalog::healthCheckAll() const {
    for (auto it = m_drivers.begin(); it != m_drivers.end(); ++it) {
        (void)healthCheck(it.key());
    }
}

} // namespace stdiolink
