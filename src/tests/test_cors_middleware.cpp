#include <gtest/gtest.h>

#include <QEventLoop>
#include <QHttpServer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTimer>

#include "stdiolink_server/http/cors_middleware.h"
#include "stdiolink_server/http/http_helpers.h"

using namespace stdiolink_server;

namespace {

struct HttpResult {
    int statusCode = 0;
    QByteArray body;
    QMap<QByteArray, QByteArray> headers;
    QString error;
};

HttpResult doRequest(const QString& method, const QUrl& url, const QByteArray& body = {}) {
    HttpResult result;
    QNetworkAccessManager manager;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = nullptr;
    if (method == "GET") {
        reply = manager.get(req);
    } else if (method == "POST") {
        reply = manager.post(req, body);
    } else if (method == "OPTIONS") {
        reply = manager.sendCustomRequest(req, "OPTIONS");
    } else {
        result.error = "unsupported method";
        return result;
    }

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(3000);
    loop.exec();

    if (!timeout.isActive()) {
        reply->abort();
        result.error = "request timeout";
        reply->deleteLater();
        return result;
    }
    timeout.stop();

    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();

    const auto rawHeaders = reply->rawHeaderPairs();
    for (const auto& pair : rawHeaders) {
        result.headers.insert(pair.first.toLower(), pair.second);
    }

    if (reply->error() != QNetworkReply::NoError && result.statusCode == 0) {
        result.error = reply->errorString();
    }

    reply->deleteLater();
    return result;
}

} // namespace

TEST(CorsMiddlewareTest, GetResponseContainsCorsHeaders) {
    QHttpServer server;

    CorsMiddleware cors;
    cors.install(server);

    server.route("/api/test", QHttpServerRequest::Method::Get,
                 [](const QHttpServerRequest&) {
                     return jsonResponse(QJsonObject{{"ok", true}});
                 });

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    auto r = doRequest("GET", QUrl(base + "/api/test"));
    ASSERT_TRUE(r.error.isEmpty()) << qPrintable(r.error);
    EXPECT_EQ(r.statusCode, 200);
    EXPECT_EQ(r.headers.value("access-control-allow-origin"), "*");
    EXPECT_EQ(r.headers.value("access-control-allow-methods"),
              "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    EXPECT_EQ(r.headers.value("access-control-allow-headers"),
              "Content-Type, Accept, Authorization, Origin");
    EXPECT_EQ(r.headers.value("access-control-max-age"), "86400");
}

TEST(CorsMiddlewareTest, PostResponseContainsCorsHeaders) {
    QHttpServer server;

    CorsMiddleware cors;
    cors.install(server);

    server.route("/api/action", QHttpServerRequest::Method::Post,
                 [](const QHttpServerRequest&) {
                     return jsonResponse(QJsonObject{{"done", true}});
                 });

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    auto r = doRequest("POST", QUrl(base + "/api/action"),
                       QJsonDocument(QJsonObject{}).toJson(QJsonDocument::Compact));
    ASSERT_TRUE(r.error.isEmpty()) << qPrintable(r.error);
    EXPECT_EQ(r.statusCode, 200);
    EXPECT_EQ(r.headers.value("access-control-allow-origin"), "*");
    EXPECT_EQ(r.headers.value("access-control-allow-methods"),
              "GET, POST, PUT, PATCH, DELETE, OPTIONS");
}

TEST(CorsMiddlewareTest, OptionsPreflightReturns204) {
    QHttpServer server;

    CorsMiddleware cors;
    cors.install(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    auto r = doRequest("OPTIONS", QUrl(base + "/api/services"));
    ASSERT_TRUE(r.error.isEmpty()) << qPrintable(r.error);
    EXPECT_EQ(r.statusCode, 204);
    EXPECT_EQ(r.headers.value("access-control-allow-origin"), "*");
}

TEST(CorsMiddlewareTest, OptionsNestedPath) {
    QHttpServer server;

    CorsMiddleware cors;
    cors.install(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    // 2-segment: /api/services
    auto r = doRequest("OPTIONS", QUrl(base + "/api/services"));
    ASSERT_TRUE(r.error.isEmpty()) << qPrintable(r.error);
    EXPECT_EQ(r.statusCode, 204);

    // 3-segment: /api/services/my-svc
    r = doRequest("OPTIONS", QUrl(base + "/api/services/my-svc"));
    ASSERT_TRUE(r.error.isEmpty()) << qPrintable(r.error);
    EXPECT_EQ(r.statusCode, 204);

    // 4-segment: /api/projects/p1/runtime
    r = doRequest("OPTIONS", QUrl(base + "/api/projects/p1/runtime"));
    ASSERT_TRUE(r.error.isEmpty()) << qPrintable(r.error);
    EXPECT_EQ(r.statusCode, 204);
}

TEST(CorsMiddlewareTest, CustomCorsOrigin) {
    QHttpServer server;

    CorsMiddleware cors("http://localhost:3000");
    cors.install(server);

    server.route("/api/test", QHttpServerRequest::Method::Get,
                 [](const QHttpServerRequest&) {
                     return jsonResponse(QJsonObject{{"ok", true}});
                 });

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    auto r = doRequest("GET", QUrl(base + "/api/test"));
    ASSERT_TRUE(r.error.isEmpty()) << qPrintable(r.error);
    EXPECT_EQ(r.statusCode, 200);
    EXPECT_EQ(r.headers.value("access-control-allow-origin"), "http://localhost:3000");
}

TEST(CorsMiddlewareTest, DefaultCorsOriginIsStar) {
    CorsMiddleware cors;
    EXPECT_EQ(cors.allowedOrigin(), "*");
}

TEST(CorsMiddlewareTest, NotFoundResponseAlsoHasCorsHeaders) {
    QHttpServer server;

    CorsMiddleware cors;
    cors.install(server);

    // Simulate the real app's missing handler which manually adds CORS headers
    // (addAfterRequestHandler does NOT apply to setMissingHandler responses)
    server.setMissingHandler(&server,
        [](const QHttpServerRequest&, QHttpServerResponder& responder) {
            QHttpHeaders headers = CorsMiddleware::buildCorsHeaders("*");
            headers.append(QHttpHeaders::WellKnownHeader::ContentType, "application/json");
            responder.write("{\"error\":\"not found\"}",
                            headers,
                            QHttpServerResponder::StatusCode::NotFound);
        });

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    auto r = doRequest("GET", QUrl(base + "/api/nonexistent/deep/path"));
    ASSERT_TRUE(r.error.isEmpty()) << qPrintable(r.error);
    EXPECT_EQ(r.statusCode, 404);
    EXPECT_EQ(r.headers.value("access-control-allow-origin"), "*");
}
