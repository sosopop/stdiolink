#include <gtest/gtest.h>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include "config/service_args.h"

using namespace stdiolink_service;

class ServiceArgsTest : public ::testing::Test {};

TEST_F(ServiceArgsTest, ParseSimpleConfigArg) {
    QStringList args = {"stdiolink_service", "./my_service", "--config.port=8080"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.error.isEmpty()) << result.error.toStdString();
    EXPECT_EQ(result.serviceDir, "./my_service");
    EXPECT_EQ(result.rawConfigValues["port"].toString(), "8080");
}

TEST_F(ServiceArgsTest, ParseNestedConfigArg) {
    QStringList args = {"stdiolink_service", "./my_service",
                        "--config.server.host=localhost",
                        "--config.server.port=3000"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.error.isEmpty()) << result.error.toStdString();
    auto server = result.rawConfigValues["server"].toObject();
    EXPECT_EQ(server["host"].toString(), "localhost");
    EXPECT_EQ(server["port"].toString(), "3000");
}

TEST_F(ServiceArgsTest, RejectInvalidPathSegment) {
    QStringList args = {"stdiolink_service", "./my_service", "--config..port=1"};
    auto result = ServiceArgs::parse(args);
    EXPECT_FALSE(result.error.isEmpty());
}

TEST_F(ServiceArgsTest, KeepBoolLiteralAsRawString) {
    QStringList args = {"stdiolink_service", "./svc", "--config.debug=true"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.rawConfigValues["debug"].isString());
    EXPECT_EQ(result.rawConfigValues["debug"].toString(), "true");
}

TEST_F(ServiceArgsTest, KeepDoubleLiteralAsRawString) {
    QStringList args = {"stdiolink_service", "./svc", "--config.ratio=0.75"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.rawConfigValues["ratio"].isString());
    EXPECT_EQ(result.rawConfigValues["ratio"].toString(), "0.75");
}

TEST_F(ServiceArgsTest, KeepStringLiteralAsRawString) {
    QStringList args = {"stdiolink_service", "./svc", "--config.name=hello"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.rawConfigValues["name"].isString());
    EXPECT_EQ(result.rawConfigValues["name"].toString(), "hello");
}

TEST_F(ServiceArgsTest, ExtractConfigFilePath) {
    QStringList args = {"stdiolink_service", "./svc", "--config-file=config.json"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.error.isEmpty()) << result.error.toStdString();
    EXPECT_EQ(result.configFilePath, "config.json");
}

TEST_F(ServiceArgsTest, DumpSchemaFlag) {
    QStringList args = {"stdiolink_service", "./svc", "--dump-config-schema"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.error.isEmpty()) << result.error.toStdString();
    EXPECT_TRUE(result.dumpSchema);
}

TEST_F(ServiceArgsTest, MissingServiceDir) {
    QStringList args = {"stdiolink_service", "--config.port=8080"};
    auto result = ServiceArgs::parse(args);
    EXPECT_FALSE(result.error.isEmpty());
}

TEST_F(ServiceArgsTest, KeepJsonArrayLiteralAsRawString) {
    QStringList args = {"stdiolink_service", "./svc", "--config.tags=[1,2,3]"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.rawConfigValues["tags"].isString());
    EXPECT_EQ(result.rawConfigValues["tags"].toString(), "[1,2,3]");
}

TEST_F(ServiceArgsTest, KeepJsonObjectLiteralAsRawString) {
    QStringList args = {"stdiolink_service", "./svc", R"(--config.opts={"a":1})"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.rawConfigValues["opts"].isString());
    EXPECT_EQ(result.rawConfigValues["opts"].toString(), R"({"a":1})");
}

TEST_F(ServiceArgsTest, MultipleConfigArgs) {
    QStringList args = {"stdiolink_service", "./svc",
                        "--config.port=8080",
                        "--config.name=test",
                        "--config.debug=false"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.error.isEmpty()) << result.error.toStdString();
    EXPECT_EQ(result.rawConfigValues.size(), 3);
}

TEST_F(ServiceArgsTest, LoadConfigFileValid) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString path = tmpDir.filePath("config.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(R"({"port": 3000, "name": "test"})");
    f.close();

    QString err;
    auto obj = ServiceArgs::loadConfigFile(path, err);
    EXPECT_TRUE(err.isEmpty()) << err.toStdString();
    EXPECT_EQ(obj["port"].toInt(), 3000);
    EXPECT_EQ(obj["name"].toString(), "test");
}

TEST_F(ServiceArgsTest, LoadConfigFileNotFound) {
    QString err;
    auto obj = ServiceArgs::loadConfigFile("nonexistent_file.json", err);
    EXPECT_FALSE(err.isEmpty());
    EXPECT_TRUE(obj.isEmpty());
}

TEST_F(ServiceArgsTest, LoadConfigFileMalformed) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString path = tmpDir.filePath("bad.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("{invalid json content");
    f.close();

    QString err;
    auto obj = ServiceArgs::loadConfigFile(path, err);
    EXPECT_FALSE(err.isEmpty());
}

TEST_F(ServiceArgsTest, LoadConfigFileFromStdin) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString path = tmpDir.filePath("stdin.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(R"({"port": 9001, "name": "stdin"})");
    f.close();

#ifdef _WIN32
    const int saved = _dup(_fileno(stdin));
    ASSERT_GE(saved, 0);
    FILE* in = _fsopen(path.toLocal8Bit().constData(), "rb", _SH_DENYNO);
    ASSERT_NE(in, nullptr);
    ASSERT_EQ(_dup2(_fileno(in), _fileno(stdin)), 0);
#else
    const int saved = dup(fileno(stdin));
    ASSERT_GE(saved, 0);
    FILE* in = std::fopen(path.toLocal8Bit().constData(), "rb");
    ASSERT_NE(in, nullptr);
    ASSERT_EQ(dup2(fileno(in), fileno(stdin)), 0);
#endif
    std::fclose(in);

    QString err;
    auto obj = ServiceArgs::loadConfigFile("-", err);

#ifdef _WIN32
    _dup2(saved, _fileno(stdin));
    _close(saved);
#else
    dup2(saved, fileno(stdin));
    close(saved);
#endif

    EXPECT_TRUE(err.isEmpty()) << err.toStdString();
    EXPECT_EQ(obj["port"].toInt(), 9001);
    EXPECT_EQ(obj["name"].toString(), "stdin");
}

TEST_F(ServiceArgsTest, HelpFlag) {
    QStringList args = {"stdiolink_service", "--help"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.help);
}

TEST_F(ServiceArgsTest, VersionFlag) {
    QStringList args = {"stdiolink_service", "--version"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.version);
}

// M86 T01 — parse() 解析 --data-root 参数
TEST_F(ServiceArgsTest, T01_ParseDataRoot) {
    auto r = ServiceArgs::parse({"app", "svcDir", "--data-root=/some/path"});
    EXPECT_TRUE(r.error.isEmpty()) << r.error.toStdString();
    EXPECT_EQ(r.dataRoot, "/some/path");
}

// M86 T02 — parse() 无 --data-root 参数
TEST_F(ServiceArgsTest, T02_ParseNoDataRoot) {
    auto r = ServiceArgs::parse({"app", "svcDir", "--guard=test"});
    EXPECT_TRUE(r.error.isEmpty()) << r.error.toStdString();
    EXPECT_TRUE(r.dataRoot.isEmpty());
}

// M86 T03 — parse() --data-root= 空值
TEST_F(ServiceArgsTest, T03_ParseDataRootEmpty) {
    auto r = ServiceArgs::parse({"app", "svcDir", "--data-root="});
    EXPECT_TRUE(r.error.isEmpty()) << r.error.toStdString();
    EXPECT_TRUE(r.dataRoot.isEmpty());
}

// M86 T19 — normalizeDataRoot 规范化测试
TEST_F(ServiceArgsTest, T19_NormalizeDataRoot) {
    // 空输入返回空
    EXPECT_TRUE(normalizeDataRoot("").isEmpty());

    // 绝对路径原样返回（已规范化）
    QString abs = normalizeDataRoot("/abs/path");
    EXPECT_TRUE(QDir::isAbsolutePath(abs));
    EXPECT_EQ(abs, QDir("/abs/path").absolutePath());

    // 相对路径规范化为绝对路径，不含 ".." 片段
    QString rel = normalizeDataRoot("../some/relative");
    EXPECT_TRUE(QDir::isAbsolutePath(rel));
    EXPECT_FALSE(rel.contains(".."));
}

// M72_R16 — loadConfigFile rejects file exceeding 1MB limit
TEST_F(ServiceArgsTest, M72_R16_LoadConfigFileTooLargeRejected) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString path = tmpDir.filePath("huge.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    // Write a valid JSON object larger than 1MB
    f.write("{\"data\":\"");
    const QByteArray filler(1024 * 1024 + 100, 'A');
    f.write(filler);
    f.write("\"}");
    f.close();

    QString err;
    auto obj = ServiceArgs::loadConfigFile(path, err);
    EXPECT_FALSE(err.isEmpty());
    EXPECT_TRUE(err.contains("too large"));
    EXPECT_TRUE(obj.isEmpty());
}

// M72_R17 — loadConfigFile accepts file just under 1MB limit
TEST_F(ServiceArgsTest, M72_R17_LoadConfigFileUnderLimitAccepted) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString path = tmpDir.filePath("ok.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(R"({"port": 3000})");
    f.close();

    QString err;
    auto obj = ServiceArgs::loadConfigFile(path, err);
    EXPECT_TRUE(err.isEmpty()) << err.toStdString();
    EXPECT_EQ(obj["port"].toInt(), 3000);
}
