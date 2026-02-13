#pragma once

#include <QHttpHeaders>
#include <QHttpServer>
#include <QString>

namespace stdiolink_server {

class CorsMiddleware {
public:
    explicit CorsMiddleware(const QString& allowedOrigin = "*");

    /// 在 QHttpServer 上注册 CORS 支持：after-request 处理器 + OPTIONS 路由
    void install(QHttpServer& server);

    QString allowedOrigin() const { return m_allowedOrigin; }

    /// 构建 CORS 响应头（供 missingHandler 等直接写 responder 的场景使用）
    QHttpHeaders corsHeaders() const;

private:
    QString m_allowedOrigin;
};

} // namespace stdiolink_server
