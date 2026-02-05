#include <gtest/gtest.h>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include "stdiolink/host/driver_registry.h"

using namespace stdiolink;

class DriverRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        DriverRegistry::instance().clear();
    }
};

// ============================================
// 注册/注销测试
// ============================================

TEST_F(DriverRegistryTest, RegisterDriver) {
    DriverConfig config;
    config.id = "test.driver";
    config.program = "/path/to/driver";

    DriverRegistry::instance().registerDriver(config.id, config);

    auto list = DriverRegistry::instance().listDrivers();
    EXPECT_TRUE(list.contains("test.driver"));
}

TEST_F(DriverRegistryTest, UnregisterDriver) {
    DriverConfig config;
    config.id = "test.driver";

    DriverRegistry::instance().registerDriver(config.id, config);
    DriverRegistry::instance().unregisterDriver(config.id);

    auto list = DriverRegistry::instance().listDrivers();
    EXPECT_FALSE(list.contains("test.driver"));
}

TEST_F(DriverRegistryTest, HasDriver) {
    DriverConfig config;
    config.id = "test.driver";

    EXPECT_FALSE(DriverRegistry::instance().hasDriver("test.driver"));

    DriverRegistry::instance().registerDriver(config.id, config);
    EXPECT_TRUE(DriverRegistry::instance().hasDriver("test.driver"));
}

// ============================================
// 获取配置测试
// ============================================

TEST_F(DriverRegistryTest, GetConfig) {
    DriverConfig config;
    config.id = "test.driver";
    config.program = "/path/to/driver";
    config.args = QStringList{"--mode=stdio"};

    DriverRegistry::instance().registerDriver(config.id, config);

    auto retrieved = DriverRegistry::instance().getConfig("test.driver");
    EXPECT_EQ(retrieved.program, "/path/to/driver");
    EXPECT_EQ(retrieved.args.size(), 1);
}

TEST_F(DriverRegistryTest, GetConfigNonExistent) {
    auto config = DriverRegistry::instance().getConfig("nonexistent");
    EXPECT_TRUE(config.id.isEmpty());
}

// ============================================
// 健康检查测试
// ============================================

TEST_F(DriverRegistryTest, HealthCheckNonExistent) {
    EXPECT_FALSE(DriverRegistry::instance().healthCheck("nonexistent"));
}

TEST_F(DriverRegistryTest, HealthCheckNoProgram) {
    DriverConfig config;
    config.id = "test.driver";
    // program 为空

    DriverRegistry::instance().registerDriver(config.id, config);
    EXPECT_FALSE(DriverRegistry::instance().healthCheck("test.driver"));
}

TEST_F(DriverRegistryTest, HealthCheckAll) {
    DriverConfig c1, c2;
    c1.id = "driver1";
    c2.id = "driver2";

    DriverRegistry::instance().registerDriver(c1.id, c1);
    DriverRegistry::instance().registerDriver(c2.id, c2);

    EXPECT_NO_THROW(DriverRegistry::instance().healthCheckAll());
}

// ============================================
// 目录扫描测试
// ============================================

TEST_F(DriverRegistryTest, ScanDirectory) {
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

    DriverRegistry::instance().scanDirectory(tempDir.path());

    auto list = DriverRegistry::instance().listDrivers();
    EXPECT_TRUE(list.contains("test"));
}

TEST_F(DriverRegistryTest, ScanDirectoryWithMeta) {
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

    DriverRegistry::instance().scanDirectory(tempDir.path());

    auto config = DriverRegistry::instance().getConfig("scanner");
    EXPECT_TRUE(config.meta != nullptr);
    EXPECT_EQ(config.meta->info.name, "Scanner");
}
