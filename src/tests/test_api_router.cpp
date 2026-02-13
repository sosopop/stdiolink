#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
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

QString exeSuffix() {
#ifdef Q_OS_WIN
    return ".exe";
#else
    return QString();
#endif
}

QString testBinaryPath(const QString& baseName) {
    return QCoreApplication::applicationDirPath() + "/" + baseName + exeSuffix();
}

bool copyExecutable(const QString& fromPath, const QString& toPath) {
    QFile::remove(toPath);
    if (!QFile::copy(fromPath, toPath)) {
        return false;
    }
    const QFileDevice::Permissions perms = QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                           QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                           QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                           QFileDevice::ExeOther;
    return QFile::setPermissions(toPath, perms);
}

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
    ASSERT_TRUE(writeText(
        serviceDir + "/manifest.json",
        QString("{\"manifestVersion\":\"1\",\"id\":\"%1\",\"name\":\"Demo\",\"version\":\"1.0.0\"}")
            .arg(id)));
    ASSERT_TRUE(writeText(serviceDir + "/index.js", "console.log('ok');\n"));
    ASSERT_TRUE(writeText(serviceDir + "/config.schema.json",
                          "{\"device\":{\"type\":\"object\",\"fields\":{\"host\":{\"type\":"
                          "\"string\",\"required\":true}}}}"));
}

void writeProject(const QString& root, const QString& id, const QString& serviceId) {
    const QString projectPath = root + "/projects/" + id + ".json";
    QJsonObject obj{{"name", id},
                    {"serviceId", serviceId},
                    {"enabled", true},
                    {"schedule", QJsonObject{{"type", "manual"}}},
                    {"config", QJsonObject{{"device", QJsonObject{{"host", "127.0.0.1"}}}}}};

    QFile file(projectPath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_GT(file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact)), 0);
}

bool sendRequest(const QString& method, const QUrl& url, const QByteArray& body, int& statusCode,
                 QByteArray& responseBody, QString& error) {
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
    } else if (method == "PATCH") {
        reply = manager.sendCustomRequest(req, "PATCH", body);
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

int connectSseAndAbort(const QUrl& url, int timeoutMs = 1500) {
    QNetworkAccessManager manager;
    QNetworkRequest req(url);

    QNetworkReply* reply = manager.get(req);
    int statusCode = 0;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::metaDataChanged, &loop, [&]() {
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code > 0) {
            statusCode = code;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        reply->abort();
        loop.quit();
    });

    timeout.start(timeoutMs);
    loop.exec();

    if (statusCode == 0) {
        statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }
    reply->deleteLater();
    return statusCode;
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
        {"config", QJsonObject{{"device", QJsonObject{{"host", "127.0.0.1"}}}}}};

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("POST", QUrl(base + "/api/projects"),
                            QJsonDocument(createReq).toJson(QJsonDocument::Compact), status, body,
                            error))
        << qPrintable(error);
    EXPECT_EQ(status, 201);
    EXPECT_TRUE(manager.projects().contains("p2"));
    EXPECT_TRUE(QFile::exists(root + "/projects/p2.json"));

    ASSERT_TRUE(
        sendRequest("DELETE", QUrl(base + "/api/projects/p2"), QByteArray(), status, body, error))
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

    ASSERT_TRUE(sendRequest("POST", QUrl(base + "/api/services/scan"),
                            QJsonDocument(QJsonObject{}).toJson(QJsonDocument::Compact), status,
                            body, error))
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

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects/p1/runtime"), QByteArray(), status,
                            body, error))
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

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/projects/missing/runtime"), QByteArray(),
                            status, body, error))
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
    cfg.serviceProgram = testBinaryPath("test_service_stub");
    ASSERT_TRUE(QFileInfo::exists(cfg.serviceProgram));

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

    ASSERT_TRUE(
        sendRequest("GET", QUrl(base + "/api/server/status"), QByteArray(), status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);

    QJsonObject obj;
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("status").toString(), "ok");
    EXPECT_FALSE(obj.value("version").toString().isEmpty());
    EXPECT_GE(obj.value("uptimeMs").toInteger(), 0);
    EXPECT_FALSE(obj.value("startedAt").toString().isEmpty());
    EXPECT_FALSE(obj.value("dataRoot").toString().isEmpty());

    const QJsonObject counts = obj.value("counts").toObject();
    EXPECT_EQ(counts.value("services").toInt(), 1);
    EXPECT_EQ(counts.value("drivers").toInt(), 0);

    const QJsonObject projects = counts.value("projects").toObject();
    EXPECT_EQ(projects.value("total").toInt(), 1);
    EXPECT_EQ(projects.value("valid").toInt(), 1);
    EXPECT_EQ(projects.value("invalid").toInt(), 0);
    EXPECT_EQ(projects.value("enabled").toInt(), 1);

    const QJsonObject instances = counts.value("instances").toObject();
    EXPECT_EQ(instances.value("total").toInt(), 0);
    EXPECT_EQ(instances.value("running").toInt(), 0);

    const QJsonObject system = obj.value("system").toObject();
    EXPECT_FALSE(system.value("platform").toString().isEmpty());
    EXPECT_GT(system.value("cpuCores").toInt(), 0);
}

TEST(ApiRouterTest, InstanceDetailReturns404ForMissing) {
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

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/instances/nonexistent"), QByteArray(), status,
                            body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, DriverDetailReturnsMetaForExistingDriver) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));
    ASSERT_TRUE(QDir().mkpath(root + "/drivers/good"));

    const QString metaDriver = testBinaryPath("test_meta_driver");
    ASSERT_TRUE(QFileInfo::exists(metaDriver));
    ASSERT_TRUE(copyExecutable(metaDriver, root + "/drivers/good/driver_under_test" + exeSuffix()));

    writeService(root, "demo");
    writeProject(root, "p1", "demo");

    ServerConfig cfg;
    cfg.serviceProgram = testBinaryPath("test_service_stub");
    ASSERT_TRUE(QFileInfo::exists(cfg.serviceProgram));

    ServerManager manager(root, cfg);
    QString initError;
    ASSERT_TRUE(manager.initialize(initError));
    ASSERT_TRUE(manager.driverCatalog()->hasDriver("test-meta-driver"));

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

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/drivers/test-meta-driver"), QByteArray(),
                            status, body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 200);

    QJsonObject obj;
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("id").toString(), "test-meta-driver");
    EXPECT_FALSE(obj.value("program").toString().isEmpty());
    EXPECT_TRUE(obj.contains("meta"));

    const QJsonObject meta = obj.value("meta").toObject();
    EXPECT_TRUE(meta.contains("info"));
    EXPECT_TRUE(meta.contains("commands"));
}

TEST(ApiRouterTest, DriverDetailReturns404ForMissing) {
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

    ASSERT_TRUE(sendRequest("GET", QUrl(base + "/api/drivers/nonexistent"), QByteArray(), status,
                            body, error))
        << qPrintable(error);
    EXPECT_EQ(status, 404);
}

// --- M51 Tests ---

namespace {

void writeProjectEx(const QString& root, const QString& id, const QString& serviceId,
                    bool enabled) {
    const QString projectPath = root + "/projects/" + id + ".json";
    QJsonObject obj{{"name", id},
                    {"serviceId", serviceId},
                    {"enabled", enabled},
                    {"schedule", QJsonObject{{"type", "manual"}}},
                    {"config", QJsonObject{{"device", QJsonObject{{"host", "127.0.0.1"}}}}}};
    QFile file(projectPath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_GT(file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact)), 0);
}

struct M51Fixture {
    QTemporaryDir tmp;
    QString root;
    ServerConfig cfg;
    std::unique_ptr<ServerManager> manager;
    std::unique_ptr<QHttpServer> server;
    std::unique_ptr<ApiRouter> router;
    QTcpServer tcpServer;
    QString base;

    bool setup(int serviceCount, const std::function<void(const QString&)>& setupFn) {
        if (!tmp.isValid())
            return false;
        root = tmp.path();
        QDir().mkpath(root + "/services");
        QDir().mkpath(root + "/projects");
        QDir().mkpath(root + "/workspaces");
        QDir().mkpath(root + "/logs");
        setupFn(root);
        manager = std::make_unique<ServerManager>(root, cfg);
        QString initError;
        if (!manager->initialize(initError))
            return false;
        server = std::make_unique<QHttpServer>();
        router = std::make_unique<ApiRouter>(manager.get());
        router->registerRoutes(*server);
        if (!tcpServer.listen(QHostAddress::AnyIPv4, 0))
            return false;
        if (!server->bind(&tcpServer))
            return false;
        base = QString("http://127.0.0.1:%1").arg(tcpServer.serverPort());
        return true;
    }
};

} // namespace

TEST(ApiRouterTest, ProjectListPaginationAndTotal) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "demo");
        writeProjectEx(root, "p1", "demo", true);
        writeProjectEx(root, "p2", "demo", true);
        writeProjectEx(root, "p3", "demo", true);
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // Default pagination
    ASSERT_TRUE(
        sendRequest("GET", QUrl(f.base + "/api/projects"), QByteArray(), status, body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 3);
    EXPECT_EQ(obj.value("page").toInt(), 1);
    EXPECT_EQ(obj.value("pageSize").toInt(), 20);
    EXPECT_EQ(obj.value("projects").toArray().size(), 3);

    // page=1&pageSize=2
    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects?page=1&pageSize=2"), QByteArray(),
                            status, body, error));
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 3);
    EXPECT_EQ(obj.value("page").toInt(), 1);
    EXPECT_EQ(obj.value("pageSize").toInt(), 2);
    EXPECT_EQ(obj.value("projects").toArray().size(), 2);

    // page=2&pageSize=2 — should get 1 remaining
    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects?page=2&pageSize=2"), QByteArray(),
                            status, body, error));
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 3);
    EXPECT_EQ(obj.value("projects").toArray().size(), 1);

    // page=999 — empty
    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects?page=999"), QByteArray(), status,
                            body, error));
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 3);
    EXPECT_EQ(obj.value("projects").toArray().size(), 0);

    // pageSize=0 normalizes to 1
    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects?pageSize=0"), QByteArray(), status,
                            body, error));
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("pageSize").toInt(), 1);
    EXPECT_EQ(obj.value("projects").toArray().size(), 1);

    // pageSize=200 normalizes to 100
    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects?pageSize=200"), QByteArray(),
                            status, body, error));
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("pageSize").toInt(), 100);
}

TEST(ApiRouterTest, ProjectListFilterByServiceId) {
    M51Fixture f;
    bool ok = f.setup(2, [](const QString& root) {
        writeService(root, "svc-a");
        writeService(root, "svc-b");
        writeProjectEx(root, "pa", "svc-a", true);
        writeProjectEx(root, "pb", "svc-b", true);
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects?serviceId=svc-a"), QByteArray(),
                            status, body, error));
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 1);
    EXPECT_EQ(obj.value("projects").toArray()[0].toObject().value("id").toString(), "pa");
}

TEST(ApiRouterTest, ProjectListFilterByEnabled) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "demo");
        writeProjectEx(root, "p-on", "demo", true);
        writeProjectEx(root, "p-off", "demo", false);
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects?enabled=true"), QByteArray(),
                            status, body, error));
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 1);
    EXPECT_EQ(obj.value("projects").toArray()[0].toObject().value("id").toString(), "p-on");

    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects?enabled=false"), QByteArray(),
                            status, body, error));
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 1);
    EXPECT_EQ(obj.value("projects").toArray()[0].toObject().value("id").toString(), "p-off");
}

TEST(ApiRouterTest, ProjectListFilterByStatus) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "demo");
        writeProjectEx(root, "p-on", "demo", true);
        writeProjectEx(root, "p-off", "demo", false);
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // p-on is valid+enabled+no instances => "stopped"
    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects?status=stopped"), QByteArray(),
                            status, body, error));
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 1);
    EXPECT_EQ(obj.value("projects").toArray()[0].toObject().value("id").toString(), "p-on");

    // p-off is valid+disabled => "disabled"
    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects?status=disabled"), QByteArray(),
                            status, body, error));
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("total").toInt(), 1);
    EXPECT_EQ(obj.value("projects").toArray()[0].toObject().value("id").toString(), "p-off");
}

TEST(ApiRouterTest, PatchProjectEnabledToggle) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "demo");
        writeProjectEx(root, "p1", "demo", true);
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // Disable
    ASSERT_TRUE(
        sendRequest("PATCH", QUrl(f.base + "/api/projects/p1/enabled"),
                    QJsonDocument(QJsonObject{{"enabled", false}}).toJson(QJsonDocument::Compact),
                    status, body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_FALSE(obj.value("enabled").toBool());
    EXPECT_EQ(obj.value("status").toString(), "disabled");

    // Re-enable
    ASSERT_TRUE(
        sendRequest("PATCH", QUrl(f.base + "/api/projects/p1/enabled"),
                    QJsonDocument(QJsonObject{{"enabled", true}}).toJson(QJsonDocument::Compact),
                    status, body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_TRUE(obj.value("enabled").toBool());
    EXPECT_EQ(obj.value("status").toString(), "stopped");
}

TEST(ApiRouterTest, PatchProjectEnabledBadRequest) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "demo");
        writeProjectEx(root, "p1", "demo", true);
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    // Missing enabled field
    ASSERT_TRUE(sendRequest("PATCH", QUrl(f.base + "/api/projects/p1/enabled"),
                            QJsonDocument(QJsonObject{}).toJson(QJsonDocument::Compact), status,
                            body, error));
    EXPECT_EQ(status, 400);

    // Non-bool enabled
    ASSERT_TRUE(
        sendRequest("PATCH", QUrl(f.base + "/api/projects/p1/enabled"),
                    QJsonDocument(QJsonObject{{"enabled", "yes"}}).toJson(QJsonDocument::Compact),
                    status, body, error));
    EXPECT_EQ(status, 400);

    // Not found
    ASSERT_TRUE(
        sendRequest("PATCH", QUrl(f.base + "/api/projects/missing/enabled"),
                    QJsonDocument(QJsonObject{{"enabled", false}}).toJson(QJsonDocument::Compact),
                    status, body, error));
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ProjectLogsReturnsLines) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "demo");
        writeProjectEx(root, "p1", "demo", true);
        // Write a log file
        writeText(root + "/logs/p1.log", "line1\nline2\nline3\nline4\nline5\n");
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // Default lines
    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects/p1/logs"), QByteArray(), status,
                            body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("projectId").toString(), "p1");
    EXPECT_EQ(obj.value("lines").toArray().size(), 5);

    // lines=2
    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects/p1/logs?lines=2"), QByteArray(),
                            status, body, error));
    ASSERT_TRUE(parseJsonObject(body, obj));
    const QJsonArray logLines = obj.value("lines").toArray();
    EXPECT_EQ(logLines.size(), 2);
    EXPECT_EQ(logLines[0].toString(), "line4");
    EXPECT_EQ(logLines[1].toString(), "line5");
}

TEST(ApiRouterTest, ProjectLogsNoFileReturnsEmpty) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "demo");
        writeProjectEx(root, "p1", "demo", true);
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects/p1/logs"), QByteArray(), status,
                            body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("lines").toArray().size(), 0);
}

TEST(ApiRouterTest, ProjectLogsNotFoundForMissingProject) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "demo");
        writeProjectEx(root, "p1", "demo", true);
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects/missing/logs"), QByteArray(),
                            status, body, error));
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ProjectRuntimeBatchReturnsAll) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "demo");
        writeProjectEx(root, "p1", "demo", true);
        writeProjectEx(root, "p2", "demo", true);
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    // No ids param — returns all
    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects/runtime"), QByteArray(), status,
                            body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    const QJsonArray runtimes = obj.value("runtimes").toArray();
    EXPECT_EQ(runtimes.size(), 2);

    // Each entry has schedule info
    for (const QJsonValue& v : runtimes) {
        const QJsonObject entry = v.toObject();
        EXPECT_FALSE(entry.value("id").toString().isEmpty());
        EXPECT_TRUE(entry.contains("schedule"));
        EXPECT_TRUE(entry.contains("runningInstances"));
    }
}

TEST(ApiRouterTest, ProjectRuntimeBatchFilterByIds) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "demo");
        writeProjectEx(root, "p1", "demo", true);
        writeProjectEx(root, "p2", "demo", true);
        writeProjectEx(root, "p3", "demo", true);
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects/runtime?ids=p1,p3"), QByteArray(),
                            status, body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    const QJsonArray runtimes = obj.value("runtimes").toArray();
    EXPECT_EQ(runtimes.size(), 2);

    QStringList ids;
    for (const QJsonValue& v : runtimes) {
        ids.append(v.toObject().value("id").toString());
    }
    EXPECT_TRUE(ids.contains("p1"));
    EXPECT_TRUE(ids.contains("p3"));
    EXPECT_FALSE(ids.contains("p2"));
}

TEST(ApiRouterTest, ProjectRuntimeBatchSkipsUnknownIds) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "demo");
        writeProjectEx(root, "p1", "demo", true);
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/projects/runtime?ids=p1,missing"),
                            QByteArray(), status, body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("runtimes").toArray().size(), 1);
}

// --- M52 Tests ---

TEST(ApiRouterTest, CreateServiceMinimalViaHttp) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        // No services pre-created
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    const QJsonObject createReq{{"id", "new-svc"}, {"name", "New Service"}, {"version", "1.0.0"}};

    ASSERT_TRUE(sendRequest("POST", QUrl(f.base + "/api/services"),
                            QJsonDocument(createReq).toJson(QJsonDocument::Compact), status, body,
                            error));
    EXPECT_EQ(status, 201);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("id").toString(), "new-svc");
    EXPECT_EQ(obj.value("name").toString(), "New Service");
    EXPECT_TRUE(obj.value("created").toBool());

    // Verify it shows up in service list
    ASSERT_TRUE(
        sendRequest("GET", QUrl(f.base + "/api/services"), QByteArray(), status, body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    const QJsonArray services = obj.value("services").toArray();
    EXPECT_EQ(services.size(), 1);
    EXPECT_EQ(services[0].toObject().value("id").toString(), "new-svc");
}

TEST(ApiRouterTest, CreateServiceConflict) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "existing"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    const QJsonObject createReq{{"id", "existing"}, {"name", "Dup"}, {"version", "1.0.0"}};

    ASSERT_TRUE(sendRequest("POST", QUrl(f.base + "/api/services"),
                            QJsonDocument(createReq).toJson(QJsonDocument::Compact), status, body,
                            error));
    EXPECT_EQ(status, 409);
}

TEST(ApiRouterTest, CreateServiceBadRequest) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {});
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    // Missing name
    ASSERT_TRUE(sendRequest("POST", QUrl(f.base + "/api/services"),
                            QJsonDocument(QJsonObject{{"id", "x"}, {"version", "1.0.0"}})
                                .toJson(QJsonDocument::Compact),
                            status, body, error));
    EXPECT_EQ(status, 400);

    // Invalid id
    ASSERT_TRUE(sendRequest(
        "POST", QUrl(f.base + "/api/services"),
        QJsonDocument(QJsonObject{{"id", "bad/id"}, {"name", "X"}, {"version", "1.0.0"}})
            .toJson(QJsonDocument::Compact),
        status, body, error));
    EXPECT_EQ(status, 400);
}

TEST(ApiRouterTest, DeleteServiceViaHttp) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "to-delete"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("DELETE", QUrl(f.base + "/api/services/to-delete"), QByteArray(),
                            status, body, error));
    EXPECT_EQ(status, 204);

    // Verify it's gone
    ASSERT_TRUE(
        sendRequest("GET", QUrl(f.base + "/api/services"), QByteArray(), status, body, error));
    QJsonObject obj;
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("services").toArray().size(), 0);
}

TEST(ApiRouterTest, DeleteServiceNotFound) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {});
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("DELETE", QUrl(f.base + "/api/services/nonexistent"), QByteArray(),
                            status, body, error));
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, DeleteServiceBlockedByAssociatedProjects) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "svc");
        writeProjectEx(root, "p1", "svc", true);
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("DELETE", QUrl(f.base + "/api/services/svc"), QByteArray(), status,
                            body, error));
    EXPECT_EQ(status, 409);
}

TEST(ApiRouterTest, DeleteServiceForceWithAssociatedProjects) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "svc");
        writeProjectEx(root, "p1", "svc", true);
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("DELETE", QUrl(f.base + "/api/services/svc?force=true"), QByteArray(),
                            status, body, error));
    EXPECT_EQ(status, 204);

    // Verify project is now invalid
    ASSERT_TRUE(
        sendRequest("GET", QUrl(f.base + "/api/projects/p1"), QByteArray(), status, body, error));
    EXPECT_EQ(status, 200);
    QJsonObject obj;
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_FALSE(obj.value("valid").toBool());
}

// --- M53 Tests: Service File CRUD ---

TEST(ApiRouterTest, ServiceFilesListsCoreFiles) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/services/demo/files"), QByteArray(), status,
                            body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("serviceId").toString(), "demo");

    const QJsonArray files = obj.value("files").toArray();
    ASSERT_GE(files.size(), 3);

    QStringList paths;
    for (const QJsonValue& v : files) {
        paths.append(v.toObject().value("path").toString());
    }
    EXPECT_TRUE(paths.contains("manifest.json"));
    EXPECT_TRUE(paths.contains("index.js"));
    EXPECT_TRUE(paths.contains("config.schema.json"));
}

TEST(ApiRouterTest, ServiceFilesIncludesSubdirFiles) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "demo");
        QDir().mkpath(root + "/services/demo/lib");
        writeText(root + "/services/demo/lib/utils.js", "// utils");
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/services/demo/files"), QByteArray(), status,
                            body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));

    const QJsonArray files = obj.value("files").toArray();
    QStringList paths;
    for (const QJsonValue& v : files) {
        paths.append(v.toObject().value("path").toString());
    }
    EXPECT_TRUE(paths.contains("lib/utils.js"));
}

TEST(ApiRouterTest, ServiceFileReadContent) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/services/demo/files/content?path=index.js"),
                            QByteArray(), status, body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("path").toString(), "index.js");
    EXPECT_FALSE(obj.value("content").toString().isEmpty());
    EXPECT_GT(obj.value("size").toInteger(), 0);
}

TEST(ApiRouterTest, ServiceFileReadTraversalReturns400) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("GET",
                            QUrl(f.base + "/api/services/demo/files/content?path=../etc/passwd"),
                            QByteArray(), status, body, error));
    EXPECT_EQ(status, 400);
}

TEST(ApiRouterTest, ServiceFileReadNonexistentReturns404) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("GET",
                            QUrl(f.base + "/api/services/demo/files/content?path=nonexist.js"),
                            QByteArray(), status, body, error));
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ServiceFileReadMissingPathReturns400) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/services/demo/files/content"), QByteArray(),
                            status, body, error));
    EXPECT_EQ(status, 400);
}

TEST(ApiRouterTest, ServiceFileWriteUpdatesContent) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    const QJsonObject writeReq{{"content", "// updated content\n"}};
    ASSERT_TRUE(sendRequest("PUT", QUrl(f.base + "/api/services/demo/files/content?path=index.js"),
                            QJsonDocument(writeReq).toJson(QJsonDocument::Compact), status, body,
                            error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("path").toString(), "index.js");

    // Verify content was updated
    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/services/demo/files/content?path=index.js"),
                            QByteArray(), status, body, error));
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("content").toString(), "// updated content\n");
}

TEST(ApiRouterTest, ServiceFileWriteManifestValidJson) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    QJsonObject manifest;
    manifest["manifestVersion"] = "1";
    manifest["id"] = "demo";
    manifest["name"] = "Updated Demo";
    manifest["version"] = "2.0.0";
    const QString manifestStr =
        QString::fromUtf8(QJsonDocument(manifest).toJson(QJsonDocument::Compact));

    const QJsonObject writeReq{{"content", manifestStr}};
    ASSERT_TRUE(
        sendRequest("PUT", QUrl(f.base + "/api/services/demo/files/content?path=manifest.json"),
                    QJsonDocument(writeReq).toJson(QJsonDocument::Compact), status, body, error));
    EXPECT_EQ(status, 200);

    // Verify memory was updated
    EXPECT_EQ(f.manager->services().value("demo").name, "Updated Demo");
    EXPECT_EQ(f.manager->services().value("demo").version, "2.0.0");
}

TEST(ApiRouterTest, ServiceFileWriteManifestInvalidJson) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    const QJsonObject writeReq{{"content", "not valid json{{{"}};
    ASSERT_TRUE(
        sendRequest("PUT", QUrl(f.base + "/api/services/demo/files/content?path=manifest.json"),
                    QJsonDocument(writeReq).toJson(QJsonDocument::Compact), status, body, error));
    EXPECT_EQ(status, 400);
}

TEST(ApiRouterTest, ServiceFileWriteManifestInvalidJsonWithDotPathStillBlocked) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    const QString manifestPath = f.root + "/services/demo/manifest.json";
    QFile manifestFile(manifestPath);
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QByteArray originalManifest = manifestFile.readAll();

    int status = 0;
    QByteArray body;
    QString error;

    const QJsonObject writeReq{{"content", "not valid json{{{"}};
    ASSERT_TRUE(
        sendRequest("PUT", QUrl(f.base + "/api/services/demo/files/content?path=./manifest.json"),
                    QJsonDocument(writeReq).toJson(QJsonDocument::Compact), status, body, error));
    EXPECT_EQ(status, 400);

    QFile manifestFileAfter(manifestPath);
    ASSERT_TRUE(manifestFileAfter.open(QIODevice::ReadOnly));
    EXPECT_EQ(manifestFileAfter.readAll(), originalManifest);
}

TEST(ApiRouterTest, ServiceFileWriteSchemaValidJson) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    const QJsonObject schema{{"host", QJsonObject{{"type", "string"}, {"required", true}}}};
    const QString schemaStr =
        QString::fromUtf8(QJsonDocument(schema).toJson(QJsonDocument::Compact));

    const QJsonObject writeReq{{"content", schemaStr}};
    ASSERT_TRUE(sendRequest(
        "PUT", QUrl(f.base + "/api/services/demo/files/content?path=config.schema.json"),
        QJsonDocument(writeReq).toJson(QJsonDocument::Compact), status, body, error));
    EXPECT_EQ(status, 200);

    // Verify memory was updated
    EXPECT_EQ(f.manager->services().value("demo").rawConfigSchema, schema);
}

TEST(ApiRouterTest, ServiceFileWriteSchemaInvalidJson) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    const QJsonObject writeReq{{"content", "[not an object]"}};
    ASSERT_TRUE(sendRequest(
        "PUT", QUrl(f.base + "/api/services/demo/files/content?path=config.schema.json"),
        QJsonDocument(writeReq).toJson(QJsonDocument::Compact), status, body, error));
    EXPECT_EQ(status, 400);
}

TEST(ApiRouterTest, ServiceFileWriteOversizedReturns413) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    // Create content > 1MB
    const QString bigContent(1024 * 1024 + 1, 'x');
    const QJsonObject writeReq{{"content", bigContent}};
    ASSERT_TRUE(sendRequest("PUT", QUrl(f.base + "/api/services/demo/files/content?path=index.js"),
                            QJsonDocument(writeReq).toJson(QJsonDocument::Compact), status, body,
                            error));
    EXPECT_EQ(status, 413);
}

TEST(ApiRouterTest, ServiceFileCreateNewFile) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    const QJsonObject createReq{{"content", "// helper\n"}};
    ASSERT_TRUE(
        sendRequest("POST", QUrl(f.base + "/api/services/demo/files/content?path=lib/helper.js"),
                    QJsonDocument(createReq).toJson(QJsonDocument::Compact), status, body, error));
    EXPECT_EQ(status, 201);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("path").toString(), "lib/helper.js");

    // Verify file exists
    ASSERT_TRUE(sendRequest("GET",
                            QUrl(f.base + "/api/services/demo/files/content?path=lib/helper.js"),
                            QByteArray(), status, body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("content").toString(), "// helper\n");
}

TEST(ApiRouterTest, ServiceFileCreateConflict) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    const QJsonObject createReq{{"content", "dup"}};
    ASSERT_TRUE(sendRequest("POST", QUrl(f.base + "/api/services/demo/files/content?path=index.js"),
                            QJsonDocument(createReq).toJson(QJsonDocument::Compact), status, body,
                            error));
    EXPECT_EQ(status, 409);
}

TEST(ApiRouterTest, ServiceFileDeleteNonCoreFile) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        writeService(root, "demo");
        QDir().mkpath(root + "/services/demo/lib");
        writeText(root + "/services/demo/lib/helper.js", "// helper");
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(f.base + "/api/services/demo/files/content?path=lib/helper.js"),
                            QByteArray(), status, body, error));
    EXPECT_EQ(status, 204);

    // Verify file is gone
    ASSERT_TRUE(sendRequest("GET",
                            QUrl(f.base + "/api/services/demo/files/content?path=lib/helper.js"),
                            QByteArray(), status, body, error));
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ServiceFileDeleteCoreFileBlocked) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(f.base + "/api/services/demo/files/content?path=manifest.json"),
                            QByteArray(), status, body, error));
    EXPECT_EQ(status, 400);

    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(f.base + "/api/services/demo/files/content?path=index.js"),
                            QByteArray(), status, body, error));
    EXPECT_EQ(status, 400);
}

TEST(ApiRouterTest, ServiceFileDeleteCoreFileBlockedWithDotPath) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(f.base + "/api/services/demo/files/content?path=./index.js"),
                            QByteArray(), status, body, error));
    EXPECT_EQ(status, 400);
    EXPECT_TRUE(QFileInfo::exists(f.root + "/services/demo/index.js"));
}

TEST(ApiRouterTest, ServiceFileDeleteNonexistentReturns404) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(f.base + "/api/services/demo/files/content?path=nonexist.js"),
                            QByteArray(), status, body, error));
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ServiceFileOpsReturn404ForMissingService) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("GET", QUrl(f.base + "/api/services/missing/files"), QByteArray(),
                            status, body, error));
    EXPECT_EQ(status, 404);

    ASSERT_TRUE(sendRequest("GET",
                            QUrl(f.base + "/api/services/missing/files/content?path=index.js"),
                            QByteArray(), status, body, error));
    EXPECT_EQ(status, 404);

    ASSERT_TRUE(
        sendRequest("PUT", QUrl(f.base + "/api/services/missing/files/content?path=index.js"),
                    QJsonDocument(QJsonObject{{"content", "x"}}).toJson(QJsonDocument::Compact),
                    status, body, error));
    EXPECT_EQ(status, 404);

    ASSERT_TRUE(
        sendRequest("POST", QUrl(f.base + "/api/services/missing/files/content?path=new.js"),
                    QJsonDocument(QJsonObject{{"content", "x"}}).toJson(QJsonDocument::Compact),
                    status, body, error));
    EXPECT_EQ(status, 404);

    ASSERT_TRUE(sendRequest("DELETE",
                            QUrl(f.base + "/api/services/missing/files/content?path=index.js"),
                            QByteArray(), status, body, error));
    EXPECT_EQ(status, 404);
}

// --- M54 Tests: Schema/Config Tool APIs ---

TEST(ApiRouterTest, ValidateSchemaValidReturnsFields) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    const QJsonObject schema{{"port", QJsonObject{{"type", "int"}, {"required", true}}},
                             {"name", QJsonObject{{"type", "string"}}}};
    const QJsonObject reqBody{{"schema", schema}};

    ASSERT_TRUE(sendRequest("POST", QUrl(f.base + "/api/services/demo/validate-schema"),
                            QJsonDocument(reqBody).toJson(QJsonDocument::Compact), status, body,
                            error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_TRUE(obj.value("valid").toBool());
    EXPECT_TRUE(obj.contains("fields"));
    EXPECT_EQ(obj.value("fields").toArray().size(), 2);
}

TEST(ApiRouterTest, ValidateSchemaInvalidType) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    const QJsonObject schema{{"createdAt", QJsonObject{{"type", "datetime"}}}};
    const QJsonObject reqBody{{"schema", schema}};

    ASSERT_TRUE(sendRequest("POST", QUrl(f.base + "/api/services/demo/validate-schema"),
                            QJsonDocument(reqBody).toJson(QJsonDocument::Compact), status, body,
                            error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_FALSE(obj.value("valid").toBool());
    EXPECT_FALSE(obj.value("error").toString().isEmpty());
}

TEST(ApiRouterTest, ValidateSchemaMissingSchemaField) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("POST", QUrl(f.base + "/api/services/demo/validate-schema"),
                            QJsonDocument(QJsonObject{}).toJson(QJsonDocument::Compact), status,
                            body, error));
    EXPECT_EQ(status, 400);
}

TEST(ApiRouterTest, ValidateSchemaServiceNotFound) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    const QJsonObject reqBody{{"schema", QJsonObject{}}};
    ASSERT_TRUE(sendRequest("POST", QUrl(f.base + "/api/services/missing/validate-schema"),
                            QJsonDocument(reqBody).toJson(QJsonDocument::Compact), status, body,
                            error));
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, GenerateDefaultsReturnsConfig) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        const QString serviceDir = root + "/services/svc-defaults";
        QDir().mkpath(serviceDir);
        writeText(
            serviceDir + "/manifest.json",
            R"({"manifestVersion":"1","id":"svc-defaults","name":"Defaults","version":"1.0.0"})");
        writeText(serviceDir + "/index.js", "console.log('ok');\n");
        writeText(
            serviceDir + "/config.schema.json",
            R"({"port":{"type":"int","required":true,"default":8080},"name":{"type":"string","required":true},"debug":{"type":"bool","default":false}})");
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    ASSERT_TRUE(sendRequest("POST", QUrl(f.base + "/api/services/svc-defaults/generate-defaults"),
                            QJsonDocument(QJsonObject{}).toJson(QJsonDocument::Compact), status,
                            body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_EQ(obj.value("serviceId").toString(), "svc-defaults");

    const QJsonObject config = obj.value("config").toObject();
    EXPECT_EQ(config.value("port").toInt(), 8080);
    EXPECT_EQ(config.value("debug").toBool(), false);
    EXPECT_FALSE(config.contains("name"));

    const QJsonArray required = obj.value("requiredFields").toArray();
    const QJsonArray optional = obj.value("optionalFields").toArray();
    EXPECT_GE(required.size(), 1);
    EXPECT_GE(optional.size(), 1);
}

TEST(ApiRouterTest, GenerateDefaultsEmptySchema) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        const QString serviceDir = root + "/services/svc-empty";
        QDir().mkpath(serviceDir);
        writeText(serviceDir + "/manifest.json",
                  R"({"manifestVersion":"1","id":"svc-empty","name":"Empty","version":"1.0.0"})");
        writeText(serviceDir + "/index.js", "console.log('ok');\n");
        writeText(serviceDir + "/config.schema.json", "{}");
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    ASSERT_TRUE(sendRequest("POST", QUrl(f.base + "/api/services/svc-empty/generate-defaults"),
                            QJsonDocument(QJsonObject{}).toJson(QJsonDocument::Compact), status,
                            body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_TRUE(obj.value("config").toObject().isEmpty());
}

TEST(ApiRouterTest, GenerateDefaultsServiceNotFound) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    ASSERT_TRUE(sendRequest("POST", QUrl(f.base + "/api/services/missing/generate-defaults"),
                            QJsonDocument(QJsonObject{}).toJson(QJsonDocument::Compact), status,
                            body, error));
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ValidateConfigValid) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    const QJsonObject config{{"device", QJsonObject{{"host", "127.0.0.1"}}}};
    const QJsonObject reqBody{{"config", config}};

    ASSERT_TRUE(sendRequest("POST", QUrl(f.base + "/api/services/demo/validate-config"),
                            QJsonDocument(reqBody).toJson(QJsonDocument::Compact), status, body,
                            error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_TRUE(obj.value("valid").toBool());
}

TEST(ApiRouterTest, ValidateConfigMissingRequired) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) {
        const QString serviceDir = root + "/services/svc-req";
        QDir().mkpath(serviceDir);
        writeText(serviceDir + "/manifest.json",
                  R"({"manifestVersion":"1","id":"svc-req","name":"Req","version":"1.0.0"})");
        writeText(serviceDir + "/index.js", "console.log('ok');\n");
        writeText(serviceDir + "/config.schema.json",
                  R"({"name":{"type":"string","required":true}})");
    });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    const QJsonObject reqBody{{"config", QJsonObject{}}};
    ASSERT_TRUE(sendRequest("POST", QUrl(f.base + "/api/services/svc-req/validate-config"),
                            QJsonDocument(reqBody).toJson(QJsonDocument::Compact), status, body,
                            error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_FALSE(obj.value("valid").toBool());
    EXPECT_FALSE(obj.value("errors").toArray().isEmpty());
    EXPECT_EQ(obj.value("errors").toArray()[0].toObject().value("field").toString(), "name");
}

TEST(ApiRouterTest, ValidateConfigServiceNotFound) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;

    const QJsonObject reqBody{{"config", QJsonObject{}}};
    ASSERT_TRUE(sendRequest("POST", QUrl(f.base + "/api/services/missing/validate-config"),
                            QJsonDocument(reqBody).toJson(QJsonDocument::Compact), status, body,
                            error));
    EXPECT_EQ(status, 404);
}

TEST(ApiRouterTest, ServiceDetailContainsConfigSchemaFields) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject obj;

    ASSERT_TRUE(
        sendRequest("GET", QUrl(f.base + "/api/services/demo"), QByteArray(), status, body, error));
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(parseJsonObject(body, obj));
    EXPECT_TRUE(obj.contains("configSchemaFields"));
    EXPECT_TRUE(obj.value("configSchemaFields").isArray());

    const QJsonObject rawSchema = obj.value("configSchema").toObject();
    const QJsonArray schemaFields = obj.value("configSchemaFields").toArray();
    EXPECT_EQ(schemaFields.size(), rawSchema.size());
}

TEST(ApiRouterTest, EventStreamReconnectsDoNotGetStuckAt429) {
    M51Fixture f;
    bool ok = f.setup(1, [](const QString& root) { writeService(root, "demo"); });
    if (!ok) {
        GTEST_SKIP() << "Cannot set up fixture";
    }

    for (int i = 0; i < 40; ++i) {
        const int status = connectSseAndAbort(QUrl(f.base + "/api/events/stream"));
        ASSERT_NE(status, 0) << "iteration=" << i;
        EXPECT_NE(status, 429) << "iteration=" << i;
    }
}
