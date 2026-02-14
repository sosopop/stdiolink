#include "static_file_server.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpServerResponder>

namespace stdiolink_server {

namespace {

const QMap<QString, QByteArray> kMimeTypes = {
    {"html", "text/html; charset=utf-8"},
    {"js", "application/javascript; charset=utf-8"},
    {"css", "text/css; charset=utf-8"},
    {"json", "application/json; charset=utf-8"},
    {"png", "image/png"},
    {"jpg", "image/jpeg"},
    {"jpeg", "image/jpeg"},
    {"gif", "image/gif"},
    {"svg", "image/svg+xml"},
    {"ico", "image/x-icon"},
    {"woff", "font/woff"},
    {"woff2", "font/woff2"},
    {"ttf", "font/ttf"},
    {"map", "application/json"},
};

constexpr qint64 kMaxFileSize = 10 * 1024 * 1024; // 10MB

} // namespace

StaticFileServer::StaticFileServer(const QString& rootDir)
    : m_rootDir(QDir::cleanPath(rootDir)) {
    QFileInfo indexFile(m_rootDir + "/index.html");
    m_valid = indexFile.exists() && indexFile.isFile();
}

bool StaticFileServer::isValid() const {
    return m_valid;
}

QString StaticFileServer::rootDir() const {
    return m_rootDir;
}

QString StaticFileServer::resolveSafePath(const QString& urlPath) const {
    if (urlPath.contains(".."))
        return {};

    QString cleaned = QDir::cleanPath(urlPath);
    if (cleaned.startsWith('/'))
        cleaned = cleaned.mid(1);

    QString fullPath = QDir::cleanPath(m_rootDir + "/" + cleaned);

    // 验证仍在 rootDir 内
    if (!fullPath.startsWith(m_rootDir + "/") && fullPath != m_rootDir)
        return {};

    QFileInfo fi(fullPath);

    // 不跟随符号链接
    if (fi.isSymLink())
        return {};

    if (!fi.exists() || !fi.isFile())
        return {};

    // 文件大小上限
    if (fi.size() > kMaxFileSize)
        return {};

    return fullPath;
}

QByteArray StaticFileServer::mimeType(const QString& filePath) {
    const int dotPos = filePath.lastIndexOf('.');
    if (dotPos < 0)
        return "application/octet-stream";

    const QString ext = filePath.mid(dotPos + 1).toLower();
    return kMimeTypes.value(ext, "application/octet-stream");
}

QByteArray StaticFileServer::cacheControl(const QString& filePath) {
    const QString fileName = QFileInfo(filePath).fileName();

    if (fileName == "index.html")
        return "no-cache, no-store, must-revalidate";

    // /assets/* 目录下带 hash 的文件长缓存
    if (filePath.contains("/assets/"))
        return "public, max-age=31536000, immutable";

    return "public, max-age=3600";
}

QHttpServerResponse StaticFileServer::serve(const QString& path) const {
    if (!m_valid)
        return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);

    QString filePath = resolveSafePath(path);
    if (filePath.isEmpty())
        return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QHttpServerResponse(QHttpServerResponder::StatusCode::InternalServerError);

    QByteArray content = file.readAll();
    QByteArray mime = mimeType(filePath);
    QByteArray cache = cacheControl(filePath);

    return QHttpServerResponse(mime, content, QHttpServerResponder::StatusCode::Ok);
}

QHttpServerResponse StaticFileServer::serveIndex() const {
    QFile file(m_rootDir + "/index.html");
    if (!file.open(QIODevice::ReadOnly))
        return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);

    QByteArray content = file.readAll();
    return QHttpServerResponse("text/html; charset=utf-8", content,
                               QHttpServerResponder::StatusCode::Ok);
}

StaticFileServer::ServeResult StaticFileServer::serveRaw(const QString& path) const {
    ServeResult result;
    if (!m_valid)
        return result;

    QString filePath = resolveSafePath(path);
    if (filePath.isEmpty())
        return result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return result;

    result.found = true;
    result.body = file.readAll();
    result.mimeType = mimeType(filePath);
    result.cacheControl = cacheControl(filePath);
    return result;
}

StaticFileServer::ServeResult StaticFileServer::serveIndexRaw() const {
    ServeResult result;

    QFile file(m_rootDir + "/index.html");
    if (!file.open(QIODevice::ReadOnly))
        return result;

    result.found = true;
    result.body = file.readAll();
    result.mimeType = "text/html; charset=utf-8";
    result.cacheControl = "no-cache, no-store, must-revalidate";
    return result;
}

} // namespace stdiolink_server
