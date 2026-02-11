#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <quickjs.h>

#include "engine/js_engine.h"
#include "engine/console_bridge.h"
#include "bindings/js_path.h"

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

class JsPathTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
        ASSERT_NE(m_engine->context(), nullptr);
        ConsoleBridge::install(m_engine->context());
        m_engine->registerModule("stdiolink/path", JsPathBinding::initModule);
    }

    void TearDown() override {
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

// ── Basic Functionality ──

TEST_F(JsPathTest, JoinBasic) {
    int ret = runScript(
        "import { join } from 'stdiolink/path';\n"
        "globalThis.ok = (join('a','b','c') === 'a/b/c') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsPathTest, DirnameBasic) {
    int ret = runScript(
        "import { dirname } from 'stdiolink/path';\n"
        "globalThis.ok = (dirname('/a/b/c.txt') === '/a/b') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsPathTest, BasenameBasic) {
    int ret = runScript(
        "import { basename } from 'stdiolink/path';\n"
        "globalThis.ok = (basename('/a/b/c.txt') === 'c.txt') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsPathTest, ExtnameBasic) {
    int ret = runScript(
        "import { extname } from 'stdiolink/path';\n"
        "globalThis.ok = (extname('/a/b/c.txt') === '.txt') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Normalize & Absolute ──

TEST_F(JsPathTest, NormalizeRemovesDotDot) {
    int ret = runScript(
        "import { normalize } from 'stdiolink/path';\n"
        "globalThis.ok = (normalize('a/./b/../c') === 'a/c') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsPathTest, ResolveReturnsAbsolute) {
    int ret = runScript(
        "import { resolve, isAbsolute } from 'stdiolink/path';\n"
        "const r = resolve('a', 'b');\n"
        "globalThis.ok = isAbsolute(r) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsPathTest, ResolveAbsoluteOverrides) {
    int ret = runScript(
        "import { resolve } from 'stdiolink/path';\n"
        "const r = resolve('/foo', '/bar', 'baz');\n"
        "globalThis.ok = (r === '/bar/baz') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsPathTest, IsAbsoluteUnixPath) {
    int ret = runScript(
        "import { isAbsolute } from 'stdiolink/path';\n"
        "globalThis.ok = isAbsolute('/usr/bin') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsPathTest, IsAbsoluteRelativePath) {
    int ret = runScript(
        "import { isAbsolute } from 'stdiolink/path';\n"
        "globalThis.ok = isAbsolute('a/b') ? 0 : 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Edge Cases ──

TEST_F(JsPathTest, JoinZeroArgs) {
    int ret = runScript(
        "import { join } from 'stdiolink/path';\n"
        "globalThis.ok = (join() === '.') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsPathTest, BasenameTrailingSeparator) {
    int ret = runScript(
        "import { basename } from 'stdiolink/path';\n"
        "globalThis.ok = (basename('/a/b/') === 'b') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsPathTest, ExtnameMultiSuffix) {
    int ret = runScript(
        "import { extname } from 'stdiolink/path';\n"
        "globalThis.ok = (extname('archive.tar.gz') === '.gz') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsPathTest, ExtnameNoSuffix) {
    int ret = runScript(
        "import { extname } from 'stdiolink/path';\n"
        "globalThis.ok = (extname('Makefile') === '') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Error Paths ──

TEST_F(JsPathTest, DirnameNonStringThrows) {
    int ret = runScript(
        "import { dirname } from 'stdiolink/path';\n"
        "try { dirname(123); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof TypeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsPathTest, JoinNonStringArgThrows) {
    int ret = runScript(
        "import { join } from 'stdiolink/path';\n"
        "try { join('a', 123); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof TypeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsPathTest, ResolveNonStringThrows) {
    int ret = runScript(
        "import { resolve } from 'stdiolink/path';\n"
        "try { resolve(42); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof TypeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}
