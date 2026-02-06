#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include "bindings/js_stdiolink_module.h"
#include "engine/console_bridge.h"
#include "engine/js_engine.h"
#include "quickjs.h"

namespace {

QString writeScript(const QTemporaryDir& dir, const QString& relativePath, const QString& content) {
    const QString fullPath = dir.path() + "/" + relativePath;
    QFileInfo fi(fullPath);
    fi.absoluteDir().mkpath(".");
    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream out(&file);
    out << content;
    out.flush();
    return fullPath;
}

QString escapeJsString(const QString& s) {
    QString out = s;
    out.replace("\\", "/");
    out.replace("'", "\\'");
    return out;
}

int readGlobalInt(JSContext* ctx, const char* key) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue val = JS_GetPropertyStr(ctx, global, key);
    int32_t out = 0;
    JS_ToInt32(ctx, &out, val);
    JS_FreeValue(ctx, val);
    JS_FreeValue(ctx, global);
    return out;
}

} // namespace

class JsProcessBindingTest : public ::testing::Test {
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

TEST_F(JsProcessBindingTest, ImportExec) {
    const QString script = writeScript(
        m_tmpDir,
        "import_exec.js",
        "import { exec } from 'stdiolink';\n"
        "globalThis.ok = (typeof exec === 'function') ? 1 : 0;\n");
    ASSERT_FALSE(script.isEmpty());
    EXPECT_EQ(runScript(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProcessBindingTest, ExecEcho) {
    const QString script = writeScript(
        m_tmpDir,
        "echo.js",
        "import { exec } from 'stdiolink';\n"
        "const r = exec('cmd', ['/c', 'echo', 'hello']);\n"
        "globalThis.exitCode = r.exitCode;\n"
        "globalThis.hasStdout = r.stdout.includes('hello') ? 1 : 0;\n");
    ASSERT_FALSE(script.isEmpty());
    EXPECT_EQ(runScript(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "exitCode"), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "hasStdout"), 1);
}

TEST_F(JsProcessBindingTest, ExecNonZeroExit) {
    const QString script = writeScript(
        m_tmpDir,
        "exit42.js",
        "import { exec } from 'stdiolink';\n"
        "const r = exec('cmd', ['/c', 'exit', '42']);\n"
        "globalThis.exitCode = r.exitCode;\n");
    ASSERT_FALSE(script.isEmpty());
    EXPECT_EQ(runScript(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "exitCode"), 42);
}

TEST_F(JsProcessBindingTest, ExecWithCwdAndEnvAndInput) {
    QFile marker(m_tmpDir.path() + "/marker.txt");
    ASSERT_TRUE(marker.open(QIODevice::WriteOnly | QIODevice::Text));
    marker.write("ok");
    marker.close();

    const QString script = writeScript(
        m_tmpDir,
        "opts.js",
        QString(
            "import { exec } from 'stdiolink';\n"
            "const cwd = '%1';\n"
            "const r1 = exec('cmd', ['/c', 'dir', '/b'], { cwd });\n"
            "const r2 = exec('cmd', ['/c', 'echo', '%%MY_X%%'], { env: { MY_X: 'abc' } });\n"
            "const r3 = exec('cmd', ['/c', 'more'], { input: 'line-in\\n' });\n"
            "globalThis.hasMarker = r1.stdout.includes('marker.txt') ? 1 : 0;\n"
            "globalThis.hasEnv = r2.stdout.toLowerCase().includes('abc') ? 1 : 0;\n"
            "globalThis.hasInput = r3.stdout.includes('line-in') ? 1 : 0;\n")
            .arg(escapeJsString(m_tmpDir.path())));
    ASSERT_FALSE(script.isEmpty());
    EXPECT_EQ(runScript(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "hasMarker"), 1);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "hasEnv"), 1);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "hasInput"), 1);
}

TEST_F(JsProcessBindingTest, ExecTimeoutThrows) {
    const QString script = writeScript(
        m_tmpDir,
        "timeout.js",
        "import { exec } from 'stdiolink';\n"
        "try {\n"
        "  exec('cmd', ['/c', 'ping', '-n', '5', '127.0.0.1', '>nul'], { timeout: 50 });\n"
        "  globalThis.caught = 0;\n"
        "} catch (e) {\n"
        "  globalThis.caught = 1;\n"
        "}\n");
    ASSERT_FALSE(script.isEmpty());
    EXPECT_EQ(runScript(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "caught"), 1);
}

TEST_F(JsProcessBindingTest, ExecNonexistentProgramThrows) {
    const QString script = writeScript(
        m_tmpDir,
        "missing_program.js",
        "import { exec } from 'stdiolink';\n"
        "try {\n"
        "  exec('__definitely_missing_program__');\n"
        "  globalThis.caught = 0;\n"
        "} catch (e) {\n"
        "  globalThis.caught = 1;\n"
        "}\n");
    ASSERT_FALSE(script.isEmpty());
    EXPECT_EQ(runScript(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "caught"), 1);
}

