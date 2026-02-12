#pragma once

#include <QHttpHeaders>
#include <QHttpServer>
#include <QString>

namespace stdiolink_server {

class CorsMiddleware {
public:
    explicit CorsMiddleware(QString allowedOrigin = QStringLiteral("*"));

    /// 在 QHttpServer 上注册 CORS 支持：after-request 处理器 + OPTIONS 路由
    void install(QHttpServer& server);

    /// 构建 CORS 响应头（供 missingHandler 等绕过 after-request 的场景使用）
    static QHttpHeaders buildCorsHeaders(const QString& origin);

    QString allowedOrigin() const { return m_allowedOrigin; }

private:
    QString m_allowedOrigin;
};

} // namespace stdiolink_server
