#include <gtest/gtest.h>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include "stdiolink/host/driver_catalog.h"

using namespace stdiolink;

class DriverCatalogTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_catalog.clear();
    }

    DriverScanner m_scanner;
    DriverCatalog m_catalog;
};

TEST_F(DriverCatalogTest, ReplaceAllAndHasDriver) {
    DriverConfig config;
    config.id = "test.driver";

    QHash<QString, DriverConfig> drivers;
    drivers.insert(config.id, config);
    m_catalog.replaceAll(drivers);
    EXPECT_TRUE(m_catalog.hasDriver("test.driver"));
}

TEST_F(DriverCatalogTest, GetConfig) {
    DriverConfig config;
    config.id = "test.driver";
    config.program = "/path/to/driver";
    config.args = QStringList{"--mode=stdio"};

    QHash<QString, DriverConfig> drivers;
    drivers.insert(config.id, config);
    m_catalog.replaceAll(drivers);

    auto retrieved = m_catalog.getConfig("test.driver");
    EXPECT_EQ(retrieved.program, "/path/to/driver");
    EXPECT_EQ(retrieved.args.size(), 1);
}

TEST_F(DriverCatalogTest, GetConfigNonExistent) {
    auto config = m_catalog.getConfig("nonexistent");
    EXPECT_TRUE(config.id.isEmpty());
}

TEST_F(DriverCatalogTest, HealthCheckNonExistent) {
    EXPECT_FALSE(m_catalog.healthCheck("nonexistent"));
}

TEST_F(DriverCatalogTest, HealthCheckNoProgram) {
    DriverConfig config;
    config.id = "test.driver";

    QHash<QString, DriverConfig> drivers;
    drivers.insert(config.id, config);
    m_catalog.replaceAll(drivers);
    EXPECT_FALSE(m_catalog.healthCheck("test.driver"));
}

TEST_F(DriverCatalogTest, HealthCheckAll) {
    DriverConfig c1, c2;
    c1.id = "driver1";
    c2.id = "driver2";
    QHash<QString, DriverConfig> drivers;
    drivers.insert(c1.id, c1);
    drivers.insert(c2.id, c2);
    m_catalog.replaceAll(drivers);
    EXPECT_NO_THROW(m_catalog.healthCheckAll());
}

TEST_F(DriverCatalogTest, ScanDirectory) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QString driverDir = tempDir.path() + "/test_driver";
    QDir().mkpath(driverDir);

    // 创建 driver.meta.json
    QFile metaFile(driverDir + "/driver.meta.json");
    ASSERT_TRUE(metaFile.open(QIODevice::WriteOnly));
    metaFile.write(R"({
        "schemaVersion": "1.0",
        "info": {"id": "test", "name": "Test Driver", "version": "1.0.0"}
    })");
    metaFile.close();

    auto scanned = m_scanner.scanDirectory(tempDir.path());
    m_catalog.replaceAll(scanned);
    auto list = m_catalog.listDrivers();
    EXPECT_TRUE(list.contains("test"));
}

TEST_F(DriverCatalogTest, ScanDirectoryWithMeta) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QString driverDir = tempDir.path() + "/scanner";
    QDir().mkpath(driverDir);

    QFile metaFile(driverDir + "/driver.meta.json");
    ASSERT_TRUE(metaFile.open(QIODevice::WriteOnly));
    metaFile.write(R"({
        "schemaVersion": "1.0",
        "info": {"id": "scanner", "name": "Scanner", "version": "2.0.0"}
    })");
    metaFile.close();

    auto scanned = m_scanner.scanDirectory(tempDir.path());
    m_catalog.replaceAll(scanned);
    auto config = m_catalog.getConfig("scanner");
    EXPECT_TRUE(config.meta != nullptr);
    EXPECT_EQ(config.meta->info.name, "Scanner");
}

TEST_F(DriverCatalogTest, ScanStats) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QString d1 = tempDir.path() + "/ok_driver";
    QString d2 = tempDir.path() + "/bad_driver";
    QDir().mkpath(d1);
    QDir().mkpath(d2);

    QFile goodMeta(d1 + "/driver.meta.json");
    ASSERT_TRUE(goodMeta.open(QIODevice::WriteOnly));
    goodMeta.write(R"({
        "schemaVersion": "1.0",
        "info": {"id": "ok", "name": "OK Driver", "version": "1.0.0"}
    })");
    goodMeta.close();

    QFile badMeta(d2 + "/driver.meta.json");
    ASSERT_TRUE(badMeta.open(QIODevice::WriteOnly));
    badMeta.write("not-json");
    badMeta.close();

    DriverScanner::ScanStats stats;
    auto scanned = m_scanner.scanDirectory(tempDir.path(), &stats);
    EXPECT_TRUE(scanned.contains("ok"));
    EXPECT_GE(stats.scannedDirectories, 2);
    EXPECT_EQ(stats.loadedDrivers, 1);
    EXPECT_EQ(stats.invalidMetaFiles, 1);
}
