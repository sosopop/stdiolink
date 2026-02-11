#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <quickjs.h>

#include "engine/js_engine.h"
#include "engine/console_bridge.h"
#include "bindings/js_fs.h"

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

class JsFsTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
        ASSERT_NE(m_engine->context(), nullptr);
        ConsoleBridge::install(m_engine->context());
        m_engine->registerModule("stdiolink/fs", JsFsBinding::initModule);
    }

    void TearDown() override {
        m_engine.reset();
    }

    int runScript(const QString& code) {
        // Inject tmpDir path as global, escaping backslashes for Windows
        QString escaped = m_tmpDir.path();
        escaped.replace('\\', '/');
        QString wrapped = QString(
            "globalThis.__tmpDir = '%1';\n%2"
        ).arg(escaped, code);
        QString path = writeScript(m_tmpDir, "test.mjs", wrapped);
        EXPECT_FALSE(path.isEmpty());
        return m_engine->evalFile(path);
    }

    QTemporaryDir m_tmpDir;
    std::unique_ptr<JsEngine> m_engine;
};

// ── Basic Read/Write ──

TEST_F(JsFsTest, WriteTextAndReadText) {
    int ret = runScript(
        "import { writeText, readText } from 'stdiolink/fs';\n"
        "const p = __tmpDir + '/hello.txt';\n"
        "writeText(p, 'Hello World');\n"
        "globalThis.ok = (readText(p) === 'Hello World') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsFsTest, WriteJsonAndReadJson) {
    int ret = runScript(
        "import { writeJson, readJson } from 'stdiolink/fs';\n"
        "const p = __tmpDir + '/cfg.json';\n"
        "writeJson(p, { port: 8080, name: 'test' });\n"
        "const cfg = readJson(p);\n"
        "globalThis.ok = (cfg.port === 8080 && cfg.name === 'test') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsFsTest, ExistsReturnsTrueForFile) {
    int ret = runScript(
        "import { writeText, exists } from 'stdiolink/fs';\n"
        "const p = __tmpDir + '/exist.txt';\n"
        "writeText(p, 'x');\n"
        "globalThis.ok = exists(p) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsFsTest, ExistsReturnsFalseForMissing) {
    int ret = runScript(
        "import { exists } from 'stdiolink/fs';\n"
        "globalThis.ok = exists(__tmpDir + '/nope.txt') ? 0 : 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsFsTest, StatReturnsCorrectFields) {
    int ret = runScript(
        "import { writeText, stat } from 'stdiolink/fs';\n"
        "const p = __tmpDir + '/stat_test.txt';\n"
        "writeText(p, 'abc');\n"
        "const s = stat(p);\n"
        "globalThis.ok = (s.isFile === true && s.isDir === false\n"
        "  && s.size >= 3 && typeof s.mtimeMs === 'number') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Options Behavior ──

TEST_F(JsFsTest, AppendMode) {
    int ret = runScript(
        "import { writeText, readText } from 'stdiolink/fs';\n"
        "const p = __tmpDir + '/append.txt';\n"
        "writeText(p, 'A');\n"
        "writeText(p, 'B', { append: true });\n"
        "globalThis.ok = (readText(p) === 'AB') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsFsTest, EnsureParentCreatesDir) {
    int ret = runScript(
        "import { writeText, exists } from 'stdiolink/fs';\n"
        "const p = __tmpDir + '/deep/nested/file.txt';\n"
        "writeText(p, 'ok', { ensureParent: true });\n"
        "globalThis.ok = exists(p) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsFsTest, MkdirRecursive) {
    int ret = runScript(
        "import { mkdir, exists } from 'stdiolink/fs';\n"
        "const d = __tmpDir + '/a/b/c';\n"
        "mkdir(d);\n"
        "globalThis.ok = exists(d) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsFsTest, ListDirSorted) {
    int ret = runScript(
        "import { writeText, listDir } from 'stdiolink/fs';\n"
        "writeText(__tmpDir + '/c.txt', '');\n"
        "writeText(__tmpDir + '/a.txt', '');\n"
        "writeText(__tmpDir + '/b.txt', '');\n"
        "const list = listDir(__tmpDir);\n"
        "const sorted = list.filter(f => f.endsWith('.txt'));\n"
        "globalThis.ok = (sorted[0] === 'a.txt'"
        " && sorted[1] === 'b.txt' && sorted[2] === 'c.txt') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsFsTest, EmptyDirListReturnsEmptyArray) {
    int ret = runScript(
        "import { mkdir, listDir } from 'stdiolink/fs';\n"
        "const d = __tmpDir + '/empty_dir';\n"
        "mkdir(d);\n"
        "const list = listDir(d);\n"
        "globalThis.ok = (Array.isArray(list) && list.length === 0) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Error Paths ──

TEST_F(JsFsTest, ReadNonExistentFileThrows) {
    int ret = runScript(
        "import { readText } from 'stdiolink/fs';\n"
        "try { readText('/nonexistent/path.txt'); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = 1; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsFsTest, ReadJsonInvalidJsonThrows) {
    int ret = runScript(
        "import { writeText, readJson } from 'stdiolink/fs';\n"
        "const p = __tmpDir + '/bad.json';\n"
        "writeText(p, 'not json {{{');\n"
        "try { readJson(p); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = 1; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsFsTest, StatNonExistentThrows) {
    int ret = runScript(
        "import { stat } from 'stdiolink/fs';\n"
        "try { stat('/no/such/path'); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = e.message.includes('/no/such/path') ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsFsTest, ExistsEmptyStringThrowsTypeError) {
    int ret = runScript(
        "import { exists } from 'stdiolink/fs';\n"
        "try { exists(''); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof TypeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsFsTest, NonStringArgThrowsTypeError) {
    int ret = runScript(
        "import { readText } from 'stdiolink/fs';\n"
        "try { readText(123); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof TypeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}
