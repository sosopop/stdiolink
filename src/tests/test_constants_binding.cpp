#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QJsonObject>
#include <QJsonArray>
#include <quickjs.h>

#include "engine/js_engine.h"
#include "engine/module_loader.h"
#include "engine/console_bridge.h"
#include "bindings/js_constants.h"
#include "bindings/js_stdiolink_module.h"
#include "bindings/js_config.h"

using namespace stdiolink_service;

namespace {

QString writeScript(const QTemporaryDir& dir, const QString& name, const QString& content) {
    QString path = dir.path() + "/" + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    QTextStream out(&f);
    out << content;
    out.flush();
    return path;
}

int readGlobalInt(JSContext* ctx, const char* key) {
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue v = JS_GetPropertyStr(ctx, g, key);
    int32_t r = 0;
    JS_ToInt32(ctx, &r, v);
    JS_FreeValue(ctx, v);
    JS_FreeValue(ctx, g);
    return r;
}

} // namespace

// ── Constants Binding Tests ──

class JsConstantsTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
        ASSERT_NE(m_engine->context(), nullptr);
        ConsoleBridge::install(m_engine->context());

        JsConfigBinding::attachRuntime(m_engine->runtime());
        JsConstantsBinding::attachRuntime(m_engine->runtime());
        JsConstantsBinding::setPathContext(m_engine->context(), {
            "/usr/bin/stdiolink_service",
            "/usr/bin",
            "/home/user",
            "/srv/demo",
            "/srv/demo/index.js",
            "/srv/demo",
            "/tmp",
            "/home/user"
        });

        m_engine->registerModule("stdiolink", jsInitStdiolinkModule);
        m_engine->registerModule("stdiolink/constants", JsConstantsBinding::initModule);
    }

    void TearDown() override {
        JsConstantsBinding::reset(m_engine->context());
        JsConfigBinding::reset(m_engine->context());
        m_engine.reset();
    }

    int runScript(const QString& code) {
        QString path = writeScript(m_tmpDir, "test.mjs", code);
        EXPECT_FALSE(path.isEmpty());
        return m_engine->evalFile(path);
    }

    QTemporaryDir m_tmpDir;
    std::unique_ptr<JsEngine> m_engine;
};

// ── Module Loading & Field Completeness ──

TEST_F(JsConstantsTest, ImportSucceeds) {
    int ret = runScript(
        "import { SYSTEM, APP_PATHS } from 'stdiolink/constants';\n"
        "globalThis.ok = (typeof SYSTEM === 'object'"
        " && typeof APP_PATHS === 'object') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsConstantsTest, SystemFieldsComplete) {
    int ret = runScript(
        "import { SYSTEM } from 'stdiolink/constants';\n"
        "const fields = ['os','arch','isWindows','isMac','isLinux'];\n"
        "globalThis.ok = fields.every(f => f in SYSTEM) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsConstantsTest, AppPathsFieldsComplete) {
    int ret = runScript(
        "import { APP_PATHS } from 'stdiolink/constants';\n"
        "const fields = ['appPath','appDir','cwd','serviceDir',\n"
        "  'serviceEntryPath','serviceEntryDir','tempDir','homeDir'];\n"
        "globalThis.ok = fields.every(f => f in APP_PATHS) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Platform Consistency ──

TEST_F(JsConstantsTest, PlatformBoolsMutuallyExclusive) {
    int ret = runScript(
        "import { SYSTEM } from 'stdiolink/constants';\n"
        "const count = [SYSTEM.isWindows, SYSTEM.isMac, SYSTEM.isLinux]\n"
        "  .filter(Boolean).length;\n"
        "globalThis.ok = (count === 1) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsConstantsTest, OsMatchesBoolFlags) {
    int ret = runScript(
        "import { SYSTEM } from 'stdiolink/constants';\n"
        "let ok = false;\n"
        "if (SYSTEM.os === 'windows') ok = SYSTEM.isWindows;\n"
        "else if (SYSTEM.os === 'macos') ok = SYSTEM.isMac;\n"
        "else if (SYSTEM.os === 'linux') ok = SYSTEM.isLinux;\n"
        "globalThis.ok = ok ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsConstantsTest, ArchIsNonEmptyString) {
    int ret = runScript(
        "import { SYSTEM } from 'stdiolink/constants';\n"
        "globalThis.ok = (typeof SYSTEM.arch === 'string'"
        " && SYSTEM.arch.length > 0) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Path Value Validity ──

TEST_F(JsConstantsTest, AllPathsNonEmpty) {
    int ret = runScript(
        "import { APP_PATHS } from 'stdiolink/constants';\n"
        "const vals = Object.values(APP_PATHS);\n"
        "globalThis.ok = vals.every(v =>"
        " typeof v === 'string' && v.length > 0) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsConstantsTest, AppDirIsParentOfAppPath) {
    int ret = runScript(
        "import { APP_PATHS } from 'stdiolink/constants';\n"
        "globalThis.ok = APP_PATHS.appPath.startsWith("
        "APP_PATHS.appDir) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsConstantsTest, ServiceEntryDirIsParentOfServiceEntryPath) {
    int ret = runScript(
        "import { APP_PATHS } from 'stdiolink/constants';\n"
        "globalThis.ok = APP_PATHS.serviceEntryPath.startsWith("
        "APP_PATHS.serviceEntryDir) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Frozen (Read-Only) ──

TEST_F(JsConstantsTest, SystemIsFrozen) {
    int ret = runScript(
        "import { SYSTEM } from 'stdiolink/constants';\n"
        "globalThis.ok = Object.isFrozen(SYSTEM) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsConstantsTest, AppPathsIsFrozen) {
    int ret = runScript(
        "import { APP_PATHS } from 'stdiolink/constants';\n"
        "globalThis.ok = Object.isFrozen(APP_PATHS) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsConstantsTest, SystemWriteThrowsInStrictMode) {
    int ret = runScript(
        "'use strict';\n"
        "import { SYSTEM } from 'stdiolink/constants';\n"
        "try {\n"
        "  SYSTEM.os = 'hacked';\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = 1;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Deep Freeze Regression (getConfig) ──

class DeepFreezeConfigTest : public ::testing::Test {
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

    int runScript(const QString& code) {
        QString path = writeScript(m_tmpDir, "test.mjs", code);
        EXPECT_FALSE(path.isEmpty());
        return m_engine->evalFile(path);
    }

    QTemporaryDir m_tmpDir;
    std::unique_ptr<JsEngine> m_engine;
};

TEST_F(DeepFreezeConfigTest, NestedObjectIsFrozen) {
    QJsonObject nested{{"host", "localhost"}, {"port", 3306}};
    QJsonObject config{{"db", nested}};
    JsConfigBinding::setMergedConfig(m_engine->context(), config);

    int ret = runScript(
        "import { getConfig } from 'stdiolink';\n"
        "const cfg = getConfig();\n"
        "globalThis.ok = Object.isFrozen(cfg.db) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(DeepFreezeConfigTest, NestedArrayIsFrozen) {
    QJsonArray arr{1, 2, 3};
    QJsonObject config{{"items", arr}};
    JsConfigBinding::setMergedConfig(m_engine->context(), config);

    int ret = runScript(
        "import { getConfig } from 'stdiolink';\n"
        "const cfg = getConfig();\n"
        "globalThis.ok = Object.isFrozen(cfg.items) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(DeepFreezeConfigTest, NestedObjectWriteThrows) {
    QJsonObject nested{{"host", "localhost"}};
    QJsonObject config{{"db", nested}};
    JsConfigBinding::setMergedConfig(m_engine->context(), config);

    int ret = runScript(
        "import { getConfig } from 'stdiolink';\n"
        "const cfg = getConfig();\n"
        "try {\n"
        "  cfg.db.host = 'hacked';\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = 1;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}
