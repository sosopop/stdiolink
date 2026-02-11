#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include <quickjs.h>
#include "bindings/js_stdiolink_module.h"
#include "bindings/js_task_scheduler.h"
#include "bindings/js_wait_any_scheduler.h"
#include "engine/console_bridge.h"
#include "engine/js_engine.h"
#include "stdiolink/platform/platform_utils.h"

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

int readGlobalInt(JSContext* ctx, const char* key) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue val = JS_GetPropertyStr(ctx, global, key);
    int32_t out = 0;
    JS_ToInt32(ctx, &out, val);
    JS_FreeValue(ctx, val);
    JS_FreeValue(ctx, global);
    return out;
}

QString escapeJsString(const QString& s) {
    QString out = s;
    out.replace("\\", "/");
    out.replace("'", "\\'");
    return out;
}

QString calculatorDriverPath() {
    return stdiolink::PlatformUtils::executablePath(
        QCoreApplication::applicationDirPath(), "calculator_driver");
}

QString slowMetaDriverPath() {
    return stdiolink::PlatformUtils::executablePath(
        QCoreApplication::applicationDirPath(), "test_slow_meta_driver");
}

} // namespace

TEST(JsTaskSchedulerTest, InitiallyEmpty) {
    JsEngine engine;
    ASSERT_NE(engine.context(), nullptr);
    JsTaskScheduler scheduler(engine.context());
    EXPECT_FALSE(scheduler.hasPending());
}

TEST(JsTaskSchedulerTest, PollEmptyReturnsFalse) {
    JsEngine engine;
    ASSERT_NE(engine.context(), nullptr);
    JsTaskScheduler scheduler(engine.context());
    EXPECT_FALSE(scheduler.poll(10));
}

class JsProxyTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
        ASSERT_NE(m_engine->context(), nullptr);
        m_scheduler = std::make_unique<JsTaskScheduler>(m_engine->context());
        m_waitAnyScheduler = std::make_unique<WaitAnyScheduler>(m_engine->context());

        ConsoleBridge::install(m_engine->context());
        m_engine->registerModule("stdiolink", jsInitStdiolinkModule);
        JsTaskScheduler::installGlobal(m_engine->context(), m_scheduler.get());
        WaitAnyScheduler::installGlobal(m_engine->context(), m_waitAnyScheduler.get());
    }

    int runScript(const QString& path) {
        int ret = m_engine->evalFile(path);
        while (m_scheduler->hasPending() || m_waitAnyScheduler->hasPending()
               || m_engine->hasPendingJobs()) {
            if (m_scheduler->hasPending()) {
                m_scheduler->poll(50);
            }
            if (m_waitAnyScheduler->hasPending()) {
                m_waitAnyScheduler->poll(50);
            }
            while (m_engine->hasPendingJobs()) {
                m_engine->executePendingJobs();
            }
        }
        if (ret == 0 && m_engine->hadJobError()) {
            ret = 1;
        }
        return ret;
    }

    std::unique_ptr<JsEngine> m_engine;
    std::unique_ptr<JsTaskScheduler> m_scheduler;
    std::unique_ptr<WaitAnyScheduler> m_waitAnyScheduler;
    QTemporaryDir m_tmpDir;
};

TEST_F(JsProxyTest, ImportWaitAny) {
    const QString scriptPath =
        writeScript(m_tmpDir, "import_wait_any.js",
                    "import { waitAny } from 'stdiolink';\n"
                    "globalThis.ok = (typeof waitAny === 'function') ? 1 : 0;\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProxyTest, WaitAnyEmptyTasksReturnsNull) {
    const QString scriptPath =
        writeScript(m_tmpDir, "wait_any_empty.js",
                    "import { waitAny } from 'stdiolink';\n"
                    "(async () => {\n"
                    "  const result = await waitAny([]);\n"
                    "  globalThis.ok = (result === null) ? 1 : 0;\n"
                    "})();\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProxyTest, WaitAnyPreservesEventAndDone) {
    const QString driverPath = calculatorDriverPath();
    ASSERT_TRUE(QFileInfo::exists(driverPath));

    const QString scriptPath = writeScript(
        m_tmpDir, "wait_any_event_done.js",
        QString("import { openDriver, waitAny } from 'stdiolink';\n"
                "(async () => {\n"
                "  const calc = await openDriver('%1');\n"
                "  const task = calc.$rawRequest('batch', {\n"
                "    operations: [\n"
                "      { type: 'add', a: 1, b: 2 },\n"
                "      { type: 'mul', a: 3, b: 4 }\n"
                "    ]\n"
                "  });\n"
                "  const first = await waitAny([task], 5000);\n"
                "  let gotEvent = !!(first && first.taskIndex === 0 && first.msg && first.msg.status === "
                "'event');\n"
                "  let gotDone = false;\n"
                "  while (true) {\n"
                "    const result = await waitAny([task], 5000);\n"
                "    if (!result) {\n"
                "      break;\n"
                "    }\n"
                "    if (result.msg && result.msg.status === 'done') {\n"
                "      gotDone = true;\n"
                "      break;\n"
                "    }\n"
                "  }\n"
                "  globalThis.ok = (gotEvent && gotDone) ? 1 : 0;\n"
                "  calc.$close();\n"
                "})();\n")
            .arg(escapeJsString(driverPath)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProxyTest, WaitAnyConflictRejectsSamePendingTask) {
    const QString driverPath = calculatorDriverPath();
    ASSERT_TRUE(QFileInfo::exists(driverPath));

    const QString scriptPath = writeScript(
        m_tmpDir, "wait_any_conflict.js",
        QString("import { openDriver, waitAny } from 'stdiolink';\n"
                "(async () => {\n"
                "  const calc = await openDriver('%1');\n"
                "  const task = calc.$rawRequest('batch', {\n"
                "    operations: [\n"
                "      { type: 'add', a: 1, b: 2 },\n"
                "      { type: 'sub', a: 9, b: 4 }\n"
                "    ]\n"
                "  });\n"
                "  const p1 = waitAny([task], 5000);\n"
                "  let conflict = 0;\n"
                "  try {\n"
                "    await waitAny([task], 5000);\n"
                "  } catch (e) {\n"
                "    conflict = 1;\n"
                "  }\n"
                "  const first = await p1;\n"
                "  globalThis.ok = (conflict === 1 && first && first.msg) ? 1 : 0;\n"
                "  calc.$close();\n"
                "})();\n")
            .arg(escapeJsString(driverPath)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProxyTest, ImportOpenDriver) {
    const QString scriptPath =
        writeScript(m_tmpDir, "import_open_driver.js",
                    "import { openDriver } from 'stdiolink';\n"
                    "(async () => {\n"
                    "  globalThis.ok = (typeof openDriver === 'function') ? 1 : 0;\n"
                    "})();\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProxyTest, OpenDriverStartFail) {
    const QString scriptPath = writeScript(m_tmpDir, "open_driver_fail.js",
                                           "import { openDriver } from 'stdiolink';\n"
                                           "(async () => {\n"
                                           "  try {\n"
                                           "    await openDriver('__nonexistent_driver__');\n"
                                           "    globalThis.caught = 0;\n"
                                           "  } catch (e) {\n"
                                           "    globalThis.caught = 1;\n"
                                           "  }\n"
                                           "})();\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "caught"), 1);
}

TEST_F(JsProxyTest, ProxyCommandCall) {
    const QString driverPath = calculatorDriverPath();
    ASSERT_TRUE(QFileInfo::exists(driverPath));

    const QString scriptPath =
        writeScript(m_tmpDir, "proxy_command.js",
                    QString("import { openDriver } from 'stdiolink';\n"
                            "(async () => {\n"
                            "  const calc = await openDriver('%1');\n"
                            "  const r = await calc.add({ a: 5, b: 3 });\n"
                            "  globalThis.ok = (r && r.result === 8) ? 1 : 0;\n"
                            "  calc.$close();\n"
                            "})();\n")
                        .arg(escapeJsString(driverPath)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProxyTest, ProxyReservedFieldsAndUndefinedCommand) {
    const QString driverPath = calculatorDriverPath();
    ASSERT_TRUE(QFileInfo::exists(driverPath));

    const QString scriptPath = writeScript(
        m_tmpDir, "proxy_fields.js",
        QString("import { openDriver } from 'stdiolink';\n"
                "(async () => {\n"
                "  const calc = await openDriver('%1');\n"
                "  globalThis.hasMeta = (calc.$meta && calc.$meta.commands) ? 1 : 0;\n"
                "  globalThis.hasDriver = (calc.$driver && typeof calc.$driver.request === "
                "'function') ? 1 : 0;\n"
                "  const t = calc.$rawRequest('add', { a: 1, b: 2 });\n"
                "  const m = t.waitNext(5000);\n"
                "  globalThis.rawOk = (m && m.status === 'done') ? 1 : 0;\n"
                "  globalThis.undefinedCmd = (calc.not_exist_cmd === undefined) ? 1 : 0;\n"
                "  calc.$close();\n"
                "})();\n")
            .arg(escapeJsString(driverPath)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "hasMeta"), 1);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "hasDriver"), 1);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "rawOk"), 1);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "undefinedCmd"), 1);
}

TEST_F(JsProxyTest, SameInstanceConcurrentThrowsBusy) {
    const QString driverPath = calculatorDriverPath();
    ASSERT_TRUE(QFileInfo::exists(driverPath));

    const QString scriptPath = writeScript(
        m_tmpDir, "proxy_busy.js",
        QString("import { openDriver } from 'stdiolink';\n"
                "(async () => {\n"
                "  const calc = await openDriver('%1');\n"
                "  let busyCaught = 0;\n"
                "  const p1 = calc.add({ a: 1, b: 2 });\n"
                "  try {\n"
                "    calc.subtract({ a: 3, b: 1 });\n"
                "  } catch (e) {\n"
                "    if (String(e).includes('DriverBusyError')) busyCaught = 1;\n"
                "  }\n"
                "  const r1 = await p1;\n"
                "  globalThis.ok = (busyCaught === 1 && r1 && r1.result === 3) ? 1 : 0;\n"
                "  calc.$close();\n"
                "})();\n")
            .arg(escapeJsString(driverPath)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProxyTest, DifferentInstancesCanRunInParallel) {
    const QString driverPath = calculatorDriverPath();
    ASSERT_TRUE(QFileInfo::exists(driverPath));

    const QString scriptPath = writeScript(
        m_tmpDir, "proxy_parallel.js",
        QString("import { openDriver } from 'stdiolink';\n"
                "(async () => {\n"
                "  const a = await openDriver('%1');\n"
                "  const b = await openDriver('%1');\n"
                "  const rs = await Promise.all([\n"
                "    a.add({ a: 1, b: 2 }),\n"
                "    b.add({ a: 3, b: 4 })\n"
                "  ]);\n"
                "  globalThis.ok = (rs[0].result === 3 && rs[1].result === 7) ? 1 : 0;\n"
                "  a.$close();\n"
                "  b.$close();\n"
                "})();\n")
            .arg(escapeJsString(driverPath)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProxyTest, DriverErrorBecomesThrow) {
    const QString driverPath = calculatorDriverPath();
    ASSERT_TRUE(QFileInfo::exists(driverPath));

    const QString scriptPath = writeScript(m_tmpDir, "proxy_error.js",
                                           QString("import { openDriver } from 'stdiolink';\n"
                                                   "(async () => {\n"
                                                   "  const calc = await openDriver('%1');\n"
                                                   "  let caught = 0;\n"
                                                   "  try {\n"
                                                   "    await calc.divide({ a: 1, b: 0 });\n"
                                                   "  } catch (e) {\n"
                                                   "    caught = 1;\n"
                                                   "  }\n"
                                                   "  globalThis.ok = caught;\n"
                                                   "  calc.$close();\n"
                                                   "})();\n")
                                               .arg(escapeJsString(driverPath)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProxyTest, CloseTerminatesDriver) {
    const QString driverPath = calculatorDriverPath();
    ASSERT_TRUE(QFileInfo::exists(driverPath));

    const QString scriptPath =
        writeScript(m_tmpDir, "proxy_close.js",
                    QString("import { openDriver } from 'stdiolink';\n"
                            "(async () => {\n"
                            "  const calc = await openDriver('%1');\n"
                            "  globalThis.runningBefore = calc.$driver.running ? 1 : 0;\n"
                            "  calc.$close();\n"
                            "  globalThis.runningAfter = calc.$driver.running ? 1 : 0;\n"
                            "})();\n")
                        .arg(escapeJsString(driverPath)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "runningBefore"), 1);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "runningAfter"), 0);
}

// ── M48: profilePolicy tests ──

TEST_F(JsProxyTest, ProfilePolicyAutoInjectsKeepaliveWhenMissing) {
    const QString driverPath = calculatorDriverPath();
    ASSERT_TRUE(QFileInfo::exists(driverPath));

    const QString scriptPath = writeScript(
        m_tmpDir, "profile_auto_inject.js",
        QString("import { openDriver } from 'stdiolink';\n"
                "(async () => {\n"
                "  const calc = await openDriver('%1');\n"
                "  const r1 = await calc.add({ a: 1, b: 2 });\n"
                "  const r2 = await calc.add({ a: 3, b: 4 });\n"
                "  globalThis.ok = (r1.result === 3 && r2.result === 7) ? 1 : 0;\n"
                "  calc.$close();\n"
                "})();\n")
            .arg(escapeJsString(driverPath)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

TEST_F(JsProxyTest, ProfilePolicyForceKeepaliveOverridesExistingProfile) {
    const QString driverPath = calculatorDriverPath();
    ASSERT_TRUE(QFileInfo::exists(driverPath));

    const QString scriptPath = writeScript(
        m_tmpDir, "profile_force_keepalive.js",
        QString("import { openDriver } from 'stdiolink';\n"
                "(async () => {\n"
                "  const calc = await openDriver('%1', ['--profile=oneshot'], {\n"
                "    profilePolicy: 'force-keepalive'\n"
                "  });\n"
                "  const r1 = await calc.add({ a: 1, b: 2 });\n"
                "  const r2 = await calc.add({ a: 3, b: 4 });\n"
                "  globalThis.forceOk = (r1.result === 3 && r2.result === 7) ? 1 : 0;\n"
                "  calc.$close();\n"
                "})();\n")
            .arg(escapeJsString(driverPath)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "forceOk"), 1);
}

TEST_F(JsProxyTest, ProfilePolicyPreservePassesArgsThroughUnchanged) {
    const QString driverPath = calculatorDriverPath();
    ASSERT_TRUE(QFileInfo::exists(driverPath));

    // preserve with explicit --profile=keepalive: args pass through unchanged,
    // so two commands should succeed (proving preserve didn't strip the profile).
    const QString scriptPath = writeScript(
        m_tmpDir, "profile_preserve.js",
        QString("import { openDriver } from 'stdiolink';\n"
                "(async () => {\n"
                "  const calc = await openDriver('%1', ['--profile=keepalive'], {\n"
                "    profilePolicy: 'preserve'\n"
                "  });\n"
                "  const r1 = await calc.add({ a: 1, b: 2 });\n"
                "  const r2 = await calc.add({ a: 3, b: 4 });\n"
                "  globalThis.preserveOk = (r1.result === 3 && r2.result === 7) ? 1 : 0;\n"
                "  calc.$close();\n"
                "})();\n")
            .arg(escapeJsString(driverPath)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "preserveOk"), 1);
}

// ── M48: metaTimeoutMs tests ──

TEST_F(JsProxyTest, MetaTimeoutMsCustomValueWorks) {
    const QString driverPath = calculatorDriverPath();
    ASSERT_TRUE(QFileInfo::exists(driverPath));

    const QString scriptPath = writeScript(
        m_tmpDir, "meta_timeout_custom.js",
        QString("import { openDriver } from 'stdiolink';\n"
                "(async () => {\n"
                "  const calc = await openDriver('%1', [], { metaTimeoutMs: 10000 });\n"
                "  const r = await calc.add({ a: 10, b: 20 });\n"
                "  globalThis.metaOk = (r.result === 30) ? 1 : 0;\n"
                "  calc.$close();\n"
                "})();\n")
            .arg(escapeJsString(driverPath)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "metaOk"), 1);
}

TEST_F(JsProxyTest, MetaTimeoutMsTooSmallCausesOpenDriverReject) {
    const QString driverPath = slowMetaDriverPath();
    ASSERT_TRUE(QFileInfo::exists(driverPath));

    const QString scriptPath = writeScript(
        m_tmpDir, "meta_timeout_reject.js",
        QString("import { openDriver } from 'stdiolink';\n"
                "(async () => {\n"
                "  let caught = 0;\n"
                "  try {\n"
                "    await openDriver('%1', ['--meta-delay-ms=3000'], {\n"
                "      metaTimeoutMs: 100\n"
                "    });\n"
                "  } catch (e) {\n"
                "    caught = String(e).includes('metadata') || String(e).includes('timeoutMs') ? 1 : 0;\n"
                "  }\n"
                "  globalThis.timeoutCaught = caught;\n"
                "})();\n")
            .arg(escapeJsString(driverPath)));
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "timeoutCaught"), 1);
}

// ── M48: parameter validation tests ──

TEST_F(JsProxyTest, OpenDriverOptionsNotObjectThrowsTypeError) {
    const QString scriptPath = writeScript(
        m_tmpDir, "opts_not_object.js",
        "import { openDriver } from 'stdiolink';\n"
        "(async () => {\n"
        "  let caught = 0;\n"
        "  try {\n"
        "    await openDriver('dummy', [], 'bad');\n"
        "  } catch (e) {\n"
        "    caught = (e instanceof TypeError) ? 1 : 0;\n"
        "  }\n"
        "  globalThis.optsErr = caught;\n"
        "})();\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "optsErr"), 1);
}

TEST_F(JsProxyTest, OpenDriverOptionsUnknownKeyThrowsTypeError) {
    const QString scriptPath = writeScript(
        m_tmpDir, "opts_unknown_key.js",
        "import { openDriver } from 'stdiolink';\n"
        "(async () => {\n"
        "  let caught = 0;\n"
        "  try {\n"
        "    await openDriver('dummy', [], { foo: 1 });\n"
        "  } catch (e) {\n"
        "    caught = (e instanceof TypeError && String(e).includes('foo')) ? 1 : 0;\n"
        "  }\n"
        "  globalThis.unknownKey = caught;\n"
        "})();\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "unknownKey"), 1);
}

TEST_F(JsProxyTest, OpenDriverInvalidProfilePolicyThrowsTypeError) {
    const QString scriptPath = writeScript(
        m_tmpDir, "opts_bad_profile.js",
        "import { openDriver } from 'stdiolink';\n"
        "(async () => {\n"
        "  let caught = 0;\n"
        "  try {\n"
        "    await openDriver('dummy', [], { profilePolicy: 'bogus' });\n"
        "  } catch (e) {\n"
        "    caught = (e instanceof TypeError) ? 1 : 0;\n"
        "  }\n"
        "  globalThis.badProfile = caught;\n"
        "})();\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "badProfile"), 1);
}

TEST_F(JsProxyTest, OpenDriverInvalidMetaTimeoutThrowsRangeError) {
    const QString scriptPath = writeScript(
        m_tmpDir, "opts_bad_timeout.js",
        "import { openDriver } from 'stdiolink';\n"
        "(async () => {\n"
        "  let negCaught = 0, nanCaught = 0, fracCaught = 0;\n"
        "  try { await openDriver('d', [], { metaTimeoutMs: -1 }); }\n"
        "  catch (e) { negCaught = (e instanceof RangeError) ? 1 : 0; }\n"
        "  try { await openDriver('d', [], { metaTimeoutMs: NaN }); }\n"
        "  catch (e) { nanCaught = (e instanceof RangeError) ? 1 : 0; }\n"
        "  try { await openDriver('d', [], { metaTimeoutMs: 1.5 }); }\n"
        "  catch (e) { fracCaught = (e instanceof RangeError) ? 1 : 0; }\n"
        "  globalThis.negCaught = negCaught;\n"
        "  globalThis.nanCaught = nanCaught;\n"
        "  globalThis.fracCaught = fracCaught;\n"
        "})();\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "negCaught"), 1);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "nanCaught"), 1);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "fracCaught"), 1);
}

TEST_F(JsProxyTest, OpenDriverArgsNotArrayThrowsTypeError) {
    const QString scriptPath = writeScript(
        m_tmpDir, "args_not_array.js",
        "import { openDriver } from 'stdiolink';\n"
        "(async () => {\n"
        "  let caught = 0;\n"
        "  try {\n"
        "    await openDriver('dummy', 123);\n"
        "  } catch (e) {\n"
        "    caught = (e instanceof TypeError) ? 1 : 0;\n"
        "  }\n"
        "  globalThis.argsErr = caught;\n"
        "})();\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "argsErr"), 1);
}

TEST_F(JsProxyTest, OpenDriverMetaTimeoutMsNotNumberThrowsTypeError) {
    const QString scriptPath = writeScript(
        m_tmpDir, "timeout_not_number.js",
        "import { openDriver } from 'stdiolink';\n"
        "(async () => {\n"
        "  let caught = 0;\n"
        "  try {\n"
        "    await openDriver('dummy', [], { metaTimeoutMs: 'fast' });\n"
        "  } catch (e) {\n"
        "    caught = (e instanceof TypeError) ? 1 : 0;\n"
        "  }\n"
        "  globalThis.typeErr = caught;\n"
        "})();\n");
    ASSERT_FALSE(scriptPath.isEmpty());

    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "typeErr"), 1);
}
