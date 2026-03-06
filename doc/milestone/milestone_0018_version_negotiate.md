# 里程碑 18：元数据版本协商

## 1. 目标

引入运行时与编译时的 Meta 质量保证。

## 2. 对应需求

- **需求7**: 元数据校验与版本协商 (Schema Validation & Versioning)

## 3. 版本协商机制

```cpp
struct DriverMeta {
    QString schemaVersion = "1.0";  // 元数据版本
};
```

### 3.1 Meta Schema 校验（补充）

新增校验器：对元数据结构进行完整性检查，失败即拒绝注册。

```cpp
class MetaSchemaValidator {
public:
    static bool validate(const meta::DriverMeta& meta, QString* error);
};
```

校验要点：
- `schemaVersion` 格式与范围
- `driver/info` 必填字段
- `commands` 命令名唯一
- `params` 与 `request.schema` 一致性
- `events` 与 event payload 结构一致性

## 4. Host 端版本检查

```cpp
class MetaVersionChecker {
public:
    static bool isCompatible(const QString& hostVersion,
                             const QString& driverVersion);
    static QStringList getSupportedVersions();
};
```

### 4.1 降级与错误策略

- 不兼容时 Host 返回 409 并提示升级/降级
- 若 `schemaVersion` 过高，Host 可尝试读取兼容子集（只读 `info/commands/config`）

## 5. 单元测试用例

### 5.1 测试文件：tests/test_version_negotiate.cpp

```cpp
#include <gtest/gtest.h>
#include "stdiolink/protocol/meta_schema_validator.h"
#include "stdiolink/host/meta_version_checker.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class VersionNegotiateTest : public ::testing::Test {};

// 测试 Schema 版本格式验证
TEST_F(VersionNegotiateTest, SchemaVersionFormat) {
    DriverMeta meta;
    meta.schemaVersion = "1.0";
    meta.info.id = "test";

    QString error;
    EXPECT_TRUE(MetaSchemaValidator::validate(meta, &error));

    meta.schemaVersion = "invalid";
    EXPECT_FALSE(MetaSchemaValidator::validate(meta, &error));
}

// 测试必填字段验证
TEST_F(VersionNegotiateTest, RequiredFieldsValidation) {
    DriverMeta meta;
    meta.schemaVersion = "1.0";
    // 缺少 info.id

    QString error;
    EXPECT_FALSE(MetaSchemaValidator::validate(meta, &error));
    EXPECT_TRUE(error.contains("id"));
}

// 测试命令名唯一性
TEST_F(VersionNegotiateTest, CommandNameUniqueness) {
    DriverMeta meta;
    meta.schemaVersion = "1.0";
    meta.info.id = "test";

    CommandMeta cmd1, cmd2;
    cmd1.name = "scan";
    cmd2.name = "scan";  // 重复
    meta.commands = {cmd1, cmd2};

    QString error;
    EXPECT_FALSE(MetaSchemaValidator::validate(meta, &error));
}
```

### 5.2 版本兼容性测试

```cpp
// 测试版本兼容性检查
TEST_F(VersionNegotiateTest, VersionCompatibility) {
    EXPECT_TRUE(MetaVersionChecker::isCompatible("1.0", "1.0"));
    EXPECT_TRUE(MetaVersionChecker::isCompatible("1.1", "1.0"));
    EXPECT_FALSE(MetaVersionChecker::isCompatible("1.0", "2.0"));
}

// 测试支持的版本列表
TEST_F(VersionNegotiateTest, SupportedVersions) {
    auto versions = MetaVersionChecker::getSupportedVersions();
    EXPECT_FALSE(versions.isEmpty());
    EXPECT_TRUE(versions.contains("1.0"));
}

// 测试降级策略
TEST_F(VersionNegotiateTest, DowngradeStrategy) {
    DriverMeta meta;
    meta.schemaVersion = "2.0";  // 高于 Host 支持

    // Host 应能读取基础子集（info/commands/config）
    QStringList supported = MetaVersionChecker::getSupportedVersions();
    EXPECT_FALSE(supported.contains("2.0"));
    // 期望：降级读取不崩溃（实现可返回 bool 或降级后的 meta）
}
```

## 6. 依赖关系

- **前置**: M17 (配置注入闭环)
- **后续**: M19 (Host注册中心)
