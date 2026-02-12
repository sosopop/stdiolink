#include "service_file_handler.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

namespace stdiolink_server {

bool ServiceFileHandler::isPathSafe(const QString& serviceDir,
                                     const QString& relativePath) {
    if (relativePath.isEmpty()) {
        return false;
    }

    if (QDir::isAbsolutePath(relativePath)) {
        return false;
    }

    const QStringList segments = relativePath.split(QRegularExpression("[/\\\\]"));
    for (const auto& seg : segments) {
        if (seg == "..") {
            return false;
        }
    }

    const QString basePath = QDir::cleanPath(QDir(serviceDir).absolutePath());
    const QString resolved = QDir::cleanPath(
        QDir(serviceDir).absoluteFilePath(relativePath));
    if (!resolved.startsWith(basePath + "/")) {
        return false;
    }

    QFileInfo resolvedInfo(resolved);
    if (resolvedInfo.exists() && resolvedInfo.isSymLink()) {
        return false;
    }

    QDir dir(serviceDir);
    for (const auto& seg : segments) {
        if (seg.isEmpty() || seg == ".") {
            continue;
        }
        const QString stepPath = dir.absoluteFilePath(seg);
        QFileInfo stepInfo(stepPath);
        if (stepInfo.exists() && stepInfo.isSymLink()) {
            return false;
        }
        dir.setPath(stepPath);
    }

    return true;
}

QString ServiceFileHandler::resolveSafePath(const QString& serviceDir,
                                             const QString& relativePath,
                                             QString& error) {
    if (!isPathSafe(serviceDir, relativePath)) {
        error = "path rejected: unsafe or invalid";
        return {};
    }
    error.clear();
    return QDir::cleanPath(QDir(serviceDir).absoluteFilePath(relativePath));
}

bool ServiceFileHandler::atomicWrite(const QString& filePath,
                                      const QByteArray& content,
                                      QString& error) {
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        error = QString("failed to open for writing: %1").arg(file.errorString());
        return false;
    }
    if (file.write(content) != content.size()) {
        file.cancelWriting();
        error = "write incomplete";
        return false;
    }
    if (!file.commit()) {
        error = QString("commit failed: %1").arg(file.errorString());
        return false;
    }
    return true;
}

QVector<FileInfo> ServiceFileHandler::listFiles(const QString& serviceDir) {
    QVector<FileInfo> result;
    const QString basePath = QDir::cleanPath(QDir(serviceDir).absolutePath());

    QDirIterator it(serviceDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.isSymLink()) {
            continue;
        }

        const QString absPath = QDir::cleanPath(fi.absoluteFilePath());
        const QString relPath = absPath.mid(basePath.length() + 1);

        FileInfo info;
        info.name = fi.fileName();
        info.path = relPath;
        info.size = fi.size();
        info.modifiedAt = fi.lastModified().toUTC().toString(Qt::ISODate);
        info.type = inferFileType(fi.fileName());
        result.append(info);
    }

    return result;
}

QString ServiceFileHandler::inferFileType(const QString& fileName) {
    if (fileName.endsWith(".json")) {
        return "json";
    }
    if (fileName.endsWith(".js") || fileName.endsWith(".mjs")) {
        return "javascript";
    }
    if (fileName.endsWith(".ts")) {
        return "typescript";
    }
    if (fileName.endsWith(".md") || fileName.endsWith(".txt")
        || fileName.endsWith(".yaml") || fileName.endsWith(".yml")
        || fileName.endsWith(".toml") || fileName.endsWith(".cfg")
        || fileName.endsWith(".ini") || fileName.endsWith(".csv")) {
        return "text";
    }
    return "unknown";
}

const QStringList& ServiceFileHandler::coreFiles() {
    static const QStringList files = {
        "manifest.json",
        "index.js",
        "config.schema.json"
    };
    return files;
}

} // namespace stdiolink_server
