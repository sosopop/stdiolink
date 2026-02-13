#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
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
    const QFileDevice::Permissions perms = QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                           QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                           QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                           QFileDevice::ExeOther;
    return QFile::setPermissions(toPath, perms);
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
    const ServerManager::ServiceRescanStats stats = manager.rescanServices(true, false, false);

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
    ASSERT_TRUE(QFileInfo::exists(cfg.serviceProgram));

    ServerManager manager(root, cfg);
    QString error;
    ASSERT_TRUE(manager.initialize(error));

    const auto s = manager.serverStatus();
    EXPECT_EQ(s.serviceCount, 1);
    EXPECT_EQ(s.projectTotal, 1);
    EXPECT_EQ(s.projectValid, 1);
    EXPECT_EQ(s.projectInvalid, 0);
    EXPECT_EQ(s.projectEnabled, 1);
    EXPECT_EQ(s.instanceTotal, 0);
    EXPECT_EQ(s.driverCount, 0);
    EXPECT_TRUE(s.startedAt.isValid());
    EXPECT_GE(s.uptimeMs, 0);
    EXPECT_GT(s.cpuCores, 0);
    EXPECT_FALSE(s.platform.isEmpty());
}

TEST(ServerManagerTest, CreateServiceMinimal) {
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
    EXPECT_EQ(manager.services().size(), 0);

    ServerManager::ServiceCreateRequest req;
    req.id = "new-svc";
    req.name = "New Service";
    req.version = "1.0.0";

    auto result = manager.createService(req);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.isEmpty());
    EXPECT_EQ(result.serviceInfo.id, "new-svc");
    EXPECT_TRUE(manager.services().contains("new-svc"));
    EXPECT_TRUE(QDir(root + "/services/new-svc").exists());
    EXPECT_TRUE(QFile::exists(root + "/services/new-svc/manifest.json"));
    EXPECT_TRUE(QFile::exists(root + "/services/new-svc/index.js"));
    EXPECT_TRUE(QFile::exists(root + "/services/new-svc/config.schema.json"));
}

TEST(ServerManagerTest, CreateServiceDuplicateReturnsError) {
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
    QString error;
    ASSERT_TRUE(manager.initialize(error));

    ServerManager::ServiceCreateRequest req;
    req.id = "existing";
    req.name = "Dup";
    req.version = "1.0.0";

    auto result = manager.createService(req);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, "service already exists");
}

TEST(ServerManagerTest, CreateServiceInvalidId) {
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
    req.name = "Test";
    req.version = "1.0.0";

    // Empty id
    req.id = "";
    EXPECT_FALSE(manager.createService(req).success);

    // Invalid chars
    req.id = "bad/id";
    EXPECT_FALSE(manager.createService(req).success);

    req.id = "bad id";
    EXPECT_FALSE(manager.createService(req).success);
}

TEST(ServerManagerTest, DeleteServiceNoAssociatedProjects) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "to-delete");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString error;
    ASSERT_TRUE(manager.initialize(error));
    ASSERT_TRUE(manager.services().contains("to-delete"));

    EXPECT_TRUE(manager.deleteService("to-delete", false, error));
    EXPECT_FALSE(manager.services().contains("to-delete"));
    EXPECT_FALSE(QDir(root + "/services/to-delete").exists());
}

TEST(ServerManagerTest, DeleteServiceWithAssociatedProjectsBlocked) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "svc");
    writeProject(root, "p1", "svc");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString error;
    ASSERT_TRUE(manager.initialize(error));

    EXPECT_FALSE(manager.deleteService("svc", false, error));
    EXPECT_TRUE(error.contains("associated projects"));
    EXPECT_TRUE(manager.services().contains("svc"));
}

TEST(ServerManagerTest, DeleteServiceForceInvalidatesProjects) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString root = tmp.path();
    ASSERT_TRUE(QDir().mkpath(root + "/services"));
    ASSERT_TRUE(QDir().mkpath(root + "/projects"));
    ASSERT_TRUE(QDir().mkpath(root + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(root + "/logs"));

    writeService(root, "svc");
    writeProject(root, "p1", "svc");

    ServerConfig cfg;
    ServerManager manager(root, cfg);
    QString error;
    ASSERT_TRUE(manager.initialize(error));
    ASSERT_TRUE(manager.projects().value("p1").valid);

    EXPECT_TRUE(manager.deleteService("svc", true, error));
    EXPECT_FALSE(manager.services().contains("svc"));
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
    EXPECT_EQ(error, "service not found");
}
