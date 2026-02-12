#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

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

bool writeText(const QString& path, const QString& content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QTextStream out(&file);
    out << content;
    return file.error() == QFile::NoError;
}

bool copyExecutable(const QString& fromPath, const QString& toPath) {
    QFile::remove(toPath);
    if (!QFile::copy(fromPath, toPath)) {
        return false;
    }
    const QFileDevice::Permissions perms =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
        | QFileDevice::ReadGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::ExeOther;
    return QFile::setPermissions(toPath, perms);
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

} // namespace

TEST(ServerManagerTest, InitializeLoadsServicesAndProjects) {
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
    QString error;
    ASSERT_TRUE(manager.initialize(error));
    EXPECT_TRUE(error.isEmpty());

    ASSERT_EQ(manager.services().size(), 1);
    ASSERT_TRUE(manager.services().contains("demo"));

    ASSERT_EQ(manager.projects().size(), 1);
    ASSERT_TRUE(manager.projects().contains("p1"));
    EXPECT_TRUE(manager.projects()["p1"].valid);

    manager.startScheduling();
    EXPECT_EQ(manager.instanceManager()->instanceCount("p1"), 0);

    manager.shutdown();
}

TEST(ServerManagerTest, RescanDriversLoadsMeta) {
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

    ServerConfig cfg;
    cfg.serviceProgram = testBinaryPath("test_service_stub");

    ServerManager manager(root, cfg);
    QString error;
    ASSERT_TRUE(manager.initialize(error));

    const auto stats = manager.rescanDrivers(true);
    EXPECT_GE(stats.scanned, 1);
    EXPECT_TRUE(manager.driverCatalog()->hasDriver("test-meta-driver"));
}

TEST(ServerManagerTest, RescanServicesRevalidatesProjects) {
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
    QString error;
    ASSERT_TRUE(manager.initialize(error));
    ASSERT_TRUE(manager.projects().value("p1").valid);

    // Remove service directory and trigger manual service scan.
    ASSERT_TRUE(QDir(root + "/services/demo").removeRecursively());
    const ServerManager::ServiceRescanStats stats =
        manager.rescanServices(true, false, false);

    EXPECT_EQ(stats.removed, 1);
    EXPECT_EQ(stats.revalidatedProjects, 1);
    EXPECT_EQ(stats.becameInvalid, 1);
    EXPECT_FALSE(stats.schedulingRestarted);
    EXPECT_TRUE(stats.invalidProjectIds.contains("p1"));
    ASSERT_TRUE(manager.projects().contains("p1"));
    EXPECT_FALSE(manager.projects().value("p1").valid);
}

TEST(ServerManagerTest, InitializeFailsWhenDataRootMissing) {
    ServerConfig cfg;
    ServerManager manager("/path/does/not/exist", cfg);

    QString error;
    EXPECT_FALSE(manager.initialize(error));
    EXPECT_FALSE(error.isEmpty());
}

TEST(ServerManagerTest, ServerStatusReturnsCorrectCounts) {
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
    cfg.host = "0.0.0.0";
    cfg.port = 7777;

    ServerManager manager(root, cfg);
    QString error;
    ASSERT_TRUE(manager.initialize(error));

    const auto s = manager.serverStatus();
    EXPECT_EQ(s.version, "0.1.0");
    EXPECT_TRUE(s.startedAt.isValid());
    EXPECT_GE(s.uptimeMs, 0);
    EXPECT_EQ(s.host, "0.0.0.0");
    EXPECT_EQ(s.port, 7777);
    EXPECT_EQ(s.serviceCount, 1);
    EXPECT_EQ(s.projectTotal, 1);
    EXPECT_EQ(s.projectValid, 1);
    EXPECT_EQ(s.projectInvalid, 0);
    EXPECT_EQ(s.projectEnabled, 1);
    EXPECT_EQ(s.projectDisabled, 0);
    EXPECT_EQ(s.instanceTotal, 0);
    EXPECT_EQ(s.instanceRunning, 0);
    EXPECT_GT(s.cpuCores, 0);
    EXPECT_FALSE(s.platform.isEmpty());
}

TEST(ServerManagerTest, ServerStatusUptimeIncreases) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString error;
    ASSERT_TRUE(manager.initialize(error));

    const qint64 uptime1 = manager.serverStatus().uptimeMs;
    // Small busy-wait to ensure measurable difference
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5) {}
    const qint64 uptime2 = manager.serverStatus().uptimeMs;
    EXPECT_GT(uptime2, uptime1);
}

TEST(ServerManagerTest, CreateServiceMinimalRequest) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString error;
    ASSERT_TRUE(manager.initialize(error));
    ASSERT_EQ(manager.services().size(), 0);

    ServerManager::ServiceCreateRequest req;
    req.id = "new-svc";
    req.name = "New Service";
    req.version = "1.0.0";

    auto result = manager.createService(req);
    ASSERT_TRUE(result.success) << qPrintable(result.error);
    EXPECT_EQ(result.serviceInfo.id, "new-svc");
    EXPECT_EQ(result.serviceInfo.name, "New Service");
    EXPECT_TRUE(result.serviceInfo.valid);

    // Verify in-memory
    ASSERT_TRUE(manager.services().contains("new-svc"));

    // Verify files on disk
    EXPECT_TRUE(QFile::exists(root + "/services/new-svc/manifest.json"));
    EXPECT_TRUE(QFile::exists(root + "/services/new-svc/index.js"));
    EXPECT_TRUE(QFile::exists(root + "/services/new-svc/config.schema.json"));
}

TEST(ServerManagerTest, CreateServiceDuplicateIdFails) {
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
    QString error;
    ASSERT_TRUE(manager.initialize(error));

    ServerManager::ServiceCreateRequest req;
    req.id = "demo";
    req.name = "Duplicate";
    req.version = "1.0.0";

    auto result = manager.createService(req);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.contains("already exists"));
}

TEST(ServerManagerTest, CreateServiceInvalidIdFails) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString error;
    ASSERT_TRUE(manager.initialize(error));

    ServerManager::ServiceCreateRequest req;
    req.id = "bad id!";
    req.name = "Bad";
    req.version = "1.0.0";

    auto result = manager.createService(req);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.contains("invalid"));
}

TEST(ServerManagerTest, DeleteServiceNoProjects) {
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
    QString error;
    ASSERT_TRUE(manager.initialize(error));
    ASSERT_TRUE(manager.services().contains("demo"));

    ASSERT_TRUE(manager.deleteService("demo", false, error)) << qPrintable(error);
    EXPECT_FALSE(manager.services().contains("demo"));
    EXPECT_FALSE(QDir(root + "/services/demo").exists());
}

TEST(ServerManagerTest, DeleteServiceWithProjectsNonForce) {
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
    QString error;
    ASSERT_TRUE(manager.initialize(error));

    EXPECT_FALSE(manager.deleteService("demo", false, error));
    EXPECT_TRUE(error.contains("associated"));
    EXPECT_TRUE(manager.services().contains("demo"));
}

TEST(ServerManagerTest, DeleteServiceWithProjectsForce) {
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
    QString error;
    ASSERT_TRUE(manager.initialize(error));
    ASSERT_TRUE(manager.projects().value("p1").valid);

    ASSERT_TRUE(manager.deleteService("demo", true, error)) << qPrintable(error);
    EXPECT_FALSE(manager.services().contains("demo"));
    EXPECT_TRUE(manager.projects().contains("p1"));
    EXPECT_FALSE(manager.projects().value("p1").valid);
    EXPECT_TRUE(manager.projects().value("p1").error.contains("deleted"));
}

TEST(ServerManagerTest, DeleteServiceNotFound) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString error;
    ASSERT_TRUE(manager.initialize(error));

    EXPECT_FALSE(manager.deleteService("nonexistent", false, error));
    EXPECT_TRUE(error.contains("not found"));
}
