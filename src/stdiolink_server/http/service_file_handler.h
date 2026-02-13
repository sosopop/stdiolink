#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace stdiolink_server {

struct FileInfo {
    QString name; // file name
    QString path; // relative path
    qint64 size = 0;
    QString modifiedAt; // ISO 8601
    QString type;       // json / javascript / text / unknown
};

class ServiceFileHandler {
public:
    /// Path safety check — returns true only if relativePath stays within serviceDir
    static bool isPathSafe(const QString& serviceDir, const QString& relativePath);

    /// Resolve a safe absolute path (validates + returns absolute path, or sets error)
    static QString resolveSafePath(const QString& serviceDir, const QString& relativePath,
                                   QString& error);

    /// Atomic write using QSaveFile
    static bool atomicWrite(const QString& filePath, const QByteArray& content, QString& error);

    /// Recursively list files under serviceDir
    static QVector<FileInfo> listFiles(const QString& serviceDir);

    /// Infer file type from extension
    static QString inferFileType(const QString& fileName);

    /// Core files that cannot be deleted
    static const QStringList& coreFiles();

    static constexpr qint64 kMaxFileSize = 1 * 1024 * 1024; // 1MB
};

} // namespace stdiolink_server
