#include "service_directory.h"

#include <QDir>
#include <QFileInfo>

namespace stdiolink_service {

ServiceDirectory::ServiceDirectory(const QString& dirPath)
    : m_dirPath(dirPath) {}

QString ServiceDirectory::manifestPath() const {
    return QDir(m_dirPath).filePath("manifest.json");
}

QString ServiceDirectory::entryPath() const {
    return QDir(m_dirPath).filePath("index.js");
}

QString ServiceDirectory::configSchemaPath() const {
    return QDir(m_dirPath).filePath("config.schema.json");
}

bool ServiceDirectory::validate(QString& error) const {
    QFileInfo dirInfo(m_dirPath);
    if (!dirInfo.exists() || !dirInfo.isDir()) {
        error = QString("service directory does not exist: %1").arg(m_dirPath);
        return false;
    }

    if (!QFileInfo::exists(manifestPath())) {
        error = QString("missing manifest.json in service directory: %1").arg(m_dirPath);
        return false;
    }
    if (!QFileInfo::exists(entryPath())) {
        error = QString("missing index.js in service directory: %1").arg(m_dirPath);
        return false;
    }
    if (!QFileInfo::exists(configSchemaPath())) {
        error = QString("missing config.schema.json in service directory: %1").arg(m_dirPath);
        return false;
    }

    error.clear();
    return true;
}

} // namespace stdiolink_service
