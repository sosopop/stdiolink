# 里程碑 19：Host 注册中心

## 1. 目标

建立完整的 Host 注册闭环，实现"即插即用"。

## 2. 对应需求

- **需求3**: Host 侧注册与索引体系 (Registry & Discovery)

## 3. DriverRegistry 设计

```cpp
class DriverRegistry {
public:
    static DriverRegistry& instance();

    void registerDriver(const QString& id, const DriverConfig& config);
    void unregisterDriver(const QString& id);

    QStringList listDrivers() const;
    DriverConfig getConfig(const QString& id) const;

    bool healthCheck(const QString& id);
    void healthCheckAll();
    void scanDirectory(const QString& path); // 发现机制
    void clear(); // 测试与重置使用
};
```

## 4. DriverConfig 结构

```cpp
struct DriverConfig {
    QString id;
    QString program;
    QStringList args;
    std::shared_ptr<meta::DriverMeta> meta;
    QString metaHash;
};
```

## 4.1 发现机制（补充）

推荐实现：
- 目录扫描：查找 `driver.meta.json` 与可执行文件
- 静态注册：配置文件注册
- 运行时发现：启动 Driver 调用 `meta.describe`

优先级：
1. 若存在 `driver.meta.json`，直接读取
2. 否则启动 Driver 调 `meta.describe`

说明：
- `driver.meta.json` 格式必须与 `--export-meta` 输出**完全一致**

缓存刷新：
- 若 `metaHash` 变化，自动刷新缓存
- 若 Driver 版本变更，强制刷新

## 5. 验收标准

1. 支持多 Driver 注册/注销
2. 统一查询接口
3. 健康检查机制
4. 元数据缓存集成
5. 支持目录扫描与自动发现

## 6. 单元测试用例

### 6.1 测试文件：tests/test_driver_registry.cpp

```cpp
#include <gtest/gtest.h>
#include "stdiolink/host/driver_registry.h"

using namespace stdiolink;

class DriverRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        DriverRegistry::instance().clear();
    }
};

// 测试注册 Driver
TEST_F(DriverRegistryTest, RegisterDriver) {
    DriverConfig config;
    config.id = "test.driver";
    config.program = "/path/to/driver";

    DriverRegistry::instance().registerDriver(config.id, config);

    auto list = DriverRegistry::instance().listDrivers();
    EXPECT_TRUE(list.contains("test.driver"));
}

// 测试注销 Driver
TEST_F(DriverRegistryTest, UnregisterDriver) {
    DriverConfig config;
    config.id = "test.driver";

    DriverRegistry::instance().registerDriver(config.id, config);
    DriverRegistry::instance().unregisterDriver(config.id);

    auto list = DriverRegistry::instance().listDrivers();
    EXPECT_FALSE(list.contains("test.driver"));
}

// 测试获取配置
TEST_F(DriverRegistryTest, GetConfig) {
    DriverConfig config;
    config.id = "test.driver";
    config.program = "/path/to/driver";

    DriverRegistry::instance().registerDriver(config.id, config);

    auto retrieved = DriverRegistry::instance().getConfig("test.driver");
    EXPECT_EQ(retrieved.program, "/path/to/driver");
}
```

### 6.2 健康检查测试

```cpp
// 测试单个 Driver 健康检查
TEST_F(DriverRegistryTest, HealthCheckSingle) {
    // 需要实际 Driver 进程
    // 或使用 Mock
}

// 测试批量健康检查
TEST_F(DriverRegistryTest, HealthCheckAll) {
    DriverConfig c1, c2;
    c1.id = "driver1";
    c2.id = "driver2";

    DriverRegistry::instance().registerDriver(c1.id, c1);
    DriverRegistry::instance().registerDriver(c2.id, c2);

    // 验证批量检查不抛异常
    EXPECT_NO_THROW(DriverRegistry::instance().healthCheckAll());
}
```

### 6.3 目录扫描测试

```cpp
// 测试目录扫描发现
TEST_F(DriverRegistryTest, ScanDirectory) {
    // 创建临时目录结构
    QTemporaryDir tempDir;
    QString driverDir = tempDir.path() + "/test_driver";
    QDir().mkpath(driverDir);

    // 创建 driver.meta.json
    QFile metaFile(driverDir + "/driver.meta.json");
    metaFile.open(QIODevice::WriteOnly);
    metaFile.write(R"({"schemaVersion":"1.0","info":{"id":"test","name":"Test","version":"1.0.0"}})");
    metaFile.close();

    DriverRegistry::instance().scanDirectory(tempDir.path());

    auto list = DriverRegistry::instance().listDrivers();
    EXPECT_TRUE(list.contains("test"));
}

// 测试元数据哈希变化检测
TEST_F(DriverRegistryTest, MetaHashChange) {
    DriverConfig config;
    config.id = "test";
    config.metaHash = "hash1";

    DriverRegistry::instance().registerDriver(config.id, config);

    // 模拟哈希变化
    config.metaHash = "hash2";
    // 验证缓存刷新逻辑
}
```

## 7. 依赖关系

- **前置**: M18 (版本协商)
- **后续**: 无（最终里程碑）
