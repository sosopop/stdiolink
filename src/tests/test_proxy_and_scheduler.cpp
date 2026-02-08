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

        ConsoleBridge::install(m_engine->context());
        m_engine->registerModule("stdiolink", jsInitStdiolinkModule);
        JsTaskScheduler::installGlobal(m_engine->context(), m_scheduler.get());
    }

    int runScript(const QString& path) {
        int ret = m_engine->evalFile(path);
        while (m_scheduler->hasPending() || m_engine->hasPendingJobs()) {
            if (m_scheduler->hasPending()) {
                m_scheduler->poll(50);
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
    QTemporaryDir m_tmpDir;
};

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
