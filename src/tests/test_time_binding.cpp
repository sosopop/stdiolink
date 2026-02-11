#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <quickjs.h>

#include "engine/js_engine.h"
#include "engine/console_bridge.h"
#include "bindings/js_time.h"

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

class JsTimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
        ASSERT_NE(m_engine->context(), nullptr);
        ConsoleBridge::install(m_engine->context());
        JsTimeBinding::attachRuntime(m_engine->runtime());
        m_engine->registerModule("stdiolink/time", JsTimeBinding::initModule);
    }

    void TearDown() override {
        JsTimeBinding::reset(m_engine->context());
        m_engine.reset();
    }

    int runScript(const QString& code) {
        QString path = writeScript(m_tmpDir, "test.mjs", code);
        EXPECT_FALSE(path.isEmpty());
        int ret = m_engine->evalFile(path);
        // Drive pending jobs (sleep Promise callbacks need event loop)
        for (int i = 0; i < 200; ++i) {
            QCoreApplication::processEvents();
            if (!m_engine->hasPendingJobs()) break;
            m_engine->executePendingJobs();
        }
        return ret;
    }

    QTemporaryDir m_tmpDir;
    std::unique_ptr<JsEngine> m_engine;
};

// ── Functional Tests ──

TEST_F(JsTimeTest, NowMsReturnsNumber) {
    int ret = runScript(
        "import { nowMs } from 'stdiolink/time';\n"
        "const t = nowMs();\n"
        "globalThis.ok = (typeof t === 'number' && t > 1e12) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsTimeTest, MonotonicMsIsNonDecreasing) {
    int ret = runScript(
        "import { monotonicMs } from 'stdiolink/time';\n"
        "const a = monotonicMs();\n"
        "const b = monotonicMs();\n"
        "globalThis.ok = (typeof a === 'number'"
        " && a >= 0 && b >= a) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Error Paths ──

TEST_F(JsTimeTest, SleepNegativeThrowsRangeError) {
    int ret = runScript(
        "import { sleep } from 'stdiolink/time';\n"
        "try { await sleep(-1); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof RangeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsTimeTest, SleepNaNThrowsRangeError) {
    int ret = runScript(
        "import { sleep } from 'stdiolink/time';\n"
        "try { await sleep(NaN); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof RangeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsTimeTest, SleepStringThrowsTypeError) {
    int ret = runScript(
        "import { sleep } from 'stdiolink/time';\n"
        "try { await sleep('100'); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof TypeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsTimeTest, SleepNoArgThrowsTypeError) {
    int ret = runScript(
        "import { sleep } from 'stdiolink/time';\n"
        "try { await sleep(); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof TypeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsTimeTest, SleepZeroResolvesQuickly) {
    int ret = runScript(
        "import { sleep } from 'stdiolink/time';\n"
        "await sleep(0);\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}
