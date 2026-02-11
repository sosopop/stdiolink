#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <quickjs.h>

#include "engine/js_engine.h"
#include "engine/console_bridge.h"
#include "bindings/js_log.h"

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

class JsLogTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
        ASSERT_NE(m_engine->context(), nullptr);
        ConsoleBridge::install(m_engine->context());
        m_engine->registerModule("stdiolink/log", JsLogBinding::initModule);
        s_capturedLines.clear();
        s_previousHandler = qInstallMessageHandler(logCapture);
    }

    void TearDown() override {
        qInstallMessageHandler(s_previousHandler);
        m_engine.reset();
    }

    static void logCapture(QtMsgType, const QMessageLogContext&,
                           const QString& msg) {
        s_capturedLines.append(msg.trimmed());
    }

    int runScript(const QString& code) {
        QString path = writeScript(m_tmpDir, "test.mjs", code);
        EXPECT_FALSE(path.isEmpty());
        return m_engine->evalFile(path);
    }

    QJsonObject lastLogJson() const {
        if (s_capturedLines.isEmpty()) return {};
        return QJsonDocument::fromJson(
            s_capturedLines.last().toUtf8()).object();
    }

    QTemporaryDir m_tmpDir;
    std::unique_ptr<JsEngine> m_engine;
    static QStringList s_capturedLines;
    static QtMessageHandler s_previousHandler;
};

QStringList JsLogTest::s_capturedLines;
QtMessageHandler JsLogTest::s_previousHandler = nullptr;

// ── Basic Output ──

TEST_F(JsLogTest, InfoOutputsJsonLine) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger();\n"
        "log.info('hello');\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
    QJsonObject obj = lastLogJson();
    EXPECT_EQ(obj["level"].toString(), "info");
    EXPECT_EQ(obj["msg"].toString(), "hello");
    EXPECT_FALSE(obj["ts"].toString().isEmpty());
}

TEST_F(JsLogTest, AllFourLevelsWork) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger();\n"
        "log.debug('d');\n"
        "log.info('i');\n"
        "log.warn('w');\n"
        "log.error('e');\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_GE(s_capturedLines.size(), 4);
    QStringList levels;
    for (int i = s_capturedLines.size() - 4;
         i < s_capturedLines.size(); ++i) {
        QJsonObject obj = QJsonDocument::fromJson(
            s_capturedLines[i].toUtf8()).object();
        levels.append(obj["level"].toString());
    }
    EXPECT_TRUE(levels.contains("debug"));
    EXPECT_TRUE(levels.contains("info"));
    EXPECT_TRUE(levels.contains("warn"));
    EXPECT_TRUE(levels.contains("error"));
}

TEST_F(JsLogTest, OutputContainsTsLevelMsgFields) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger({ svc: 'test' });\n"
        "log.info('msg', { key: 'val' });\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    QJsonObject obj = lastLogJson();
    EXPECT_TRUE(obj.contains("ts"));
    EXPECT_TRUE(obj.contains("level"));
    EXPECT_TRUE(obj.contains("msg"));
    EXPECT_TRUE(obj.contains("fields"));
    QJsonObject fields = obj["fields"].toObject();
    EXPECT_EQ(fields["svc"].toString(), "test");
    EXPECT_EQ(fields["key"].toString(), "val");
}

// ── Field Inheritance ──

TEST_F(JsLogTest, BaseFieldsIncluded) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger({ service: 'demo' });\n"
        "log.info('test');\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    QJsonObject fields = lastLogJson()["fields"].toObject();
    EXPECT_EQ(fields["service"].toString(), "demo");
}

TEST_F(JsLogTest, ChildInheritsAndMerges) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger({ a: 1 });\n"
        "const child = log.child({ b: 2 });\n"
        "child.info('test');\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    QJsonObject fields = lastLogJson()["fields"].toObject();
    EXPECT_EQ(fields["a"].toInt(), 1);
    EXPECT_EQ(fields["b"].toInt(), 2);
}

TEST_F(JsLogTest, ChildChainMergesCorrectly) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger({ a: 1 });\n"
        "const c1 = log.child({ b: 2 });\n"
        "const c2 = c1.child({ c: 3 });\n"
        "c2.info('test');\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    QJsonObject fields = lastLogJson()["fields"].toObject();
    EXPECT_EQ(fields["a"].toInt(), 1);
    EXPECT_EQ(fields["b"].toInt(), 2);
    EXPECT_EQ(fields["c"].toInt(), 3);
}

TEST_F(JsLogTest, CallFieldsOverrideBaseFields) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger({ key: 'base' });\n"
        "const child = log.child({ key: 'child' });\n"
        "child.info('test', { key: 'call' });\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    QJsonObject fields = lastLogJson()["fields"].toObject();
    EXPECT_EQ(fields["key"].toString(), "call");
}

TEST_F(JsLogTest, ChildOverridesParentField) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger({ key: 'parent' });\n"
        "const child = log.child({ key: 'child' });\n"
        "child.info('test');\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    QJsonObject fields = lastLogJson()["fields"].toObject();
    EXPECT_EQ(fields["key"].toString(), "child");
}

// ── Stability & Edge Cases ──

TEST_F(JsLogTest, FieldsNonObjectThrowsTypeError) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger();\n"
        "try { log.info('msg', 'not-object');\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = (e instanceof TypeError) ? 1 : 0;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsLogTest, MsgNonStringAutoConverts) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger();\n"
        "log.info(42);\n"
        "log.info({ key: 'val' });\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsLogTest, CreateLoggerNoArgsWorks) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger();\n"
        "log.info('bare logger');\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
    QJsonObject obj = lastLogJson();
    EXPECT_EQ(obj["msg"].toString(), "bare logger");
}

TEST_F(JsLogTest, FieldsNullOrUndefinedIgnored) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger();\n"
        "log.info('test', null);\n"
        "log.info('test', undefined);\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}
