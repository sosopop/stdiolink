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

TEST(ServerManagerTest, InitializeFailsWhenDataRootMissing) {
    ServerConfig cfg;
    ServerManager manager("/path/does/not/exist", cfg);

    QString error;
    EXPECT_FALSE(manager.initialize(error));
    EXPECT_FALSE(error.isEmpty());
}
