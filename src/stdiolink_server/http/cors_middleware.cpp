#include "cors_middleware.h"

#include <QHttpHeaders>
#include <QHttpServerResponse>

namespace stdiolink_server {

CorsMiddleware::CorsMiddleware(const QString& allowedOrigin) : m_allowedOrigin(allowedOrigin) {}

void CorsMiddleware::install(QHttpServer& server) {
    // 1. after-request handler: inject CORS headers into all responses
    server.addAfterRequestHandler(
        &server, [origin = m_allowedOrigin](const QHttpServerRequest&, QHttpServerResponse& resp) {
            QHttpHeaders headers = resp.headers();
            headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin,
                                    origin.toUtf8());
            headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::AccessControlAllowMethods,
                                    "GET, POST, PUT, PATCH, DELETE, OPTIONS");
            headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::AccessControlAllowHeaders,
                                    "Content-Type, Accept, Authorization, Origin");
            headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::AccessControlMaxAge, "86400");
            resp.setHeaders(std::move(headers));
        });

    // 2. OPTIONS preflight routes (by path segment depth, 1-5 segments)
    using Method = QHttpServerRequest::Method;

    server.route("/api/<arg>", Method::Options, [](const QString&) {
        return QHttpServerResponse(QHttpServerResponse::StatusCode::NoContent);
    });

    server.route("/api/<arg>/<arg>", Method::Options, [](const QString&, const QString&) {
        return QHttpServerResponse(QHttpServerResponse::StatusCode::NoContent);
    });

    server.route("/api/<arg>/<arg>/<arg>", Method::Options,
                 [](const QString&, const QString&, const QString&) {
                     return QHttpServerResponse(QHttpServerResponse::StatusCode::NoContent);
                 });

    server.route("/api/<arg>/<arg>/<arg>/<arg>", Method::Options,
                 [](const QString&, const QString&, const QString&, const QString&) {
                     return QHttpServerResponse(QHttpServerResponse::StatusCode::NoContent);
                 });

    server.route(
        "/api/<arg>/<arg>/<arg>/<arg>/<arg>", Method::Options,
        [](const QString&, const QString&, const QString&, const QString&, const QString&) {
            return QHttpServerResponse(QHttpServerResponse::StatusCode::NoContent);
        });
}

QHttpHeaders CorsMiddleware::corsHeaders() const {
    QHttpHeaders headers;
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin,
                   m_allowedOrigin.toUtf8());
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowMethods,
                   "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowHeaders,
                   "Content-Type, Accept, Authorization, Origin");
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlMaxAge, "86400");
    return headers;
}

} // namespace stdiolink_server
