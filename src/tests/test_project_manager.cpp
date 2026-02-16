#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "stdiolink_server/manager/project_manager.h"

using namespace stdiolink_server;

namespace {

bool writeJsonFile(const QString& path, const QJsonObject& obj) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact)) > 0;
}

bool writeRawFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(content) == content.size();
}

QMap<QString, ServiceInfo> makeServices() {
    ServiceInfo service;
    service.id = "demo";
    service.name = "Demo";
    service.version = "1.0.0";
    service.hasSchema = true;
    service.valid = true;

    QJsonObject schemaObj{
        {"device", QJsonObject{
             {"type", "object"},
             {"fields", QJsonObject{
                  {"host", QJsonObject{{"type", "string"}, {"required", true}}},
                  {"port", QJsonObject{{"type", "int"}, {"default", 502}}}
              }}
         }}
    };

    service.rawConfigSchema = schemaObj;
    service.configSchema = stdiolink_service::ServiceConfigSchema::fromJsObject(schemaObj);

    return {{"demo", service}};
}

} // namespace

class ProjectManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmpDir.isValid());
        projectsDir = tmpDir.path() + "/projects";
        ASSERT_TRUE(QDir().mkpath(projectsDir));
    }

    void writeProject(const QString& id, const QJsonObject& obj) {
        ASSERT_TRUE(writeJsonFile(projectsDir + "/" + id + ".json", obj));
    }

    QTemporaryDir tmpDir;
    QString projectsDir;
};

TEST_F(ProjectManagerTest, EmptyDirectory) {
    ProjectManager manager;
    ProjectManager::LoadStats stats;

    const auto result = manager.loadAll(projectsDir, makeServices(), &stats);
    EXPECT_TRUE(result.isEmpty());
    EXPECT_EQ(stats.loaded, 0);
    EXPECT_EQ(stats.invalid, 0);
}

TEST_F(ProjectManagerTest, ValidProjectAndDefaultsMerged) {
    writeProject("test_1", QJsonObject{
        {"name", "Test"},
        {"serviceId", "demo"},
        {"enabled", true},
        {"schedule", QJsonObject{{"type", "manual"}}},
        {"config", QJsonObject{{"device", QJsonObject{{"host", "127.0.0.1"}}}}}
    });

    ProjectManager manager;
    ProjectManager::LoadStats stats;
    const auto result = manager.loadAll(projectsDir, makeServices(), &stats);

    ASSERT_EQ(result.size(), 1);
    ASSERT_TRUE(result.contains("test_1"));
    EXPECT_TRUE(result["test_1"].valid);
    EXPECT_EQ(result["test_1"].config["device"].toObject()["port"].toInt(), 502);
    EXPECT_EQ(stats.loaded, 1);
    EXPECT_EQ(stats.invalid, 0);
}

TEST_F(ProjectManagerTest, InvalidJson) {
    ASSERT_TRUE(writeRawFile(projectsDir + "/bad.json", "not-json"));

    ProjectManager manager;
    ProjectManager::LoadStats stats;
    const auto result = manager.loadAll(projectsDir, makeServices(), &stats);

    EXPECT_EQ(stats.invalid, 1);
    ASSERT_TRUE(result.contains("bad"));
    EXPECT_FALSE(result["bad"].valid);
}

TEST_F(ProjectManagerTest, UnknownService) {
    writeProject("orphan", QJsonObject{
        {"name", "Orphan"},
        {"serviceId", "missing"},
        {"enabled", true},
        {"schedule", QJsonObject{{"type", "manual"}}},
        {"config", QJsonObject{{"device", QJsonObject{{"host", "127.0.0.1"}}}}}
    });

    ProjectManager manager;
    ProjectManager::LoadStats stats;
    const auto result = manager.loadAll(projectsDir, makeServices(), &stats);

    EXPECT_EQ(stats.invalid, 1);
    ASSERT_TRUE(result.contains("orphan"));
    EXPECT_FALSE(result["orphan"].valid);
}

TEST_F(ProjectManagerTest, BodyIdMismatchMarkedInvalid) {
    writeProject("p1", QJsonObject{
        {"id", "another"},
        {"name", "Mismatch"},
        {"serviceId", "demo"},
        {"enabled", true},
        {"schedule", QJsonObject{{"type", "manual"}}},
        {"config", QJsonObject{{"device", QJsonObject{{"host", "127.0.0.1"}}}}}
    });

    ProjectManager manager;
    ProjectManager::LoadStats stats;
    const auto result = manager.loadAll(projectsDir, makeServices(), &stats);

    ASSERT_TRUE(result.contains("p1"));
    EXPECT_FALSE(result["p1"].valid);
    EXPECT_EQ(stats.invalid, 1);
}

TEST_F(ProjectManagerTest, InvalidProjectIdFilteredByFilename) {
    writeProject("bad id", QJsonObject{
        {"name", "Bad"},
        {"serviceId", "demo"},
        {"enabled", true},
        {"schedule", QJsonObject{{"type", "manual"}}},
        {"config", QJsonObject{{"device", QJsonObject{{"host", "127.0.0.1"}}}}}
    });

    ProjectManager manager;
    ProjectManager::LoadStats stats;
    const auto result = manager.loadAll(projectsDir, makeServices(), &stats);

    EXPECT_TRUE(result.isEmpty());
    EXPECT_EQ(stats.loaded, 0);
    EXPECT_EQ(stats.invalid, 0);
}

TEST(ProjectManagerIdTest, ValidateProjectId) {
    EXPECT_FALSE(ProjectManager::isValidProjectId(""));
    EXPECT_FALSE(ProjectManager::isValidProjectId("a/b"));
    EXPECT_FALSE(ProjectManager::isValidProjectId("a b"));
    EXPECT_TRUE(ProjectManager::isValidProjectId("silo-a"));
    EXPECT_TRUE(ProjectManager::isValidProjectId("test_123"));
}

TEST(ProjectManagerIoTest, SaveAndRemoveProject) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString projectsDir = tmpDir.path() + "/projects";

    Project project;
    project.id = "save_test";
    project.name = "SaveTest";
    project.serviceId = "demo";
    project.enabled = true;
    project.schedule = Schedule{};
    project.config = QJsonObject{{"device", QJsonObject{{"host", "127.0.0.1"}}}};

    QString error;
    EXPECT_TRUE(ProjectManager::saveProject(projectsDir, project, error));
    EXPECT_TRUE(error.isEmpty());
    EXPECT_TRUE(QFile::exists(projectsDir + "/save_test.json"));

    EXPECT_TRUE(ProjectManager::removeProject(projectsDir, "save_test", error));
    EXPECT_TRUE(error.isEmpty());
    EXPECT_FALSE(QFile::exists(projectsDir + "/save_test.json"));
}

// M72_G04 — saveProject normal success with atomic write
TEST(ProjectManagerIoTest, M72_G04_SaveProjectAtomicWriteSuccess) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString projectsDir = tmpDir.path() + "/projects";

    Project project;
    project.id = "atomic_test";
    project.name = "AtomicTest";
    project.serviceId = "demo";
    project.enabled = true;
    project.schedule = Schedule{};
    project.config = QJsonObject{{"device", QJsonObject{{"host", "10.0.0.1"}}}};

    QString error;
    EXPECT_TRUE(ProjectManager::saveProject(projectsDir, project, error));
    EXPECT_TRUE(error.isEmpty());

    // Verify file exists and is valid JSON
    const QString filePath = projectsDir + "/atomic_test.json";
    ASSERT_TRUE(QFile::exists(filePath));

    QFile file(filePath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    EXPECT_EQ(parseErr.error, QJsonParseError::NoError);
    EXPECT_TRUE(doc.isObject());

    const QJsonObject obj = doc.object();
    EXPECT_EQ(obj.value("name").toString(), "AtomicTest");
    EXPECT_EQ(obj.value("serviceId").toString(), "demo");
}

// M72_R04 — saveProject failure preserves old file
TEST(ProjectManagerIoTest, M72_R04_SaveProjectIsAtomicOnWriteFailure) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString projectsDir = tmpDir.path() + "/projects";
    ASSERT_TRUE(QDir().mkpath(projectsDir));

    // Write an initial file
    const QString filePath = projectsDir + "/preserve_test.json";
    {
        QFile f(filePath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(R"({"name":"original","serviceId":"demo"})");
    }

    // Try to save to a read-only directory (simulate failure)
    // Make the projects dir read-only
    QFile::setPermissions(projectsDir,
                          QFileDevice::ReadOwner | QFileDevice::ExeOwner);

    Project project;
    project.id = "preserve_test";
    project.name = "Modified";
    project.serviceId = "demo";
    project.enabled = true;
    project.schedule = Schedule{};
    project.config = QJsonObject{};

    QString error;
    const bool saved = ProjectManager::saveProject(projectsDir, project, error);

    // Restore permissions for cleanup
    QFile::setPermissions(projectsDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    // If setPermissions didn't actually prevent writing (e.g. running as root),
    // skip rather than vacuously pass.
    if (saved) {
        GTEST_SKIP() << "Environment cannot simulate write failure (permissions ineffective)";
    }

    EXPECT_FALSE(saved);
    EXPECT_FALSE(error.isEmpty());

    // Old file should be preserved
    QFile f(filePath);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray content = f.readAll();
    EXPECT_TRUE(content.contains("original"));
}
