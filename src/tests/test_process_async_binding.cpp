#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <quickjs.h>

#include "engine/js_engine.h"
#include "engine/console_bridge.h"
#include "bindings/js_process_async.h"

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

QString stubPath() {
    return QCoreApplication::applicationDirPath() + "/test_process_async_stub";
}

} // namespace

class JsProcessAsyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
        ASSERT_NE(m_engine->context(), nullptr);
        ConsoleBridge::install(m_engine->context());
        JsProcessAsyncBinding::attachRuntime(m_engine->runtime());
        m_engine->registerModule("stdiolink/process",
                                 JsProcessAsyncBinding::initModule);
    }

    void TearDown() override {
        JsProcessAsyncBinding::reset(m_engine->context());
        m_engine.reset();
    }

    int runScript(const QString& code) {
        QString wrapped = QString(
            "globalThis.__stub = '%1';\n%2"
        ).arg(stubPath(), code);
        QString path = writeScript(m_tmpDir, "test.mjs", wrapped);
        EXPECT_FALSE(path.isEmpty());
        int ret = m_engine->evalFile(path);

        for (int i = 0; i < 2000; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            while (m_engine->hasPendingJobs()) {
                m_engine->executePendingJobs();
            }
            if (!JsProcessAsyncBinding::hasPending(m_engine->context())
                && !m_engine->hasPendingJobs()) {
                break;
            }
            QThread::msleep(5);
        }
        return ret;
    }

    QTemporaryDir m_tmpDir;
    std::unique_ptr<JsEngine> m_engine;
};

// ── execAsync ──

TEST_F(JsProcessAsyncTest, ExecAsyncResolvesOnExitCodeZero) {
    int ret = runScript(
        "import { execAsync } from 'stdiolink/process';\n"
        "const r = await execAsync(__stub, ['--mode=stdout', '--text=hello']);\n"
        "globalThis.ok = (r.exitCode === 0"
        " && r.stdout.trim() === 'hello') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProcessAsyncTest, ExecAsyncNonZeroStillResolves) {
    int ret = runScript(
        "import { execAsync } from 'stdiolink/process';\n"
        "const r = await execAsync(__stub, ['--mode=stdout', '--exit-code=42']);\n"
        "globalThis.ok = (r.exitCode === 42) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProcessAsyncTest, ExecAsyncCapturesStderr) {
    int ret = runScript(
        "import { execAsync } from 'stdiolink/process';\n"
        "const r = await execAsync(__stub, ['--mode=stderr', '--text=oops']);\n"
        "globalThis.ok = (r.stderr.trim() === 'oops') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProcessAsyncTest, ExecAsyncWithInput) {
    int ret = runScript(
        "import { execAsync } from 'stdiolink/process';\n"
        "const r = await execAsync(__stub, ['--mode=echo'],\n"
        "  { input: 'ping' });\n"
        "globalThis.ok = (r.stdout.trim() === 'ping') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProcessAsyncTest, ExecAsyncTimeoutRejects) {
    int ret = runScript(
        "import { execAsync } from 'stdiolink/process';\n"
        "try {\n"
        "  await execAsync(__stub, ['--mode=sleep', '--sleep-ms=5000'],\n"
        "    { timeoutMs: 200 });\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = 1;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProcessAsyncTest, ExecAsyncMissingProgramRejects) {
    int ret = runScript(
        "import { execAsync } from 'stdiolink/process';\n"
        "try {\n"
        "  await execAsync('/nonexistent_binary_xyz');\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = 1;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── spawn ──

TEST_F(JsProcessAsyncTest, SpawnOnStdoutReceivesChunks) {
    int ret = runScript(
        "import { spawn } from 'stdiolink/process';\n"
        "const p = spawn(__stub, ['--mode=stdout', '--text=chunk1']);\n"
        "let got = '';\n"
        "p.onStdout((c) => { got += c; });\n"
        "p.onExit(() => {\n"
        "  globalThis.ok = got.trim() === 'chunk1' ? 1 : 0;\n"
        "});\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProcessAsyncTest, SpawnOnExitTriggeredOnce) {
    int ret = runScript(
        "import { spawn } from 'stdiolink/process';\n"
        "const p = spawn(__stub, ['--mode=stdout']);\n"
        "let count = 0;\n"
        "p.onExit(() => { count++; });\n"
        "p.onExit(() => {\n"
        "  globalThis.ok = count === 1 ? 1 : 0;\n"
        "});\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProcessAsyncTest, SpawnWriteAndCloseStdin) {
    int ret = runScript(
        "import { spawn } from 'stdiolink/process';\n"
        "const p = spawn(__stub, ['--mode=echo']);\n"
        "let got = '';\n"
        "p.onStdout((c) => { got += c; });\n"
        "p.write('hello\\n');\n"
        "p.closeStdin();\n"
        "p.onExit(() => {\n"
        "  globalThis.ok = got.trim() === 'hello' ? 1 : 0;\n"
        "});\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProcessAsyncTest, SpawnKillTerminatesProcess) {
    int ret = runScript(
        "import { spawn } from 'stdiolink/process';\n"
        "const p = spawn(__stub, ['--mode=sleep', '--sleep-ms=10000']);\n"
        "p.onExit((e) => {\n"
        "  globalThis.ok = 1;\n"
        "});\n"
        "p.kill();\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Unknown option validation ──

TEST_F(JsProcessAsyncTest, ExecAsyncUnknownOptionThrows) {
    int ret = runScript(
        "import { execAsync } from 'stdiolink/process';\n"
        "try {\n"
        "  await execAsync(__stub, [], { badOption: true });\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = (e instanceof TypeError) ? 1 : 0;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProcessAsyncTest, SpawnUnknownOptionThrows) {
    int ret = runScript(
        "import { spawn } from 'stdiolink/process';\n"
        "try {\n"
        "  spawn(__stub, [], { badOption: true });\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = (e instanceof TypeError) ? 1 : 0;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Output buffer overflow ──

TEST_F(JsProcessAsyncTest, M72_R10_ExecAsyncOutputOverflowRejects) {
    const QString floodStub = QCoreApplication::applicationDirPath() + "/test_output_flood_stub";
    if (!QFileInfo::exists(floodStub)) {
        GTEST_SKIP() << "test_output_flood_stub not found";
    }
    // Request 9MB of stdout output (exceeds 8MB limit)
    int ret = runScript(
        QString(
            "import { execAsync } from 'stdiolink/process';\n"
            "try {\n"
            "  await execAsync('%1', ['--flood-stdout=9437184']);\n"
            "  globalThis.ok = 0;\n"
            "} catch (e) {\n"
            "  const msg = (typeof e === 'string') ? e : (e.message || '');\n"
            "  globalThis.ok = msg.includes('overflow') ? 1 : 0;\n"
            "}\n"
        ).arg(floodStub).toStdString().c_str()
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// ── Post-exit onExit registration ──

TEST_F(JsProcessAsyncTest, SpawnOnExitAfterExitFiresImmediately) {
    int ret = runScript(
        "import { spawn } from 'stdiolink/process';\n"
        "const p = spawn(__stub, ['--mode=stdout', '--text=hi']);\n"
        "p.onExit(() => {\n"
        "  p.onExit((e) => {\n"
        "    globalThis.ok = (e.exitCode === 0) ? 1 : 0;\n"
        "  });\n"
        "});\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}
