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

namespace {

QString writeScript(const QTemporaryDir& dir, const QString& name, const QString& content) {
    const QString path = dir.path() + "/" + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream out(&f);
    out << content;
    out.flush();
    return path;
}

} // namespace

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
        m_engine.reset();
    }

    int evalScript(const QString& name, const QString& content) {
        const QString path = writeScript(m_tmpDir, name, content);
        if (path.isEmpty()) return -1;
        return m_engine->evalFile(path);
    }

    QTemporaryDir m_tmpDir;
    std::unique_ptr<JsEngine> m_engine;
};

TEST_F(ServiceConfigJsTest, DefineAndGetConfig) {
    JsConfigBinding::setRawConfig(
        m_engine->context(),
        QJsonObject{{"port", 8080}, {"name", "test"}},
        QJsonObject{},
        false);
    int ret = evalScript("define_get.js",
        "import { defineConfig, getConfig } from 'stdiolink';\n"
        "defineConfig({\n"
        "    port: { type: 'int', required: true },\n"
        "    name: { type: 'string', required: true },\n"
        "    debug: { type: 'bool', default: false }\n"
        "});\n"
        "const cfg = getConfig();\n"
        "if (cfg.port !== 8080) throw new Error('port mismatch');\n"
        "if (cfg.name !== 'test') throw new Error('name mismatch');\n"
        "if (cfg.debug !== false) throw new Error('debug mismatch');\n"
    );
    EXPECT_EQ(ret, 0);
}

TEST_F(ServiceConfigJsTest, ConfigIsReadOnly) {
    JsConfigBinding::setRawConfig(
        m_engine->context(),
        QJsonObject{{"port", 3000}},
        QJsonObject{},
        false);
    int ret = evalScript("readonly.js",
        "import { defineConfig, getConfig } from 'stdiolink';\n"
        "defineConfig({ port: { type: 'int', required: true } });\n"
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

TEST_F(ServiceConfigJsTest, DuplicateDefineConfigThrows) {
    JsConfigBinding::setRawConfig(m_engine->context(), QJsonObject{}, QJsonObject{}, false);
    int ret = evalScript("dup_define.js",
        "import { defineConfig } from 'stdiolink';\n"
        "defineConfig({ a: { type: 'string', default: '' } });\n"
        "try {\n"
        "    defineConfig({ b: { type: 'int', default: 0 } });\n"
        "    throw new Error('should not reach');\n"
        "} catch (e) {\n"
        "    if (e.message === 'should not reach') throw e;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
}

TEST_F(ServiceConfigJsTest, RequiredFieldMissingThrows) {
    JsConfigBinding::setRawConfig(m_engine->context(), QJsonObject{}, QJsonObject{}, false);
    int ret = evalScript("required_missing.js",
        "import { defineConfig } from 'stdiolink';\n"
        "defineConfig({ port: { type: 'int', required: true } });\n"
    );
    EXPECT_NE(ret, 0);
}

TEST_F(ServiceConfigJsTest, TypeMismatchThrows) {
    JsConfigBinding::setRawConfig(
        m_engine->context(),
        QJsonObject{{"port", "not_a_number"}},
        QJsonObject{},
        false);
    int ret = evalScript("type_mismatch.js",
        "import { defineConfig } from 'stdiolink';\n"
        "defineConfig({ port: { type: 'int', required: true } });\n"
    );
    EXPECT_NE(ret, 0);
}

TEST_F(ServiceConfigJsTest, ConstraintViolationThrows) {
    JsConfigBinding::setRawConfig(
        m_engine->context(),
        QJsonObject{{"port", 99999}},
        QJsonObject{},
        false);
    int ret = evalScript("constraint_fail.js",
        "import { defineConfig } from 'stdiolink';\n"
        "defineConfig({\n"
        "    port: { type: 'int', required: true,\n"
        "            constraints: { min: 1, max: 65535 } }\n"
        "});\n"
    );
    EXPECT_NE(ret, 0);
}

TEST_F(ServiceConfigJsTest, CliOverridesFileConfig) {
    JsConfigBinding::setRawConfig(
        m_engine->context(),
        QJsonObject{{"port", 9090}},
        QJsonObject{{"port", 3000}, {"name", "fromFile"}},
        false);
    int ret = evalScript("cli_override.js",
        "import { defineConfig, getConfig } from 'stdiolink';\n"
        "defineConfig({\n"
        "    port: { type: 'int', required: true },\n"
        "    name: { type: 'string', required: true }\n"
        "});\n"
        "const cfg = getConfig();\n"
        "if (cfg.port !== 9090) throw new Error('cli should override file');\n"
        "if (cfg.name !== 'fromFile') throw new Error('file should fill name');\n"
    );
    EXPECT_EQ(ret, 0);
}

TEST_F(ServiceConfigJsTest, GetConfigBeforeDefineReturnsEmpty) {
    JsConfigBinding::setRawConfig(m_engine->context(), QJsonObject{}, QJsonObject{}, false);
    int ret = evalScript("get_before_define.js",
        "import { getConfig } from 'stdiolink';\n"
        "const cfg = getConfig();\n"
        "if (Object.keys(cfg).length !== 0) throw new Error('expected empty');\n"
    );
    EXPECT_EQ(ret, 0);
}

TEST_F(ServiceConfigJsTest, SchemaAccessibleFromCpp) {
    JsConfigBinding::setRawConfig(
        m_engine->context(),
        QJsonObject{{"port", 8080}},
        QJsonObject{},
        false);
    evalScript("schema_access.js",
        "import { defineConfig } from 'stdiolink';\n"
        "defineConfig({\n"
        "    port: { type: 'int', required: true,\n"
        "            description: 'listen port',\n"
        "            constraints: { min: 1, max: 65535 } }\n"
        "});\n"
    );
    EXPECT_TRUE(JsConfigBinding::hasSchema(m_engine->context()));
    auto schema = JsConfigBinding::getSchema(m_engine->context());
    EXPECT_EQ(schema.fields.size(), 1);
    EXPECT_EQ(schema.fields[0].name, "port");
    auto json = schema.toJson();
    EXPECT_FALSE(json.isEmpty());
}

TEST_F(ServiceConfigJsTest, DumpSchemaMode) {
    JsConfigBinding::setRawConfig(m_engine->context(), QJsonObject{}, QJsonObject{}, true);
    int ret = evalScript("dump_schema.js",
        "import { defineConfig } from 'stdiolink';\n"
        "defineConfig({\n"
        "    port: { type: 'int', required: true },\n"
        "    name: { type: 'string', default: 'svc' }\n"
        "});\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_TRUE(JsConfigBinding::hasSchema(m_engine->context()));
    EXPECT_TRUE(JsConfigBinding::isDumpSchemaMode(m_engine->context()));
}
