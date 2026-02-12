#include "cors_middleware.h"

#include <QHttpHeaders>
#include <QHttpServerResponse>

namespace stdiolink_server {

CorsMiddleware::CorsMiddleware(QString allowedOrigin)
    : m_allowedOrigin(std::move(allowedOrigin)) {}

QHttpHeaders CorsMiddleware::buildCorsHeaders(const QString& origin) {
    QHttpHeaders headers;
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin, origin);
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowMethods,
                   "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowHeaders,
                   "Content-Type, Accept, Authorization, Origin");
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlMaxAge, "86400");
    return headers;
}

void CorsMiddleware::install(QHttpServer& server) {
    // 1. after-request handler: inject CORS headers into all responses
    server.addAfterRequestHandler(&server,
        [origin = m_allowedOrigin](const QHttpServerRequest&, QHttpServerResponse& resp) {
            QHttpHeaders headers = resp.headers();
            headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin,
                                    origin);
            headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::AccessControlAllowMethods,
                                    "GET, POST, PUT, PATCH, DELETE, OPTIONS");
            headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::AccessControlAllowHeaders,
                                    "Content-Type, Accept, Authorization, Origin");
            headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::AccessControlMaxAge,
                                    "86400");
            resp.setHeaders(std::move(headers));
        });

    // 2. OPTIONS preflight routes (1-5 path segments under /api/)
    server.route("/api/<arg>", QHttpServerRequest::Method::Options,
                 [](const QString&) {
                     return QHttpServerResponse(
                         QHttpServerResponse::StatusCode::NoContent);
                 });

    server.route("/api/<arg>/<arg>", QHttpServerRequest::Method::Options,
                 [](const QString&, const QString&) {
                     return QHttpServerResponse(
                         QHttpServerResponse::StatusCode::NoContent);
                 });

    server.route("/api/<arg>/<arg>/<arg>", QHttpServerRequest::Method::Options,
                 [](const QString&, const QString&, const QString&) {
                     return QHttpServerResponse(
                         QHttpServerResponse::StatusCode::NoContent);
                 });

    server.route("/api/<arg>/<arg>/<arg>/<arg>",
                 QHttpServerRequest::Method::Options,
                 [](const QString&, const QString&, const QString&,
                    const QString&) {
                     return QHttpServerResponse(
                         QHttpServerResponse::StatusCode::NoContent);
                 });

    server.route("/api/<arg>/<arg>/<arg>/<arg>/<arg>",
                 QHttpServerRequest::Method::Options,
                 [](const QString&, const QString&, const QString&,
                    const QString&, const QString&) {
                     return QHttpServerResponse(
                         QHttpServerResponse::StatusCode::NoContent);
                 });
}

} // namespace stdiolink_server
