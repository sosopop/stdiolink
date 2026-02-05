#include "driver_registry.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>

namespace stdiolink {

DriverRegistry& DriverRegistry::instance() {
    static DriverRegistry inst;
    return inst;
}

void DriverRegistry::registerDriver(const QString& id, const DriverConfig& config) {
    m_drivers[id] = config;
}

void DriverRegistry::unregisterDriver(const QString& id) {
    m_drivers.remove(id);
}

QStringList DriverRegistry::listDrivers() const {
    return m_drivers.keys();
}

DriverConfig DriverRegistry::getConfig(const QString& id) const {
    return m_drivers.value(id);
}

bool DriverRegistry::hasDriver(const QString& id) const {
    return m_drivers.contains(id);
}

bool DriverRegistry::healthCheck(const QString& id) {
    if (!m_drivers.contains(id)) {
        return false;
    }

    const auto& config = m_drivers[id];
    if (config.program.isEmpty()) {
        return false;
    }

    // 检查程序文件是否存在
    return QFile::exists(config.program);
}

void DriverRegistry::healthCheckAll() {
    for (auto it = m_drivers.begin(); it != m_drivers.end(); ++it) {
        healthCheck(it.key());
    }
}

void DriverRegistry::scanDirectory(const QString& path) {
    QDirIterator it(path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString dirPath = it.next();
        QString metaPath = dirPath + "/driver.meta.json";

        if (QFile::exists(metaPath)) {
            DriverConfig config;
            if (loadMetaFromFile(metaPath, config)) {
                // 查找可执行文件
                QDir dir(dirPath);
                QStringList exeFilters;
#ifdef Q_OS_WIN
                exeFilters << "*.exe";
#else
                exeFilters << "*";
#endif
                auto exeFiles = dir.entryList(exeFilters, QDir::Files | QDir::Executable);
                if (!exeFiles.isEmpty()) {
                    config.program = dir.absoluteFilePath(exeFiles.first());
                }

                registerDriver(config.id, config);
            }
        }
    }
}

void DriverRegistry::clear() {
    m_drivers.clear();
}

bool DriverRegistry::loadMetaFromFile(const QString& path, DriverConfig& config) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        return false;
    }

    if (!doc.isObject()) {
        return false;
    }

    config.meta = std::make_shared<meta::DriverMeta>(
        meta::DriverMeta::fromJson(doc.object()));
    config.id = config.meta->info.id;
    config.metaHash = computeMetaHash(data);

    return true;
}

QString DriverRegistry::computeMetaHash(const QByteArray& data) {
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

} // namespace stdiolink
