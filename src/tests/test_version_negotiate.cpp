#include <gtest/gtest.h>
#include "stdiolink/host/meta_version_checker.h"
#include "stdiolink/protocol/meta_schema_validator.h"
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink;
using namespace stdiolink::meta;

// ============================================
// Schema 版本格式验证测试
// ============================================

TEST(VersionNegotiate, SchemaVersionFormatValid) {
    DriverMeta meta;
    meta.schemaVersion = "1.0";
    meta.info.id = "test";
    meta.info.name = "Test Driver";

    QString error;
    EXPECT_TRUE(MetaSchemaValidator::validate(meta, &error));
}

TEST(VersionNegotiate, SchemaVersionFormatInvalid) {
    DriverMeta meta;
    meta.schemaVersion = "invalid";
    meta.info.id = "test";
    meta.info.name = "Test Driver";

    QString error;
    EXPECT_FALSE(MetaSchemaValidator::validate(meta, &error));
    EXPECT_TRUE(error.contains("schemaVersion"));
}

TEST(VersionNegotiate, SchemaVersionFormatVariants) {
    DriverMeta meta;
    meta.info.id = "test";
    meta.info.name = "Test Driver";

    QString error;

    meta.schemaVersion = "2.0";
    EXPECT_TRUE(MetaSchemaValidator::validate(meta, &error));

    meta.schemaVersion = "1.10";
    EXPECT_TRUE(MetaSchemaValidator::validate(meta, &error));

    meta.schemaVersion = "1";
    EXPECT_FALSE(MetaSchemaValidator::validate(meta, &error));

    meta.schemaVersion = "1.0.0";
    EXPECT_FALSE(MetaSchemaValidator::validate(meta, &error));
}

// ============================================
// 必填字段验证测试
// ============================================

TEST(VersionNegotiate, RequiredFieldId) {
    DriverMeta meta;
    meta.schemaVersion = "1.0";
    meta.info.name = "Test Driver";
    // 缺少 info.id

    QString error;
    EXPECT_FALSE(MetaSchemaValidator::validate(meta, &error));
    EXPECT_TRUE(error.contains("id"));
}

TEST(VersionNegotiate, RequiredFieldName) {
    DriverMeta meta;
    meta.schemaVersion = "1.0";
    meta.info.id = "test";
    // 缺少 info.name

    QString error;
    EXPECT_FALSE(MetaSchemaValidator::validate(meta, &error));
    EXPECT_TRUE(error.contains("name"));
}

// ============================================
// 命令名唯一性测试
// ============================================

TEST(VersionNegotiate, CommandNameUniqueness) {
    DriverMeta meta;
    meta.schemaVersion = "1.0";
    meta.info.id = "test";
    meta.info.name = "Test Driver";

    CommandMeta cmd1, cmd2;
    cmd1.name = "scan";
    cmd2.name = "scan";  // 重复
    meta.commands = {cmd1, cmd2};

    QString error;
    EXPECT_FALSE(MetaSchemaValidator::validate(meta, &error));
    EXPECT_TRUE(error.contains("Duplicate"));
}

TEST(VersionNegotiate, CommandNameEmpty) {
    DriverMeta meta;
    meta.schemaVersion = "1.0";
    meta.info.id = "test";
    meta.info.name = "Test Driver";

    CommandMeta cmd;
    cmd.name = "";
    meta.commands = {cmd};

    QString error;
    EXPECT_FALSE(MetaSchemaValidator::validate(meta, &error));
}

// ============================================
// 版本兼容性测试
// ============================================

TEST(VersionNegotiate, VersionCompatibilitySame) {
    EXPECT_TRUE(MetaVersionChecker::isCompatible("1.0", "1.0"));
}

TEST(VersionNegotiate, VersionCompatibilityHigherHost) {
    EXPECT_TRUE(MetaVersionChecker::isCompatible("1.1", "1.0"));
    EXPECT_TRUE(MetaVersionChecker::isCompatible("1.5", "1.0"));
}

TEST(VersionNegotiate, VersionCompatibilityLowerHost) {
    EXPECT_FALSE(MetaVersionChecker::isCompatible("1.0", "1.1"));
}

TEST(VersionNegotiate, VersionCompatibilityMajorMismatch) {
    EXPECT_FALSE(MetaVersionChecker::isCompatible("1.0", "2.0"));
    EXPECT_FALSE(MetaVersionChecker::isCompatible("2.0", "1.0"));
}

TEST(VersionNegotiate, SupportedVersions) {
    auto versions = MetaVersionChecker::getSupportedVersions();
    EXPECT_FALSE(versions.isEmpty());
    EXPECT_TRUE(versions.contains("1.0"));
}

TEST(VersionNegotiate, ParseVersion) {
    int major = 0, minor = 0;

    EXPECT_TRUE(MetaVersionChecker::parseVersion("1.0", major, minor));
    EXPECT_EQ(major, 1);
    EXPECT_EQ(minor, 0);

    EXPECT_TRUE(MetaVersionChecker::parseVersion("2.5", major, minor));
    EXPECT_EQ(major, 2);
    EXPECT_EQ(minor, 5);

    EXPECT_FALSE(MetaVersionChecker::parseVersion("invalid", major, minor));
}
