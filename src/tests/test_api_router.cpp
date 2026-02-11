#include <gtest/gtest.h>

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHttpServer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
