#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>

#include <quickjs.h>
#include "engine/console_bridge.h"
#include "engine/js_engine.h"
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

QString servicePath() {
    return stdiolink::PlatformUtils::executablePath(
        QCoreApplication::applicationDirPath(), "stdiolink_service");
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

} // namespace

class JsEngineScaffoldTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
    }

    std::unique_ptr<JsEngine> m_engine;
    QTemporaryDir m_tmpDir;
};

TEST_F(JsEngineScaffoldTest, ContextAndRuntimeCreated) {
    EXPECT_NE(m_engine->context(), nullptr);
    EXPECT_NE(m_engine->runtime(), nullptr);
}

TEST_F(JsEngineScaffoldTest, EvalSimpleScript) {
    const QString scriptPath = writeScript(m_tmpDir, "simple.js", "globalThis.result = 3;");
    ASSERT_FALSE(scriptPath.isEmpty());
    EXPECT_EQ(m_engine->evalFile(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "result"), 3);
}

TEST_F(JsEngineScaffoldTest, EvalMissingFileReturns2) {
    EXPECT_EQ(m_engine->evalFile(m_tmpDir.path() + "/missing.js"), 2);
}

TEST_F(JsEngineScaffoldTest, EvalSyntaxErrorReturns1) {
    const QString scriptPath = writeScript(m_tmpDir, "bad.js", "globalThis.result = ;");
    ASSERT_FALSE(scriptPath.isEmpty());
    EXPECT_EQ(m_engine->evalFile(scriptPath), 1);
}

TEST_F(JsEngineScaffoldTest, PromiseJobsAreDrained) {
    const QString scriptPath =
        writeScript(m_tmpDir, "promise.js",
                    "globalThis.result = 0;\n"
                    "Promise.resolve(42).then(v => { globalThis.result = v; });\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(m_engine->evalFile(scriptPath), 0);
    while (m_engine->hasPendingJobs()) {
        m_engine->executePendingJobs();
    }
    EXPECT_FALSE(m_engine->hadJobError());
    EXPECT_EQ(readGlobalInt(m_engine->context(), "result"), 42);
}

TEST_F(JsEngineScaffoldTest, ConsoleBridgeCallable) {
    ConsoleBridge::install(m_engine->context());
    const QString scriptPath = writeScript(m_tmpDir, "console.js",
                                           "console.log('a=', 1, {x:2});\n"
                                           "console.warn('w');\n"
                                           "console.error('e');\n");
    ASSERT_FALSE(scriptPath.isEmpty());
    EXPECT_EQ(m_engine->evalFile(scriptPath), 0);
}

class StdioLinkServiceEntryTest : public ::testing::Test {
protected:
    struct RunResult {
        int exitCode = -1;
        QString stdoutText;
        QString stderrText;
    };

    RunResult runService(const QStringList& args) const {
        QProcess proc;
        proc.start(servicePath(), args);
        const bool finished = proc.waitForFinished(10000);
        EXPECT_TRUE(finished);
        RunResult result;
        result.exitCode = finished ? proc.exitCode() : -1;
        result.stdoutText = QString::fromUtf8(proc.readAllStandardOutput());
        result.stderrText = QString::fromUtf8(proc.readAllStandardError());
        return result;
    }
};

TEST_F(StdioLinkServiceEntryTest, HelpAndVersion) {
    const RunResult help = runService({"--help"});
    EXPECT_EQ(help.exitCode, 0);
    EXPECT_TRUE(help.stderrText.contains("Usage: stdiolink_service"));

    const RunResult version = runService({"--version"});
    EXPECT_EQ(version.exitCode, 0);
    EXPECT_TRUE(version.stderrText.contains("stdiolink_service"));
}

TEST_F(StdioLinkServiceEntryTest, MissingFileReturns2) {
    const RunResult result = runService({"__missing__.js"});
    EXPECT_EQ(result.exitCode, 2);
}

TEST_F(StdioLinkServiceEntryTest, BasicScriptWritesStderr) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeScript(dir, "manifest.json",
                R"({"manifestVersion":"1","id":"test","name":"Test","version":"1.0"})");
    writeScript(dir, "config.schema.json", "{}");
    writeScript(dir, "index.js", "console.log('hello-m21');\n");

    const RunResult result = runService({dir.path()});
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_TRUE(result.stdoutText.isEmpty());
    EXPECT_TRUE(result.stderrText.contains("hello-m21"));
}

TEST_F(StdioLinkServiceEntryTest, SyntaxErrorReturns1) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeScript(dir, "manifest.json",
                R"({"manifestVersion":"1","id":"test","name":"Test","version":"1.0"})");
    writeScript(dir, "config.schema.json", "{}");
    writeScript(dir, "index.js", "let = ;\n");

    const RunResult result = runService({dir.path()});
    EXPECT_EQ(result.exitCode, 1);
}
