#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "stdiolink_server/scanner/service_scanner.h"

using namespace stdiolink_server;

namespace {

bool writeTextFile(const QString& path, const QString& content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QTextStream out(&file);
    out << content;
    return file.error() == QFile::NoError;
}

void createService(const QString& root,
                   const QString& dirName,
                   const QString& manifest,
                   const QString& schema,
                   bool withEntry = true,
                   bool withSchema = true) {
    const QString dir = root + "/" + dirName;
    QDir().mkpath(dir);
    ASSERT_TRUE(writeTextFile(dir + "/manifest.json", manifest));
    if (withEntry) {
        ASSERT_TRUE(writeTextFile(dir + "/index.js", "console.log('ok');\n"));
    }
    if (withSchema) {
        ASSERT_TRUE(writeTextFile(dir + "/config.schema.json", schema));
    }
}

} // namespace

TEST(ServiceScannerTest, EmptyDirectory) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString servicesDir = tmp.path() + "/services";
    ASSERT_TRUE(QDir().mkpath(servicesDir));

    ServiceScanner scanner;
    ServiceScanner::ScanStats stats;
    const auto result = scanner.scan(servicesDir, &stats);

    EXPECT_TRUE(result.isEmpty());
    EXPECT_EQ(stats.scannedDirs, 0);
    EXPECT_EQ(stats.loadedServices, 0);
    EXPECT_EQ(stats.failedServices, 0);
}

TEST(ServiceScannerTest, ValidServiceLoadedWithRawSchema) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString servicesDir = tmp.path() + "/services";
    ASSERT_TRUE(QDir().mkpath(servicesDir));

    createService(
        servicesDir,
        "collector",
        R"({"manifestVersion":"1","id":"collector","name":"Collector","version":"1.0.0"})",
        R"({"device":{"type":"object","fields":{"host":{"type":"string","required":true}}}})");

    ServiceScanner scanner;
    ServiceScanner::ScanStats stats;
    const auto result = scanner.scan(servicesDir, &stats);

    ASSERT_EQ(result.size(), 1);
    ASSERT_TRUE(result.contains("collector"));
    const ServiceInfo info = result.value("collector");
    EXPECT_TRUE(info.valid);
    EXPECT_TRUE(info.hasSchema);
    EXPECT_EQ(info.name, "Collector");
    EXPECT_TRUE(info.rawConfigSchema.contains("device"));
    EXPECT_EQ(stats.loadedServices, 1);
    EXPECT_EQ(stats.failedServices, 0);
}

TEST(ServiceScannerTest, InvalidManifestIsSkipped) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString servicesDir = tmp.path() + "/services";
    ASSERT_TRUE(QDir().mkpath(servicesDir));

    createService(
        servicesDir,
        "bad",
        "not-json",
        "{}");

    ServiceScanner scanner;
    ServiceScanner::ScanStats stats;
    const auto result = scanner.scan(servicesDir, &stats);

    EXPECT_TRUE(result.isEmpty());
    EXPECT_EQ(stats.scannedDirs, 1);
    EXPECT_EQ(stats.loadedServices, 0);
    EXPECT_EQ(stats.failedServices, 1);
}

TEST(ServiceScannerTest, MissingSchemaIsSkipped) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString servicesDir = tmp.path() + "/services";
    ASSERT_TRUE(QDir().mkpath(servicesDir));

    createService(
        servicesDir,
        "no-schema",
        R"({"manifestVersion":"1","id":"svc","name":"Svc","version":"1.0.0"})",
        "{}",
        true,
        false);

    ServiceScanner scanner;
    ServiceScanner::ScanStats stats;
    const auto result = scanner.scan(servicesDir, &stats);

    EXPECT_TRUE(result.isEmpty());
    EXPECT_EQ(stats.failedServices, 1);
}

TEST(ServiceScannerTest, DuplicateServiceIdKeepsFirstAndSkipsSecond) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString servicesDir = tmp.path() + "/services";
    ASSERT_TRUE(QDir().mkpath(servicesDir));

    createService(
        servicesDir,
        "svc-a",
        R"({"manifestVersion":"1","id":"dup","name":"SvcA","version":"1.0.0"})",
        R"({"k":{"type":"string"}})");
    createService(
        servicesDir,
        "svc-b",
        R"({"manifestVersion":"1","id":"dup","name":"SvcB","version":"1.0.0"})",
        R"({"k":{"type":"string"}})");

    ServiceScanner scanner;
    ServiceScanner::ScanStats stats;
    const auto result = scanner.scan(servicesDir, &stats);

    ASSERT_EQ(result.size(), 1);
    ASSERT_TRUE(result.contains("dup"));
    EXPECT_EQ(stats.scannedDirs, 2);
    EXPECT_EQ(stats.loadedServices, 1);
    EXPECT_EQ(stats.failedServices, 1);
}
