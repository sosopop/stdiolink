#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "stdiolink_server/config/server_args.h"
#include "stdiolink_server/config/server_config.h"

using namespace stdiolink_server;

namespace {

bool writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(content) == content.size();
}

} // namespace

TEST(ServerConfigTest, MissingFileUsesDefaults) {
    QString error;
    const auto cfg = ServerConfig::loadFromFile("/tmp/stdiolink_nonexistent_config.json", error);
    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(cfg.port, 8080);
    EXPECT_EQ(cfg.host, "127.0.0.1");
    EXPECT_EQ(cfg.logLevel, "info");
    EXPECT_TRUE(cfg.serviceProgram.isEmpty());
}

TEST(ServerConfigTest, InvalidJsonReturnsError) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString filePath = dir.path() + "/config.json";
    ASSERT_TRUE(writeFile(filePath, "{bad json"));

    QString error;
    (void)ServerConfig::loadFromFile(filePath, error);
    EXPECT_FALSE(error.isEmpty());
}

TEST(ServerConfigTest, ApplyArgsOverridesOnlyExplicitFlags) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString filePath = dir.path() + "/config.json";
    const QJsonObject obj{{"port", 9001}, {"host", "0.0.0.0"}, {"logLevel", "warn"}};
    ASSERT_TRUE(writeFile(filePath, QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    QString error;
    ServerConfig cfg = ServerConfig::loadFromFile(filePath, error);
    ASSERT_TRUE(error.isEmpty());

    const auto noOverride = ServerArgs::parse({"stdiolink_server", "--data-root=/data"});
    cfg.applyArgs(noOverride);
    EXPECT_EQ(cfg.port, 9001);
    EXPECT_EQ(cfg.host, "0.0.0.0");
    EXPECT_EQ(cfg.logLevel, "warn");

    const auto partialOverride = ServerArgs::parse({
        "stdiolink_server",
        "--port=7777",
        "--log-level=error"
    });
    cfg.applyArgs(partialOverride);
    EXPECT_EQ(cfg.port, 7777);
    EXPECT_EQ(cfg.host, "0.0.0.0");
    EXPECT_EQ(cfg.logLevel, "error");
}

TEST(ServerConfigTest, UnknownFieldRejected) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString filePath = dir.path() + "/config.json";
    const QJsonObject obj{{"port", 8080}, {"unknown", 1}};
    ASSERT_TRUE(writeFile(filePath, QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    QString error;
    (void)ServerConfig::loadFromFile(filePath, error);
    EXPECT_FALSE(error.isEmpty());
}

TEST(ServerConfigTest, InvalidServiceProgramTypeRejected) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString filePath = dir.path() + "/config.json";
    const QJsonObject obj{{"serviceProgram", 123}};
    ASSERT_TRUE(writeFile(filePath, QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    QString error;
    (void)ServerConfig::loadFromFile(filePath, error);
    EXPECT_FALSE(error.isEmpty());
}

TEST(ServerConfigTest, CorsOriginParsedFromConfig) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString filePath = dir.path() + "/config.json";
    const QJsonObject obj{{"corsOrigin", "http://localhost:3000"}};
    ASSERT_TRUE(writeFile(filePath, QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    QString error;
    const auto cfg = ServerConfig::loadFromFile(filePath, error);
    ASSERT_TRUE(error.isEmpty()) << qPrintable(error);
    EXPECT_EQ(cfg.corsOrigin, "http://localhost:3000");
}

TEST(ServerConfigTest, CorsOriginDefaultIsStar) {
    QString error;
    const auto cfg = ServerConfig::loadFromFile("/tmp/stdiolink_nonexistent_config.json", error);
    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(cfg.corsOrigin, "*");
}

// --- logMaxBytes / logMaxFiles ---

TEST(ServerConfigTest, LogMaxBytesValidValue) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString filePath = dir.path() + "/config.json";
    const QJsonObject obj{{"logMaxBytes", 5 * 1024 * 1024}};
    ASSERT_TRUE(writeFile(filePath, QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    QString error;
    const auto cfg = ServerConfig::loadFromFile(filePath, error);
    EXPECT_TRUE(error.isEmpty()) << qPrintable(error);
    EXPECT_EQ(cfg.logMaxBytes, 5 * 1024 * 1024);
}

TEST(ServerConfigTest, LogMaxBytesTooSmallRejected) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString filePath = dir.path() + "/config.json";
    const QJsonObject obj{{"logMaxBytes", 512}};
    ASSERT_TRUE(writeFile(filePath, QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    QString error;
    (void)ServerConfig::loadFromFile(filePath, error);
    EXPECT_FALSE(error.isEmpty());
}

TEST(ServerConfigTest, LogMaxBytesWrongTypeRejected) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString filePath = dir.path() + "/config.json";
    const QJsonObject obj{{"logMaxBytes", "big"}};
    ASSERT_TRUE(writeFile(filePath, QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    QString error;
    (void)ServerConfig::loadFromFile(filePath, error);
    EXPECT_FALSE(error.isEmpty());
}

TEST(ServerConfigTest, LogMaxFilesValidValue) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString filePath = dir.path() + "/config.json";
    const QJsonObject obj{{"logMaxFiles", 5}};
    ASSERT_TRUE(writeFile(filePath, QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    QString error;
    const auto cfg = ServerConfig::loadFromFile(filePath, error);
    EXPECT_TRUE(error.isEmpty()) << qPrintable(error);
    EXPECT_EQ(cfg.logMaxFiles, 5);
}

TEST(ServerConfigTest, LogMaxFilesZeroRejected) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString filePath = dir.path() + "/config.json";
    const QJsonObject obj{{"logMaxFiles", 0}};
    ASSERT_TRUE(writeFile(filePath, QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    QString error;
    (void)ServerConfig::loadFromFile(filePath, error);
    EXPECT_FALSE(error.isEmpty());
}

TEST(ServerConfigTest, LogMaxFilesOver100Rejected) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString filePath = dir.path() + "/config.json";
    const QJsonObject obj{{"logMaxFiles", 200}};
    ASSERT_TRUE(writeFile(filePath, QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    QString error;
    (void)ServerConfig::loadFromFile(filePath, error);
    EXPECT_FALSE(error.isEmpty());
}

TEST(ServerConfigTest, LogMaxFilesWrongTypeRejected) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString filePath = dir.path() + "/config.json";
    const QJsonObject obj{{"logMaxFiles", "three"}};
    ASSERT_TRUE(writeFile(filePath, QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    QString error;
    (void)ServerConfig::loadFromFile(filePath, error);
    EXPECT_FALSE(error.isEmpty());
}

TEST(ServerConfigTest, LogFieldsDefaultValues) {
    QString error;
    const auto cfg = ServerConfig::loadFromFile("/tmp/stdiolink_nonexistent_config.json", error);
    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(cfg.logMaxBytes, 10 * 1024 * 1024);
    EXPECT_EQ(cfg.logMaxFiles, 3);
}
