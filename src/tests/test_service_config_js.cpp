#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <quickjs.h>
#include "bindings/js_config.h"
#include "bindings/js_stdiolink_module.h"
#include "engine/console_bridge.h"
#include "engine/js_engine.h"

using namespace stdiolink_service;

class ServiceConfigJsTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
        ASSERT_NE(m_engine->context(), nullptr);
        ConsoleBridge::install(m_engine->context());
        JsConfigBinding::attachRuntime(m_engine->runtime());
        m_engine->registerModule("stdiolink", jsInitStdiolinkModule);
    }

    void TearDown() override {
        JsConfigBinding::reset(m_engine->context());
        m_engine.reset();
    }

    int evalScript(const QString& name, const QString& content) {
        const QString path = m_tmpDir.path() + "/" + name;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return -1;
        f.write(content.toUtf8());
        f.close();
        return m_engine->evalFile(path);
    }

    QTemporaryDir m_tmpDir;
    std::unique_ptr<JsEngine> m_engine;
};

TEST_F(ServiceConfigJsTest, GetConfigReturnsInjectedConfig) {
    JsConfigBinding::setMergedConfig(
        m_engine->context(),
        QJsonObject{{"port", 8080}, {"name", "test"}, {"debug", false}});

    int ret = evalScript("get_config.js",
        "import { getConfig } from 'stdiolink';\n"
        "const cfg = getConfig();\n"
        "if (cfg.port !== 8080) throw new Error('port mismatch');\n"
        "if (cfg.name !== 'test') throw new Error('name mismatch');\n"
        "if (cfg.debug !== false) throw new Error('debug mismatch');\n"
    );
    EXPECT_EQ(ret, 0);
}

TEST_F(ServiceConfigJsTest, ConfigIsReadOnly) {
    JsConfigBinding::setMergedConfig(
        m_engine->context(),
        QJsonObject{{"port", 3000}});

    int ret = evalScript("readonly.js",
        "import { getConfig } from 'stdiolink';\n"
        "const cfg = getConfig();\n"
        "try {\n"
        "    cfg.port = 9999;\n"
        "    throw new Error('should not reach');\n"
        "} catch (e) {\n"
        "    if (e.message === 'should not reach') throw e;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
}

TEST_F(ServiceConfigJsTest, DefineConfigNotExported) {
    JsConfigBinding::setMergedConfig(m_engine->context(), QJsonObject{});

    int ret = evalScript("no_define.js",
        "import { defineConfig } from 'stdiolink';\n"
    );
    EXPECT_NE(ret, 0);
}

TEST_F(ServiceConfigJsTest, GetConfigWithNoInjectionReturnsEmpty) {
    int ret = evalScript("get_empty.js",
        "import { getConfig } from 'stdiolink';\n"
        "const cfg = getConfig();\n"
        "if (Object.keys(cfg).length !== 0) throw new Error('expected empty');\n"
    );
    EXPECT_EQ(ret, 0);
}

TEST_F(ServiceConfigJsTest, GetConfigReturnsNestedValues) {
    JsConfigBinding::setMergedConfig(
        m_engine->context(),
        QJsonObject{{"port", 9090}, {"name", "fromCli"}});

    int ret = evalScript("nested_values.js",
        "import { getConfig } from 'stdiolink';\n"
        "const cfg = getConfig();\n"
        "if (cfg.port !== 9090) throw new Error('port mismatch');\n"
        "if (cfg.name !== 'fromCli') throw new Error('name mismatch');\n"
    );
    EXPECT_EQ(ret, 0);
}

TEST_F(ServiceConfigJsTest, SetMergedConfigOverwritesPrevious) {
    JsConfigBinding::setMergedConfig(
        m_engine->context(),
        QJsonObject{{"port", 99999}});
    JsConfigBinding::setMergedConfig(
        m_engine->context(),
        QJsonObject{{"port", 3000}});

    int ret = evalScript("overwrite.js",
        "import { getConfig } from 'stdiolink';\n"
        "const cfg = getConfig();\n"
        "if (cfg.port !== 3000) throw new Error('expected overwritten value');\n"
    );
    EXPECT_EQ(ret, 0);
}

TEST_F(ServiceConfigJsTest, GetConfigReturnsBoolValues) {
    JsConfigBinding::setMergedConfig(
        m_engine->context(),
        QJsonObject{{"debug", true}, {"verbose", false}});

    int ret = evalScript("bool_values.js",
        "import { getConfig } from 'stdiolink';\n"
        "const cfg = getConfig();\n"
        "if (cfg.debug !== true) throw new Error('debug mismatch');\n"
        "if (cfg.verbose !== false) throw new Error('verbose mismatch');\n"
    );
    EXPECT_EQ(ret, 0);
}

TEST_F(ServiceConfigJsTest, ResetClearsConfig) {
    JsConfigBinding::setMergedConfig(
        m_engine->context(),
        QJsonObject{{"port", 8080}});
    JsConfigBinding::reset(m_engine->context());

    int ret = evalScript("after_reset.js",
        "import { getConfig } from 'stdiolink';\n"
        "const cfg = getConfig();\n"
        "if (Object.keys(cfg).length !== 0) throw new Error('expected empty after reset');\n"
    );
    EXPECT_EQ(ret, 0);
}

TEST_F(ServiceConfigJsTest, GetConfigMultipleCallsReturnSameData) {
    JsConfigBinding::setMergedConfig(
        m_engine->context(),
        QJsonObject{{"port", 8080}});

    int ret = evalScript("multi_call.js",
        "import { getConfig } from 'stdiolink';\n"
        "const a = getConfig();\n"
        "const b = getConfig();\n"
        "if (a.port !== b.port) throw new Error('inconsistent');\n"
    );
    EXPECT_EQ(ret, 0);
}

