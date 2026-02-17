#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <quickjs.h>
#include "bindings/js_stdiolink_module.h"
#include "engine/console_bridge.h"
#include "engine/js_engine.h"
#include "utils/js_convert.h"
#include "stdiolink/platform/platform_utils.h"

namespace {

QString writeScript(const QTemporaryDir& dir, const QString& relativePath, const QString& content) {
    const QString fullPath = dir.path() + "/" + relativePath;
    QFileInfo info(fullPath);
    info.absoluteDir().mkpath(".");
    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream out(&file);
    out << content;
    out.flush();
    return fullPath;
}

int readGlobalInt(JSContext* ctx, const char* key) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue val = JS_GetPropertyStr(ctx, global, key);
    int32_t result = 0;
    JS_ToInt32(ctx, &result, val);
    JS_FreeValue(ctx, val);
    JS_FreeValue(ctx, global);
    return result;
}

QString driverBinaryPath() {
    return stdiolink::PlatformUtils::executablePath(
        QCoreApplication::applicationDirPath(), "stdio.drv.calculator");
}

QString escapeForSingleQuoteJs(const QString& s) {
    QString out = s;
    out.replace("\\", "/");
    out.replace("'", "\\'");
    return out;
}

} // namespace

class JsConvertTest : public ::testing::Test {
protected:
    void SetUp() override { m_engine = std::make_unique<JsEngine>(); }

    std::unique_ptr<JsEngine> m_engine;
};

TEST_F(JsConvertTest, QJsonObjectRoundTrip) {
    QJsonObject original{
        {"name", "test"},
        {"count", 42},
        {"active", true},
        {"tags", QJsonArray{"a", "b"}},
        {"nested", QJsonObject{{"x", 1.5}}},
    };

    JSValue js = qjsonObjectToJsValue(m_engine->context(), original);
    QJsonObject back = jsValueToQJsonObject(m_engine->context(), js);
    JS_FreeValue(m_engine->context(), js);

    EXPECT_EQ(back.value("name").toString(), "test");
    EXPECT_EQ(back.value("count").toInt(), 42);
    EXPECT_TRUE(back.value("active").toBool());
    EXPECT_EQ(back.value("tags").toArray().size(), 2);
    EXPECT_DOUBLE_EQ(back.value("nested").toObject().value("x").toDouble(), 1.5);
}

TEST_F(JsConvertTest, JsArrayToQJson) {
    JSContext* ctx = m_engine->context();
    JSValue arr = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, arr, 0, JS_NewInt32(ctx, 10));
    JS_SetPropertyUint32(ctx, arr, 1, JS_NewString(ctx, "x"));

    QJsonValue value = jsValueToQJson(ctx, arr);
    JS_FreeValue(ctx, arr);

    ASSERT_TRUE(value.isArray());
    QJsonArray a = value.toArray();
    ASSERT_EQ(a.size(), 2);
    EXPECT_EQ(a.at(0).toInt(), 10);
    EXPECT_EQ(a.at(1).toString(), "x");
}

class JsDriverBindingTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
        ConsoleBridge::install(m_engine->context());
        m_engine->registerModule("stdiolink", jsInitStdiolinkModule);
    }

    int runScript(const QString& scriptPath) {
        const int ret = m_engine->evalFile(scriptPath);
        while (m_engine->hasPendingJobs()) {
            m_engine->executePendingJobs();
        }
        return ret;
    }

    std::unique_ptr<JsEngine> m_engine;
    QTemporaryDir m_tmpDir;
};

TEST_F(JsDriverBindingTest, ImportAndConstructDriver) {
    const QString scriptPath =
        writeScript(m_tmpDir, "import_driver.js",
                    "import { Driver } from 'stdiolink';\n"
                    "const d = new Driver();\n"
                    "globalThis.ok = (typeof Driver === 'function' && d) ? 1 : 0;\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsDriverBindingTest, StartNonexistentReturnsFalse) {
    const QString scriptPath =
        writeScript(m_tmpDir, "start_nonexistent.js",
                    "import { Driver } from 'stdiolink';\n"
                    "const d = new Driver();\n"
                    "globalThis.ok = d.start('__missing_driver__') ? 0 : 1;\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsDriverBindingTest, RequestAndWaitNextWithCalculatorDriver) {
    const QString bin = driverBinaryPath();
    ASSERT_TRUE(QFileInfo::exists(bin));

    const QString scriptPath =
        writeScript(m_tmpDir, "request_wait.js",
                    QString("import { Driver } from 'stdiolink';\n"
                            "const d = new Driver();\n"
                            "if (!d.start('%1')) throw new Error('start failed');\n"
                            "const t = d.request('add', { a: 10, b: 20 });\n"
                            "const m = t.waitNext(5000);\n"
                            "globalThis.ok = (m && m.status === 'done' && m.data && m.data.result "
                            "=== 30) ? 1 : 0;\n"
                            "globalThis.done = t.done ? 1 : 0;\n"
                            "d.terminate();\n")
                        .arg(escapeForSingleQuoteJs(bin)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "done"), 1);
}

TEST_F(JsDriverBindingTest, QueryMetaReturnsObject) {
    const QString bin = driverBinaryPath();
    ASSERT_TRUE(QFileInfo::exists(bin));

    const QString scriptPath =
        writeScript(m_tmpDir, "query_meta.js",
                    QString("import { Driver } from 'stdiolink';\n"
                            "const d = new Driver();\n"
                            "if (!d.start('%1')) throw new Error('start failed');\n"
                            "const meta = d.queryMeta(5000);\n"
                            "globalThis.hasMeta = meta ? 1 : 0;\n"
                            "globalThis.hasCommands = (meta && meta.commands && "
                            "meta.commands.length > 0) ? 1 : 0;\n"
                            "d.terminate();\n")
                        .arg(escapeForSingleQuoteJs(bin)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "hasMeta"), 1);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "hasCommands"), 1);
}
