#include <gtest/gtest.h>

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHttpServer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include "stdiolink_server/http/api_router.h"
#include "stdiolink_server/http/event_stream_handler.h"
#include "stdiolink_server/manager/process_monitor.h"
#include "stdiolink_server/server_manager.h"

using namespace stdiolink_server;

namespace {

bool writeText(const QString& path, const QString& content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QTextStream out(&file);
    out << content;
    return file.error() == QFile::NoError;
}

void writeService(const QString& root, const QString& id) {
    const QString serviceDir = root + "/services/" + id;
    ASSERT_TRUE(QDir().mkpath(serviceDir));
    ASSERT_TRUE(writeText(serviceDir + "/manifest.json",
                          QString("{\"manifestVersion\":\"1\",\"id\":\"%1\",\"name\":\"Demo\",\"version\":\"1.0.0\"}")
                              .arg(id)));
    ASSERT_TRUE(writeText(serviceDir + "/index.js", "console.log('ok');\n"));
    ASSERT_TRUE(writeText(serviceDir + "/config.schema.json",
                          "{\"device\":{\"type\":\"object\",\"fields\":{\"host\":{\"type\":\"string\",\"required\":true}}}}"));
}

void writeProject(const QString& root,
                  const QString& id,
                  const QString& serviceId) {
    const QString projectPath = root + "/projects/" + id + ".json";
    QJsonObject obj{
        {"name", id},
        {"serviceId", serviceId},
        {"enabled", true},
        {"schedule", QJsonObject{{"type", "manual"}}},
        {"config", QJsonObject{{"device", QJsonObject{{"host", "127.0.0.1"}}}}}
    };

    QFile file(projectPath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_GT(file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact)), 0);
}

bool sendRequest(const QString& method,
                 const QUrl& url,
                 const QByteArray& body,
                 int& statusCode,
                 QByteArray& responseBody,
                 QString& error) {
    QNetworkAccessManager manager;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = nullptr;
    if (method == "GET") {
        reply = manager.get(req);
    } else if (method == "POST") {
        reply = manager.post(req, body);
    } else if (method == "PUT") {
        reply = manager.put(req, body);
    } else if (method == "DELETE") {
        reply = manager.sendCustomRequest(req, "DELETE", body);
    } else if (method == "PATCH") {
        reply = manager.sendCustomRequest(req, "PATCH", body);
    } else {
        error = "unsupported method";
        return false;
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
        error = "request timeout";
        reply->deleteLater();
        return false;
    }
    timeout.stop();

    statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    responseBody = reply->readAll();
    if (reply->error() != QNetworkReply::NoError && statusCode == 0) {
        error = reply->errorString();
        reply->deleteLater();
        return false;
    }

    reply->deleteLater();
    error.clear();
    return true;
}

bool openStreamAndReadHeaders(const QUrl& url,
                              int& statusCode,
                              QMap<QByteArray, QByteArray>& headers,
                              QString& error) {
    QNetworkAccessManager manager;
    QNetworkRequest req(url);

    QNetworkReply* reply = manager.get(req);
    if (!reply) {
        error = "failed to create request";
        return false;
    }

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::metaDataChanged, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    timeout.start(3000);
    loop.exec();
    if (!timeout.isActive()) {
        reply->abort();
        error = "request timeout";
        reply->deleteLater();
        return false;
    }
    timeout.stop();

    statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    for (const auto& pair : reply->rawHeaderPairs()) {
        headers.insert(pair.first.toLower(), pair.second);
    }

    reply->abort();
    reply->deleteLater();
    error.clear();
    return true;
}

bool parseJsonObject(const QByteArray& body, QJsonObject& obj) {
    if (body.trimmed().isEmpty()) {
        obj = QJsonObject();
        return false;
    }
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    obj = doc.object();
    return true;
}

} // namespace

TEST(ApiRouterTest, RegisterRoutesSmoke) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    SUCCEED();
}

TEST(ApiRouterTest, GetServicesAndProjectsViaHttp) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen in current environment";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind QHttpServer in current environment";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/services"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    QJsonObject obj;
    ASSERT_TRUE(parseJsonObject(body, obj));
    const QJsonArray services = obj.value("services").toArray();
    ASSERT_EQ(services.size(), 1);
    EXPECT_EQ(services[0].toObject().value("id").toString(), "demo");

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    const QJsonArray projects = obj.value("projects").toArray();
    ASSERT_EQ(projects.size(), 1);
    EXPECT_EQ(projects[0].toObject().value("id").toString(), "p1");
}

TEST(ApiRouterTest, CreateAndDeleteProjectViaHttp) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen in current environment";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind QHttpServer in current environment";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    const QJsonObject createReq{
        {"id", "p2"},
        {"name", "Project2"},
        {"serviceId", "demo"},
        {"enabled", true},
        {"schedule", QJsonObject{{"type", "manual"}}},
        {"config", QJsonObject{{"device", QJsonObject{{"host", "127.0.0.1"}}}}}
    };

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/projects"),
                            QJsonDocument(createReq).toJson(QJsonDocument::Compact),
                            status,
                            body,
                            error))
        << qPrintable(error);
    EXPECT_EQ(status, 201);
    EXPECT_TRUE(manager.projects().contains("p2"));
    EXPECT_TRUE(QFile::exists(root + "/projects/p2.json"));

    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(base + "/api/projects/p2"),
                            QByteArray(),
                            status,
                            body,
                            error))
        << qPrintable(error);
    EXPECT_EQ(status, 204);
    EXPECT_FALSE(manager.projects().contains("p2"));
    EXPECT_FALSE(QFile::exists(root + "/projects/p2.json"));
}

TEST(ApiRouterTest, ServiceScanRefreshesServiceCatalog) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));
    ASSERT_EQ(manager.services().size(), 1);

    // Add a new service after startup; it should be discovered by /api/services/scan.
    writeService(root, "extra");

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen in current environment";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind QHttpServer in current environment";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services/scan"),
                            QJsonDocument(QJsonObject{}).toJson(QJsonDocument::Compact),
                            status,
                            body,
                            error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);

    QJsonObject scanObj;
    ASSERT_TRUE(parseJsonObject(body, scanObj));
    EXPECT_EQ(scanObj.value("added").toInt(), 1);
    EXPECT_EQ(scanObj.value("loadedServices").toInt(), 2);
    EXPECT_EQ(scanObj.value("revalidatedProjects").toInt(), 1);
    EXPECT_TRUE(scanObj.value("invalidProjects").toArray().isEmpty());

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/services"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);

    QJsonObject listObj;
    ASSERT_TRUE(parseJsonObject(body, listObj));
    const QJsonArray services = listObj.value("services").toArray();
    EXPECT_EQ(services.size(), 2);
    EXPECT_TRUE(manager.services().contains("extra"));
}

TEST(ApiRouterTest, ProjectRuntimeShowsScheduleAndInstanceState) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen in current environment";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind QHttpServer in current environment";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects/p1/runtime"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);

    QJsonObject runtime;
    ASSERT_TRUE(parseJsonObject(body, runtime));
    EXPECT_EQ(runtime.value("id").toString(), "p1");
    EXPECT_EQ(runtime.value("runningInstances").toInt(), 0);
    EXPECT_EQ(runtime.value("status").toString(), "stopped");

    const QJsonObject schedule = runtime.value("schedule").toObject();
    EXPECT_EQ(schedule.value("type").toString(), "manual");
    EXPECT_FALSE(schedule.value("timerActive").toBool());
    EXPECT_FALSE(schedule.value("restartSuppressed").toBool());
    EXPECT_FALSE(schedule.value("autoRestarting").toBool());

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects/missing/runtime"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ServerStatusReturnsCorrectFields) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");

    ServerConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 9999;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/server/status"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);

    QJsonObject obj;
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("status").toString(), "ok");
    EXPECT_EQ(obj.value("version").toString(), "0.1.0");
    EXPECT_GE(obj.value("uptimeMs").toInteger(), 0);
    EXPECT_FALSE(obj.value("startedAt").toString().isEmpty());
    EXPECT_EQ(obj.value("host").toString(), "127.0.0.1");
    EXPECT_EQ(obj.value("port").toInt(), 9999);
    EXPECT_FALSE(obj.value("dataRoot").toString().isEmpty());

    const QJsonObject counts = obj.value("counts").toObject();
    EXPECT_EQ(counts.value("services").toInt(), 1);
    EXPECT_EQ(counts.value("drivers").toInt(), 0);

    const QJsonObject projects = counts.value("projects").toObject();
    EXPECT_EQ(projects.value("total").toInt(), 1);
    EXPECT_EQ(projects.value("valid").toInt(), 1);
    EXPECT_EQ(projects.value("enabled").toInt(), 1);

    const QJsonObject instances = counts.value("instances").toObject();
    EXPECT_EQ(instances.value("total").toInt(), 0);

    const QJsonObject system = obj.value("system").toObject();
    EXPECT_FALSE(system.value("platform").toString().isEmpty());
    EXPECT_GT(system.value("cpuCores").toInt(), 0);
}

TEST(ApiRouterTest, EventStreamContainsCorsHeaders) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    ServerConfig cfg;
    cfg.corsOrigin = "http://localhost:3000";
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QMap<QByteArray, QByteArray> headers;
    QString error;
    ASSERT_TRUE(openStreamAndReadHeaders(QUrl(base + "/api/events/stream"),
                                         status,
                                         headers,
                                         error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    EXPECT_EQ(headers.value("access-control-allow-origin"), "http://localhost:3000");
    EXPECT_EQ(headers.value("content-type"), "text/event-stream");
}

TEST(ApiRouterTest, InstanceDetailReturns404ForMissing) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/instances/nonexistent"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, DriverDetailReturns404ForMissing) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/drivers/nonexistent"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ProjectListPaginationAndFiltering) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeService(root, "other");
    writeProject(root, "p1", "demo");
    writeProject(root, "p2", "demo");
    writeProject(root, "p3", "other");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // No filters — returns all with pagination metadata
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 3);
    EXPECT_EQ(obj.value("page").toInt(), 1);
    EXPECT_EQ(obj.value("pageSize").toInt(), 20);
    EXPECT_EQ(obj.value("projects").toArray().size(), 3);

    // Filter by serviceId
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects?serviceId=demo"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 2);

    // Filter by serviceId with no match
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects?serviceId=nonexistent"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 0);

    // Pagination: page=1, pageSize=2
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects?page=1&pageSize=2"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 3);
    EXPECT_EQ(obj.value("projects").toArray().size(), 2);
    EXPECT_EQ(obj.value("page").toInt(), 1);
    EXPECT_EQ(obj.value("pageSize").toInt(), 2);

    // Pagination: page=2, pageSize=2
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects?page=2&pageSize=2"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 3);
    EXPECT_EQ(obj.value("projects").toArray().size(), 1);

    // Pagination: page out of range
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects?page=999&pageSize=2"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 3);
    EXPECT_EQ(obj.value("projects").toArray().size(), 0);

    // pageSize clamped to 100
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects?pageSize=200"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("pageSize").toInt(), 100);

    // pageSize=0 clamped to 1
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects?pageSize=0"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("pageSize").toInt(), 1);
}

TEST(ApiRouterTest, ProjectEnabledToggle) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));
    ASSERT_TRUE(manager.projects().value("p1").enabled);
    manager.projects()["p1"].schedule.type = ScheduleType::FixedRate;
    manager.projects()["p1"].schedule.intervalMs = 1000;
    manager.projects()["p1"].schedule.maxConcurrent = 1;
    manager.startScheduling();

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    ASSERT_TRUE(sendRequest("GET",
                            QUrl(base + "/api/projects/p1/runtime"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    ASSERT_TRUE(obj.value("schedule").toObject().value("timerActive").toBool());

    // Disable project
    ASSERT_TRUE(sendRequest("PATCH",
                            QUrl(base + "/api/projects/p1/enabled"),
                            QJsonDocument(QJsonObject{{"enabled", false}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_FALSE(obj.value("enabled").toBool());
    EXPECT_EQ(obj.value("status").toString(), "disabled");
    EXPECT_FALSE(manager.projects().value("p1").enabled);

    ASSERT_TRUE(sendRequest("GET",
                            QUrl(base + "/api/projects/p1/runtime"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    ASSERT_FALSE(obj.value("schedule").toObject().value("timerActive").toBool());

    // Re-enable project
    ASSERT_TRUE(sendRequest("PATCH",
                            QUrl(base + "/api/projects/p1/enabled"),
                            QJsonDocument(QJsonObject{{"enabled", true}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_TRUE(obj.value("enabled").toBool());
    EXPECT_TRUE(manager.projects().value("p1").enabled);

    ASSERT_TRUE(sendRequest("GET",
                            QUrl(base + "/api/projects/p1/runtime"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    ASSERT_TRUE(obj.value("schedule").toObject().value("timerActive").toBool());

    // Missing enabled field
    ASSERT_TRUE(sendRequest("PATCH",
                            QUrl(base + "/api/projects/p1/enabled"),
                            QJsonDocument(QJsonObject{}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // Non-bool enabled field
    ASSERT_TRUE(sendRequest("PATCH",
                            QUrl(base + "/api/projects/p1/enabled"),
                            QJsonDocument(QJsonObject{{"enabled", "yes"}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // Project not found
    ASSERT_TRUE(sendRequest("PATCH",
                            QUrl(base + "/api/projects/nonexistent/enabled"),
                            QJsonDocument(QJsonObject{{"enabled", true}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ProjectLogsEndpoint) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");

    // Write a fake log file
    {
        QFile logFile(root + "/logs/p1.log");
        ASSERT_TRUE(logFile.open(QIODevice::WriteOnly));
        for (int i = 1; i <= 10; ++i) {
            logFile.write(QString("line %1\n").arg(i).toUtf8());
        }
    }

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // Read logs with default lines
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects/p1/logs"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("projectId").toString(), "p1");
    EXPECT_FALSE(obj.value("logPath").toString().isEmpty());
    EXPECT_GE(obj.value("lines").toArray().size(), 1);

    // Read logs with lines=3
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects/p1/logs?lines=3"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_LE(obj.value("lines").toArray().size(), 3);

    // Log file doesn't exist — returns empty array, not 404
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects/p1/logs"), QByteArray(), status, body, error))
        << qPrintable(error);
    // p1 has a log file, so let's test with a project that has no log
    writeProject(root, "p-nolog", "demo");
    // Reload projects
    manager.projects().insert("p-nolog", manager.projects().value("p1"));
    manager.projects()["p-nolog"].id = "p-nolog";

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects/p-nolog/logs"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("lines").toArray().size(), 0);

    // Project not found
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects/nonexistent/logs"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ProjectRuntimeBatch) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");
    writeProject(root, "p2", "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // All runtimes
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects/runtime"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    const QJsonArray runtimes = obj.value("runtimes").toArray();
    EXPECT_EQ(runtimes.size(), 2);

    // Check runtime structure
    const QJsonObject rt = runtimes[0].toObject();
    EXPECT_FALSE(rt.value("id").toString().isEmpty());
    EXPECT_TRUE(rt.contains("enabled"));
    EXPECT_TRUE(rt.contains("valid"));
    EXPECT_TRUE(rt.contains("status"));
    EXPECT_TRUE(rt.contains("runningInstances"));
    EXPECT_TRUE(rt.contains("schedule"));

    // Filter by ids
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects/runtime?ids=p1"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("runtimes").toArray().size(), 1);
    EXPECT_EQ(obj.value("runtimes").toArray()[0].toObject().value("id").toString(), "p1");

    // Filter with nonexistent id — skipped
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects/runtime?ids=p1,nonexistent"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("runtimes").toArray().size(), 1);
}

TEST(ApiRouterTest, ProjectListFilterByEnabled) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");
    writeProject(root, "p2", "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    // Disable p2
    manager.projects()["p2"].enabled = false;

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // Filter enabled=true
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects?enabled=true"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 1);

    // Filter enabled=false
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects?enabled=false"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 1);

    // Filter status=disabled
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects?status=disabled"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 1);
    EXPECT_EQ(obj.value("projects").toArray()[0].toObject().value("id").toString(), "p2");
}

TEST(ApiRouterTest, ServiceCreateAndListViaHttp) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // Create with empty template (default)
    const QJsonObject createReq{
        {"id", "test-svc"},
        {"name", "Test Service"},
        {"version", "1.0.0"},
        {"description", "A test"},
        {"author", "tester"}
    };

    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services"),
                            QJsonDocument(createReq).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 201);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("id").toString(), "test-svc");
    EXPECT_EQ(obj.value("name").toString(), "Test Service");
    EXPECT_TRUE(obj.value("created").toBool());

    // Verify via GET /api/services
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/services"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("services").toArray().size(), 1);

    // Verify files exist
    EXPECT_TRUE(QFile::exists(root + "/services/test-svc/manifest.json"));
    EXPECT_TRUE(QFile::exists(root + "/services/test-svc/index.js"));
    EXPECT_TRUE(QFile::exists(root + "/services/test-svc/config.schema.json"));
}

TEST(ApiRouterTest, ServiceCreateTemplates) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;

    // Create with basic template
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services"),
                            QJsonDocument(QJsonObject{
                                {"id", "basic-svc"},
                                {"name", "Basic"},
                                {"version", "1.0.0"},
                                {"template", "basic"}
                            }).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 201);

    // Verify basic template index.js contains log.info
    QFile basicIndex(root + "/services/basic-svc/index.js");
    ASSERT_TRUE(basicIndex.open(QIODevice::ReadOnly));
    const QString basicContent = basicIndex.readAll();
    EXPECT_TRUE(basicContent.contains("log.info"));

    // Create with driver_demo template
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services"),
                            QJsonDocument(QJsonObject{
                                {"id", "driver-svc"},
                                {"name", "Driver"},
                                {"version", "1.0.0"},
                                {"template", "driver_demo"}
                            }).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 201);

    // Verify driver_demo template index.js contains openDriver
    QFile driverIndex(root + "/services/driver-svc/index.js");
    ASSERT_TRUE(driverIndex.open(QIODevice::ReadOnly));
    const QString driverContent = driverIndex.readAll();
    EXPECT_TRUE(driverContent.contains("openDriver"));
}

TEST(ApiRouterTest, ServiceCreateWithCustomContent) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;

    // Create with custom indexJs and configSchema overriding template
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services"),
                            QJsonDocument(QJsonObject{
                                {"id", "custom-svc"},
                                {"name", "Custom"},
                                {"version", "2.0.0"},
                                {"template", "basic"},
                                {"indexJs", "// custom code\n"},
                                {"configSchema", QJsonObject{{"port", QJsonObject{{"type", "int"}}}}}
                            }).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 201);

    // Verify custom index.js
    QFile indexFile(root + "/services/custom-svc/index.js");
    ASSERT_TRUE(indexFile.open(QIODevice::ReadOnly));
    EXPECT_EQ(QString(indexFile.readAll()), "// custom code\n");

    // Verify custom schema
    QFile schemaFile(root + "/services/custom-svc/config.schema.json");
    ASSERT_TRUE(schemaFile.open(QIODevice::ReadOnly));
    const QJsonDocument schemaDoc = QJsonDocument::fromJson(schemaFile.readAll());
    EXPECT_TRUE(schemaDoc.object().contains("port"));
}

TEST(ApiRouterTest, ServiceCreateValidationErrors) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "existing");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;

    // Missing id
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services"),
                            QJsonDocument(QJsonObject{
                                {"name", "X"}, {"version", "1.0.0"}
                            }).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // Missing name
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services"),
                            QJsonDocument(QJsonObject{
                                {"id", "x"}, {"version", "1.0.0"}
                            }).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // Missing version
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services"),
                            QJsonDocument(QJsonObject{
                                {"id", "x"}, {"name", "X"}
                            }).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // Invalid id (contains space)
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services"),
                            QJsonDocument(QJsonObject{
                                {"id", "bad id"}, {"name", "X"}, {"version", "1.0.0"}
                            }).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // Duplicate id
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services"),
                            QJsonDocument(QJsonObject{
                                {"id", "existing"}, {"name", "X"}, {"version", "1.0.0"}
                            }).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 409);

    // Invalid template
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services"),
                            QJsonDocument(QJsonObject{
                                {"id", "x"}, {"name", "X"}, {"version", "1.0.0"},
                                {"template", "unknown"}
                            }).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // indexJs must be string
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services"),
                            QJsonDocument(QJsonObject{
                                {"id", "x2"}, {"name", "X"}, {"version", "1.0.0"},
                                {"indexJs", QJsonObject{{"bad", true}}}
                            }).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // configSchema must be object
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services"),
                            QJsonDocument(QJsonObject{
                                {"id", "x3"}, {"name", "X"}, {"version", "1.0.0"},
                                {"configSchema", "bad"}
                            }).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // invalid configSchema should be treated as client error instead of 500
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services"),
                            QJsonDocument(QJsonObject{
                                {"id", "x4"}, {"name", "X"}, {"version", "1.0.0"},
                                {"configSchema", QJsonObject{{"port", QJsonObject{{"type", "invalid"}}}}}
                            }).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);
}

TEST(ApiRouterTest, ServiceDeleteViaHttp) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;

    // Delete existing service with no projects
    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(base + "/api/services/demo"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 204);
    EXPECT_FALSE(manager.services().contains("demo"));

    // Delete nonexistent
    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(base + "/api/services/nonexistent"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ServiceFileListViaHttp) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    // Add a subdirectory file
    ASSERT_TRUE(QDir().mkpath(root + "/services/demo/lib"));
    ASSERT_TRUE(writeText(root + "/services/demo/lib/utils.js", "// utils\n"));

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // List files
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/services/demo/files"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("serviceId").toString(), "demo");

    const QJsonArray files = obj.value("files").toArray();
    EXPECT_GE(files.size(), 4); // manifest.json, index.js, config.schema.json, lib/utils.js

    bool foundManifest = false, foundIndex = false, foundSchema = false, foundUtils = false;
    for (const auto& v : files) {
        const QJsonObject f = v.toObject();
        if (f.value("path").toString() == "manifest.json") foundManifest = true;
        if (f.value("path").toString() == "index.js") foundIndex = true;
        if (f.value("path").toString() == "config.schema.json") foundSchema = true;
        if (f.value("path").toString() == "lib/utils.js") {
            foundUtils = true;
            EXPECT_EQ(f.value("type").toString(), "javascript");
        }
    }
    EXPECT_TRUE(foundManifest);
    EXPECT_TRUE(foundIndex);
    EXPECT_TRUE(foundSchema);
    EXPECT_TRUE(foundUtils);

    // Service not found
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/services/nonexistent/files"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ServiceFileReadViaHttp) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // Read existing file
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/services/demo/files/content?path=index.js"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("path").toString(), "index.js");
    EXPECT_FALSE(obj.value("content").toString().isEmpty());
    EXPECT_GT(obj.value("size").toInteger(), 0);

    // Path traversal → 400
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/services/demo/files/content?path=../etc/passwd"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // Nonexistent file → 404
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/services/demo/files/content?path=nonexist.js"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);

    // Missing path parameter → 400
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/services/demo/files/content"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // Service not found
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/services/nonexistent/files/content?path=index.js"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ServiceFileWriteViaHttp) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // Update index.js
    const QString newContent = "// updated code\nconsole.log('hello');\n";
    ASSERT_TRUE(sendRequest("PUT",
                            QUrl(base + "/api/services/demo/files/content?path=index.js"),
                            QJsonDocument(QJsonObject{{"content", newContent}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_TRUE(obj.value("updated").toBool());

    // Verify content was written
    QFile indexFile(root + "/services/demo/index.js");
    ASSERT_TRUE(indexFile.open(QIODevice::ReadOnly));
    EXPECT_EQ(QString(indexFile.readAll()), newContent);

    // Update manifest.json with valid content → 200 + memory updated
    const QString validManifest = QJsonDocument(QJsonObject{
        {"manifestVersion", "1"},
        {"id", "demo"},
        {"name", "Updated Demo"},
        {"version", "2.0.0"}
    }).toJson(QJsonDocument::Compact);
    ASSERT_TRUE(sendRequest("PUT",
                            QUrl(base + "/api/services/demo/files/content?path=manifest.json"),
                            QJsonDocument(QJsonObject{{"content", validManifest}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    // Verify memory was updated
    EXPECT_EQ(manager.services().value("demo").name, "Updated Demo");

    // Update manifest.json with invalid JSON → 400
    ASSERT_TRUE(sendRequest("PUT",
                            QUrl(base + "/api/services/demo/files/content?path=manifest.json"),
                            QJsonDocument(QJsonObject{{"content", "not json"}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // Update config.schema.json with invalid JSON → 400
    ASSERT_TRUE(sendRequest("PUT",
                            QUrl(base + "/api/services/demo/files/content?path=config.schema.json"),
                            QJsonDocument(QJsonObject{{"content", "{broken"}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // Missing content field → 400
    ASSERT_TRUE(sendRequest("PUT",
                            QUrl(base + "/api/services/demo/files/content?path=index.js"),
                            QJsonDocument(QJsonObject{}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);
}

TEST(ApiRouterTest, ServiceFileCreateAndDeleteViaHttp) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // Create new file in subdirectory (auto-creates dir)
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services/demo/files/content?path=lib/helper.js"),
                            QJsonDocument(QJsonObject{{"content", "// helper\n"}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 201);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_TRUE(obj.value("created").toBool());
    EXPECT_TRUE(QFile::exists(root + "/services/demo/lib/helper.js"));

    // Create file that already exists → 409
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services/demo/files/content?path=index.js"),
                            QJsonDocument(QJsonObject{{"content", "// dup"}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 409);

    // Delete the created file
    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(base + "/api/services/demo/files/content?path=lib/helper.js"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 204);
    EXPECT_FALSE(QFile::exists(root + "/services/demo/lib/helper.js"));

    // Delete core file manifest.json → 400
    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(base + "/api/services/demo/files/content?path=manifest.json"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // Delete core file index.js → 400
    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(base + "/api/services/demo/files/content?path=index.js"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // Delete nonexistent file → 404
    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(base + "/api/services/demo/files/content?path=nonexist.js"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ServiceDeleteWithProjectsForceViaHttp) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // Non-force delete with associated projects → 409
    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(base + "/api/services/demo"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 409);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_TRUE(obj.contains("associatedProjects"));
    EXPECT_TRUE(manager.services().contains("demo"));

    // Force delete
    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(base + "/api/services/demo?force=true"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 204);
    EXPECT_FALSE(manager.services().contains("demo"));
    EXPECT_FALSE(manager.projects().value("p1").valid);
    EXPECT_TRUE(manager.projects().value("p1").error.contains("deleted"));
}

// --- M54: Schema Validation & Config Utils API Tests ---

TEST(ApiRouterTest, ValidateSchemaViaHttp) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // Valid schema
    QJsonObject validSchema{
        {"port", QJsonObject{{"type", "int"}, {"required", true}, {"default", 8080}}},
        {"name", QJsonObject{{"type", "string"}}}
    };
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services/demo/validate-schema"),
                            QJsonDocument(QJsonObject{{"schema", validSchema}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_TRUE(obj.value("valid").toBool());
    EXPECT_TRUE(obj.contains("fields"));
    EXPECT_EQ(obj.value("fields").toArray().size(), 2);

    // Invalid schema (unknown type)
    QJsonObject invalidSchema{
        {"ts", QJsonObject{{"type", "datetime"}}}
    };
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services/demo/validate-schema"),
                            QJsonDocument(QJsonObject{{"schema", invalidSchema}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_FALSE(obj.value("valid").toBool());
    EXPECT_FALSE(obj.value("error").toString().isEmpty());

    // Missing schema field → 400
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services/demo/validate-schema"),
                            QJsonDocument(QJsonObject{}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // Service not found → 404
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services/nonexistent/validate-schema"),
                            QJsonDocument(QJsonObject{{"schema", validSchema}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, GenerateDefaultsViaHttp) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services/demo/generate-defaults"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("serviceId").toString(), "demo");
    EXPECT_TRUE(obj.contains("config"));
    EXPECT_TRUE(obj.contains("requiredFields"));
    EXPECT_TRUE(obj.contains("optionalFields"));

    // Service not found → 404
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services/nonexistent/generate-defaults"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ValidateConfigViaHttp) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // Valid config (empty config against demo schema which has no required fields)
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services/demo/validate-config"),
                            QJsonDocument(QJsonObject{{"config", QJsonObject{}}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_TRUE(obj.value("valid").toBool());

    // Missing config field → 400
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services/demo/validate-config"),
                            QJsonDocument(QJsonObject{}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 400);

    // Service not found → 404
    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services/nonexistent/validate-config"),
                            QJsonDocument(QJsonObject{{"config", QJsonObject{}}}).toJson(QJsonDocument::Compact),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ServiceDetailIncludesConfigSchemaFieldsViaHttp) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    ASSERT_TRUE(sendRequest("GET",
                            QUrl(base + "/api/services/demo"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_TRUE(obj.contains("configSchemaFields"));
    EXPECT_TRUE(obj.value("configSchemaFields").isArray());
}

// M72_R07 — Request body exceeding 1MB limit returns 413
TEST(ApiRouterTest, M72_R07_RequestBodyTooLargeReturns413) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    // Build a JSON body > 1MB
    const QByteArray filler(1024 * 1024 + 100, 'A');
    const QByteArray largeBody = "{\"id\":\"x\",\"name\":\"" + filler + "\",\"version\":\"1.0.0\"}";

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/services"),
                            largeBody,
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 413);

    QJsonObject obj;
    if (parseJsonObject(body, obj)) {
        EXPECT_TRUE(obj.value("error").toString().contains("too large"));
    }
}

// M72_R06 — Bounded tail read: large log file returns only tail lines
TEST(ApiRouterTest, M72_R06_BoundedTailReadLargeLogFile) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");

    // Write a log file with many lines
    {
        QFile logFile(root + "/logs/p1.log");
        ASSERT_TRUE(logFile.open(QIODevice::WriteOnly));
        for (int i = 1; i <= 200; ++i) {
            logFile.write(QString("log line %1\n").arg(i).toUtf8());
        }
    }

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // Request only 5 lines — should return exactly 5
    ASSERT_TRUE(sendRequest("GET",
                            QUrl(base + "/api/projects/p1/logs?lines=5"),
                            QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("lines").toArray().size(), 5);

    // The last line should be "log line 200"
    const QJsonArray lines = obj.value("lines").toArray();
    EXPECT_TRUE(lines.last().toString().contains("200"));
}

// M72_R11 — ProcessMonitor::isSupported returns consistent value
TEST(ApiRouterTest, M72_R11_ProcessMonitorIsSupportedConsistent) {
    // On macOS/Linux isSupported() should return true
    // On other platforms it returns false
    // Just verify it doesn't crash and returns a bool
    const bool supported = ProcessMonitor::isSupported();
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    EXPECT_TRUE(supported);
#else
    Q_UNUSED(supported);
#endif
}

// M72_R12 — ProcessMonitor endpoint returns correct response on all platforms
TEST(ApiRouterTest, M72_R12_ProcessMonitorEndpointResponse) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // Request process-tree for a nonexistent instance
    ASSERT_TRUE(sendRequest("GET",
                            QUrl(base + "/api/instances/fake-id/process-tree"),
                            QByteArray(), status, body, error))
        << qPrintable(error);

    if (ProcessMonitor::isSupported()) {
        // Supported platform: should get 404 (instance not found), not 501
        EXPECT_EQ(status, 404);
    } else {
        // Unsupported platform: should get 501 with structured body
        EXPECT_EQ(status, 501);
        ASSERT_TRUE(parseJsonObject(body, obj));
        EXPECT_FALSE(obj.value("error").toString().isEmpty());
        EXPECT_EQ(obj.value("code").toString(), "PROCESS_MONITOR_UNSUPPORTED");
        EXPECT_FALSE(obj.value("supported").toBool());
        EXPECT_FALSE(obj.value("platform").toString().isEmpty());
    }

    // Also test the resources endpoint
    ASSERT_TRUE(sendRequest("GET",
                            QUrl(base + "/api/instances/fake-id/resources"),
                            QByteArray(), status, body, error))
        << qPrintable(error);

    if (ProcessMonitor::isSupported()) {
        EXPECT_EQ(status, 404);
    } else {
        EXPECT_EQ(status, 501);
        ASSERT_TRUE(parseJsonObject(body, obj));
        EXPECT_FALSE(obj.value("error").toString().isEmpty());
        EXPECT_EQ(obj.value("code").toString(), "PROCESS_MONITOR_UNSUPPORTED");
        EXPECT_FALSE(obj.value("supported").toBool());
        EXPECT_FALSE(obj.value("platform").toString().isEmpty());
    }
}

// M72_R15 — Request body too large on project create returns 413
TEST(ApiRouterTest, M72_R15_ProjectCreateBodyTooLargeReturns413) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    // Build a JSON body > 1MB for project create
    const QByteArray filler(1024 * 1024 + 100, 'X');
    const QByteArray largeBody = "{\"id\":\"p-big\",\"name\":\"" + filler + "\"}";

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("POST",
                            QUrl(base + "/api/projects"),
                            largeBody,
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 413);

    QJsonObject obj;
    if (parseJsonObject(body, obj)) {
        EXPECT_TRUE(obj.value("error").toString().contains("too large"));
    }
}

// M72_R14 — SSE connection lifecycle: activeConnectionCount tracks open connections
TEST(ApiRouterTest, M72_R14_SseConnectionLifecycle) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));

    auto* handler = manager.eventStreamHandler();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->activeConnectionCount(), 0);

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    // Open an SSE connection
    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl(base + "/api/events/stream"));
    QNetworkReply* reply = nam.get(req);
    ASSERT_NE(reply, nullptr);

    // Wait for headers (connection established)
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::metaDataChanged, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(3000);
    loop.exec();

    if (!timeout.isActive()) {
        reply->abort();
        reply->deleteLater();
        GTEST_SKIP() << "SSE connection timeout";
    }
    timeout.stop();

    // Connection should now be tracked
    EXPECT_EQ(handler->activeConnectionCount(), 1);

    // Close all connections and verify count drops to 0
    handler->closeAllConnections();
    EXPECT_EQ(handler->activeConnectionCount(), 0);

    reply->abort();
    reply->deleteLater();
}

// --- GET /api/events ---

TEST(ApiRouterTest, GetEventsReturnsPublishedEvents) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError)) << qPrintable(initError);

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen in current environment";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind QHttpServer in current environment";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    // Publish events via EventBus
    auto* bus = manager.eventBus();
    ASSERT_NE(bus, nullptr);
    bus->publish("instance.started", QJsonObject{{"instanceId", "i1"}, {"projectId", "p1"}});
    bus->publish("schedule.triggered", QJsonObject{{"projectId", "p1"}});
    bus->publish("instance.finished", QJsonObject{{"instanceId", "i1"}, {"projectId", "p1"}});

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/events"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);

    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    EXPECT_EQ(obj.value("count").toInt(), 3);

    const QJsonArray events = obj.value("events").toArray();
    ASSERT_EQ(events.size(), 3);
    // Newest first
    EXPECT_EQ(events[0].toObject().value("type").toString(), "instance.finished");
    EXPECT_EQ(events[2].toObject().value("type").toString(), "instance.started");
}

TEST(ApiRouterTest, GetEventsFilterByTypePrefix) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError)) << qPrintable(initError);

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen in current environment";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind QHttpServer in current environment";
    }

    const QString base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    auto* bus = manager.eventBus();
    ASSERT_NE(bus, nullptr);
    bus->publish("instance.started", QJsonObject{{"instanceId", "i1"}, {"projectId", "p1"}});
    bus->publish("schedule.triggered", QJsonObject{{"projectId", "p1"}});
    bus->publish("instance.finished", QJsonObject{{"instanceId", "i1"}, {"projectId", "p1"}});

    int status = 0;
    QByteArray body;
    QString error;

    // Filter by type prefix "instance"
    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/events?type=instance"),
                            QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);

    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    EXPECT_EQ(obj.value("count").toInt(), 2);

    const QJsonArray events = obj.value("events").toArray();
    ASSERT_EQ(events.size(), 2);
    for (int i = 0; i < events.size(); ++i) {
        EXPECT_TRUE(events[i].toObject().value("type").toString().startsWith("instance"));
    }
}

TEST(ApiRouterTest, GetEventsLimitParameter) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError)) << qPrintable(initError);

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen in current environment";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind QHttpServer in current environment";
    }

    const QString base =
        QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    auto* bus = manager.eventBus();
    ASSERT_NE(bus, nullptr);
    for (int i = 0; i < 5; ++i) {
        bus->publish("event.x", QJsonObject{{"i", i}});
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/events?limit=2"),
                            QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);

    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    EXPECT_EQ(obj.value("count").toInt(), 2);

    const QJsonArray events = obj.value("events").toArray();
    ASSERT_EQ(events.size(), 2);
    // Newest first: i=4, i=3
    EXPECT_EQ(events[0].toObject().value("data").toObject()
                  .value("i").toInt(), 4);
    EXPECT_EQ(events[1].toObject().value("data").toObject()
                  .value("i").toInt(), 3);
}

TEST(ApiRouterTest, GetEventsFilterByProjectId) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError)) << qPrintable(initError);

    QHttpServer server;
    ApiRouter router(&manager);
    router.registerRoutes(server);

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::AnyIPv4, 0)) {
        GTEST_SKIP() << "Cannot listen in current environment";
    }
    if (!server.bind(&tcpServer)) {
        GTEST_SKIP() << "Cannot bind QHttpServer in current environment";
    }

    const QString base =
        QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());

    auto* bus = manager.eventBus();
    ASSERT_NE(bus, nullptr);
    bus->publish("instance.started",
                 QJsonObject{{"instanceId", "i1"}, {"projectId", "pA"}});
    bus->publish("instance.started",
                 QJsonObject{{"instanceId", "i2"}, {"projectId", "pB"}});
    bus->publish("instance.finished",
                 QJsonObject{{"instanceId", "i1"}, {"projectId", "pA"}});

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("GET",
                            QUrl(base + "/api/events?projectId=pA"),
                            QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);

    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    EXPECT_EQ(obj.value("count").toInt(), 2);

    const QJsonArray events = obj.value("events").toArray();
    ASSERT_EQ(events.size(), 2);
    for (int i = 0; i < events.size(); ++i) {
        EXPECT_EQ(events[i].toObject().value("data").toObject()
                      .value("projectId").toString(), "pA");
    }
}
