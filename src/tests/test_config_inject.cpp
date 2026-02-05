#include <gtest/gtest.h>
#include <QDir>
#include <QFile>
#include "stdiolink/host/config_injector.h"

using namespace stdiolink;
using namespace stdiolink::meta;

// ============================================
// 环境变量注入测试
// ============================================

TEST(ConfigInject, EnvInjection) {
    ConfigApply apply;
    apply.method = "env";
    apply.envPrefix = "DRIVER_";

    QJsonObject config{{"timeout", 5000}, {"debug", true}};

    auto envVars = ConfigInjector::toEnvVars(config, apply);
    EXPECT_EQ(envVars["DRIVER_TIMEOUT"], "5000");
    EXPECT_EQ(envVars["DRIVER_DEBUG"], "true");
}

TEST(ConfigInject, EnvInjectionString) {
    ConfigApply apply;
    apply.envPrefix = "APP_";

    QJsonObject config{{"name", "test"}, {"mode", "fast"}};

    auto envVars = ConfigInjector::toEnvVars(config, apply);
    EXPECT_EQ(envVars["APP_NAME"], "test");
    EXPECT_EQ(envVars["APP_MODE"], "fast");
}

TEST(ConfigInject, EnvInjectionNoPrefix) {
    ConfigApply apply;
    apply.envPrefix = "";

    QJsonObject config{{"value", 42}};

    auto envVars = ConfigInjector::toEnvVars(config, apply);
    EXPECT_EQ(envVars["VALUE"], "42");
}

// ============================================
// 启动参数注入测试
// ============================================

TEST(ConfigInject, ArgsInjection) {
    ConfigApply apply;
    apply.method = "args";

    QJsonObject config{{"timeout", 5000}};

    QStringList args = ConfigInjector::toArgs(config, apply);
    EXPECT_TRUE(args.contains("--timeout=5000"));
}

TEST(ConfigInject, ArgsInjectionMultiple) {
    ConfigApply apply;

    QJsonObject config{{"fps", 30}, {"debug", true}, {"name", "test"}};

    QStringList args = ConfigInjector::toArgs(config, apply);
    EXPECT_EQ(args.size(), 3);
    EXPECT_TRUE(args.contains("--fps=30"));
    EXPECT_TRUE(args.contains("--debug=true"));
    EXPECT_TRUE(args.contains("--name=test"));
}

TEST(ConfigInject, ArgsInjectionEmpty) {
    ConfigApply apply;
    QJsonObject config;

    QStringList args = ConfigInjector::toArgs(config, apply);
    EXPECT_TRUE(args.isEmpty());
}

// ============================================
// 配置文件注入测试
// ============================================

TEST(ConfigInject, FileInjection) {
    QJsonObject config{{"timeout", 5000}, {"debug", true}};
    QString path = QDir::temp().filePath("test_config_inject.json");

    EXPECT_TRUE(ConfigInjector::toFile(config, path));
    EXPECT_TRUE(QFile::exists(path));

    // 验证文件内容
    QJsonObject loaded;
    EXPECT_TRUE(ConfigInjector::fromFile(path, loaded));
    EXPECT_EQ(loaded["timeout"].toInt(), 5000);
    EXPECT_EQ(loaded["debug"].toBool(), true);

    QFile::remove(path);
}

TEST(ConfigInject, FileInjectionRoundTrip) {
    QJsonObject config{
        {"name", "test"},
        {"count", 42},
        {"enabled", false},
        {"ratio", 3.14}
    };
    QString path = QDir::temp().filePath("test_roundtrip.json");

    EXPECT_TRUE(ConfigInjector::toFile(config, path));

    QJsonObject loaded;
    EXPECT_TRUE(ConfigInjector::fromFile(path, loaded));

    EXPECT_EQ(loaded["name"].toString(), "test");
    EXPECT_EQ(loaded["count"].toInt(), 42);
    EXPECT_EQ(loaded["enabled"].toBool(), false);
    EXPECT_DOUBLE_EQ(loaded["ratio"].toDouble(), 3.14);

    QFile::remove(path);
}

TEST(ConfigInject, FileReadNonExistent) {
    QJsonObject config;
    EXPECT_FALSE(ConfigInjector::fromFile("/nonexistent/path.json", config));
}
