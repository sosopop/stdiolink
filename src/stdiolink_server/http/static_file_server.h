#pragma once

#include <QByteArray>
#include <QHttpServerResponse>
#include <QString>

namespace stdiolink_server {

class StaticFileServer {
public:
    /// 原始服务结果，用于 missingHandler 中手动写入 responder
    struct ServeResult {
        bool found = false;
        QByteArray body;
        QByteArray mimeType;
        QByteArray cacheControl;
    };

    explicit StaticFileServer(const QString& rootDir);

    /// 检查 webui 目录是否有效（存在且包含 index.html）
    bool isValid() const;

    /// 根据请求路径返回静态文件响应
    QHttpServerResponse serve(const QString& path) const;

    /// SPA 回退：返回 index.html
    QHttpServerResponse serveIndex() const;

    /// 原始服务：返回文件内容和元数据（供 missingHandler 使用）
    ServeResult serveRaw(const QString& path) const;

    /// 原始 SPA 回退
    ServeResult serveIndexRaw() const;

    /// 获取根目录
    QString rootDir() const;

private:
    QString resolveSafePath(const QString& urlPath) const;
    static QByteArray mimeType(const QString& filePath);
    static QByteArray cacheControl(const QString& filePath);

    QString m_rootDir;
    bool m_valid = false;
};

} // namespace stdiolink_server
