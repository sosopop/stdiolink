#include <gtest/gtest.h>
#include "stdiolink/console/system_options.h"
#include "stdiolink/driver/help_generator.h"

using namespace stdiolink;

// ============================================
// SystemOptionRegistry 测试
// ============================================

TEST(SystemOptionRegistry, ListReturnsAllOptions) {
    auto options = SystemOptionRegistry::list();
    EXPECT_GE(options.size(), 7);

    // 验证必须包含的选项
    QStringList names;
    for (const auto& opt : options) {
        names << opt.longName;
    }
    EXPECT_TRUE(names.contains("help"));
    EXPECT_TRUE(names.contains("version"));
    EXPECT_TRUE(names.contains("mode"));
    EXPECT_TRUE(names.contains("profile"));
    EXPECT_TRUE(names.contains("cmd"));
    EXPECT_TRUE(names.contains("export-meta"));
    EXPECT_TRUE(names.contains("export-doc"));
}

TEST(SystemOptionRegistry, FindLongHelp) {
    auto opt = SystemOptionRegistry::findLong("help");
    ASSERT_NE(opt, nullptr);
    EXPECT_EQ(opt->longName, "help");
    EXPECT_EQ(opt->shortName, "h");
    EXPECT_FALSE(opt->requiresValue);
}

TEST(SystemOptionRegistry, FindLongMode) {
    auto opt = SystemOptionRegistry::findLong("mode");
    ASSERT_NE(opt, nullptr);
    EXPECT_EQ(opt->longName, "mode");
    EXPECT_EQ(opt->shortName, "m");
    EXPECT_TRUE(opt->requiresValue);
    EXPECT_TRUE(opt->choices.contains("stdio"));
    EXPECT_TRUE(opt->choices.contains("console"));
}

TEST(SystemOptionRegistry, FindLongProfile) {
    auto opt = SystemOptionRegistry::findLong("profile");
    ASSERT_NE(opt, nullptr);
    EXPECT_EQ(opt->longName, "profile");
    EXPECT_TRUE(opt->choices.contains("oneshot"));
    EXPECT_TRUE(opt->choices.contains("keepalive"));
}

TEST(SystemOptionRegistry, FindLongExportDoc) {
    auto opt = SystemOptionRegistry::findLong("export-doc");
    ASSERT_NE(opt, nullptr);
    EXPECT_EQ(opt->shortName, "D");
    EXPECT_TRUE(opt->choices.contains("markdown"));
    EXPECT_TRUE(opt->choices.contains("openapi"));
    EXPECT_TRUE(opt->choices.contains("html"));
    EXPECT_TRUE(opt->choices.contains("ts"));
    EXPECT_TRUE(opt->choices.contains("typescript"));
    EXPECT_TRUE(opt->choices.contains("dts"));
}

TEST(SystemOptionRegistry, FindLongNonExistent) {
    auto opt = SystemOptionRegistry::findLong("nonexistent");
    EXPECT_EQ(opt, nullptr);
}

TEST(SystemOptionRegistry, FindShortH) {
    auto opt = SystemOptionRegistry::findShort("h");
    ASSERT_NE(opt, nullptr);
    EXPECT_EQ(opt->longName, "help");
}

TEST(SystemOptionRegistry, FindShortV) {
    auto opt = SystemOptionRegistry::findShort("v");
    ASSERT_NE(opt, nullptr);
    EXPECT_EQ(opt->longName, "version");
}

TEST(SystemOptionRegistry, FindShortM) {
    auto opt = SystemOptionRegistry::findShort("m");
    ASSERT_NE(opt, nullptr);
    EXPECT_EQ(opt->longName, "mode");
}

TEST(SystemOptionRegistry, FindShortC) {
    auto opt = SystemOptionRegistry::findShort("c");
    ASSERT_NE(opt, nullptr);
    EXPECT_EQ(opt->longName, "cmd");
}

TEST(SystemOptionRegistry, FindShortE) {
    auto opt = SystemOptionRegistry::findShort("E");
    ASSERT_NE(opt, nullptr);
    EXPECT_EQ(opt->longName, "export-meta");
}

TEST(SystemOptionRegistry, FindShortD) {
    auto opt = SystemOptionRegistry::findShort("D");
    ASSERT_NE(opt, nullptr);
    EXPECT_EQ(opt->longName, "export-doc");
}

TEST(SystemOptionRegistry, FindShortNonExistent) {
    auto opt = SystemOptionRegistry::findShort("x");
    EXPECT_EQ(opt, nullptr);
}

TEST(SystemOptionRegistry, IsFrameworkArg) {
    EXPECT_TRUE(SystemOptionRegistry::isFrameworkArg("help"));
    EXPECT_TRUE(SystemOptionRegistry::isFrameworkArg("version"));
    EXPECT_TRUE(SystemOptionRegistry::isFrameworkArg("mode"));
    EXPECT_TRUE(SystemOptionRegistry::isFrameworkArg("profile"));
    EXPECT_TRUE(SystemOptionRegistry::isFrameworkArg("cmd"));
    EXPECT_TRUE(SystemOptionRegistry::isFrameworkArg("export-meta"));
    EXPECT_TRUE(SystemOptionRegistry::isFrameworkArg("export-doc"));
    EXPECT_FALSE(SystemOptionRegistry::isFrameworkArg("fps"));
    EXPECT_FALSE(SystemOptionRegistry::isFrameworkArg("unknown"));
}

TEST(SystemOptionRegistry, IsFrameworkShortArg) {
    EXPECT_TRUE(SystemOptionRegistry::isFrameworkShortArg("h"));
    EXPECT_TRUE(SystemOptionRegistry::isFrameworkShortArg("v"));
    EXPECT_TRUE(SystemOptionRegistry::isFrameworkShortArg("m"));
    EXPECT_TRUE(SystemOptionRegistry::isFrameworkShortArg("c"));
    EXPECT_TRUE(SystemOptionRegistry::isFrameworkShortArg("E"));
    EXPECT_TRUE(SystemOptionRegistry::isFrameworkShortArg("D"));
    EXPECT_FALSE(SystemOptionRegistry::isFrameworkShortArg("x"));
    EXPECT_FALSE(SystemOptionRegistry::isFrameworkShortArg("z"));
}

// ============================================
// HelpGenerator 系统选项测试 (M20)
// ============================================

TEST(HelpGeneratorSystemOptions, ContainsAllOptions) {
    QString output = HelpGenerator::generateSystemOptions();

    // 验证包含所有系统选项
    EXPECT_TRUE(output.contains("--help"));
    EXPECT_TRUE(output.contains("--version"));
    EXPECT_TRUE(output.contains("--mode"));
    EXPECT_TRUE(output.contains("--profile"));
    EXPECT_TRUE(output.contains("--cmd"));
    EXPECT_TRUE(output.contains("--export-meta"));
    EXPECT_TRUE(output.contains("--export-doc"));
}

TEST(HelpGeneratorSystemOptions, ContainsShortOptions) {
    QString output = HelpGenerator::generateSystemOptions();

    // 验证包含短参数
    EXPECT_TRUE(output.contains("-h"));
    EXPECT_TRUE(output.contains("-v"));
    EXPECT_TRUE(output.contains("-m"));
    EXPECT_TRUE(output.contains("-c"));
    EXPECT_TRUE(output.contains("-E"));
    EXPECT_TRUE(output.contains("-D"));
}

TEST(HelpGeneratorSystemOptions, ContainsChoices) {
    QString output = HelpGenerator::generateSystemOptions();

    // 验证包含可选值
    EXPECT_TRUE(output.contains("stdio"));
    EXPECT_TRUE(output.contains("console"));
    EXPECT_TRUE(output.contains("oneshot"));
    EXPECT_TRUE(output.contains("keepalive"));
    EXPECT_TRUE(output.contains("markdown"));
    EXPECT_TRUE(output.contains("openapi"));
    EXPECT_TRUE(output.contains("html"));
    EXPECT_TRUE(output.contains("ts"));
}

TEST(HelpGeneratorSystemOptions, ContainsDescriptions) {
    QString output = HelpGenerator::generateSystemOptions();

    // 验证包含描述
    EXPECT_TRUE(output.contains("Show help"));
    EXPECT_TRUE(output.contains("Show version"));
    EXPECT_TRUE(output.contains("Run mode"));
    EXPECT_TRUE(output.contains("Execute command"));
    EXPECT_TRUE(output.contains("Export metadata"));
    EXPECT_TRUE(output.contains("Export documentation"));
}

TEST(HelpGeneratorSystemOptions, GenerateHelpIncludesSystemOptions) {
    meta::DriverMeta meta;
    meta.info.id = "test";
    meta.info.name = "Test Driver";
    meta.info.version = "1.0.0";

    QString output = HelpGenerator::generateHelp(meta);

    // 验证全局帮助包含系统选项
    EXPECT_TRUE(output.contains("Options:"));
    EXPECT_TRUE(output.contains("--help"));
    EXPECT_TRUE(output.contains("--version"));
    EXPECT_TRUE(output.contains("--mode"));
    EXPECT_TRUE(output.contains("--profile"));
    EXPECT_TRUE(output.contains("--export-meta"));
    EXPECT_TRUE(output.contains("--export-doc"));
}
