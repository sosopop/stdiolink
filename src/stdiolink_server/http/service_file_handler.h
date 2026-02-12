#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace stdiolink_server {

struct FileInfo {
    QString name;
    QString path;
    qint64 size = 0;
    QString modifiedAt;
    QString type;
};

class ServiceFileHandler {
public:
    static bool isPathSafe(const QString& serviceDir, const QString& relativePath);

    static QString resolveSafePath(const QString& serviceDir,
                                   const QString& relativePath,
                                   QString& error);

    static bool atomicWrite(const QString& filePath,
                            const QByteArray& content,
                            QString& error);

    static QVector<FileInfo> listFiles(const QString& serviceDir);

    static QString inferFileType(const QString& fileName);

    static const QStringList& coreFiles();

    static constexpr qint64 kMaxFileSize = 1 * 1024 * 1024; // 1MB
};

} // namespace stdiolink_server
