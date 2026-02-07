#pragma once

#include <QString>

namespace stdiolink_service {

class ServiceDirectory {
public:
    explicit ServiceDirectory(const QString& dirPath);

    QString manifestPath() const;
    QString entryPath() const;
    QString configSchemaPath() const;

    bool validate(QString& error) const;

private:
    QString m_dirPath;
};

} // namespace stdiolink_service
