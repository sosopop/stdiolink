#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QFile>
#include "config/service_directory.h"

using namespace stdiolink_service;

class ServiceDirectoryTest : public ::testing::Test {
protected:
    void createFile(const QString& path, const QByteArray& content = "{}") {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(content);
        f.close();
    }
};

TEST_F(ServiceDirectoryTest, ValidDirectoryWithAllFiles) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/manifest.json");
    createFile(tmp.path() + "/index.js", "// entry");
    createFile(tmp.path() + "/config.schema.json");

    ServiceDirectory dir(tmp.path());
    QString err;
    EXPECT_TRUE(dir.validate(err));
}

TEST_F(ServiceDirectoryTest, MissingManifest) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/index.js");
    createFile(tmp.path() + "/config.schema.json");

    ServiceDirectory dir(tmp.path());
    QString err;
    EXPECT_FALSE(dir.validate(err));
    EXPECT_TRUE(err.contains("manifest.json"));
}

TEST_F(ServiceDirectoryTest, MissingIndexJs) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/manifest.json");
    createFile(tmp.path() + "/config.schema.json");

    ServiceDirectory dir(tmp.path());
    QString err;
    EXPECT_FALSE(dir.validate(err));
    EXPECT_TRUE(err.contains("index.js"));
}

TEST_F(ServiceDirectoryTest, MissingConfigSchema) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/manifest.json");
    createFile(tmp.path() + "/index.js");

    ServiceDirectory dir(tmp.path());
    QString err;
    EXPECT_FALSE(dir.validate(err));
    EXPECT_TRUE(err.contains("config.schema.json"));
}

TEST_F(ServiceDirectoryTest, PathConcatenation) {
    ServiceDirectory dir("/some/path/my_service");
    EXPECT_TRUE(dir.manifestPath().endsWith("manifest.json"));
    EXPECT_TRUE(dir.entryPath().endsWith("index.js"));
    EXPECT_TRUE(dir.configSchemaPath().endsWith("config.schema.json"));
}

TEST_F(ServiceDirectoryTest, NonexistentDirectory) {
    ServiceDirectory dir("/nonexistent/path");
    QString err;
    EXPECT_FALSE(dir.validate(err));
}
