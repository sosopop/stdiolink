#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include "bindings/js_driver_resolve.h"
#include "bindings/js_driver_resolve_binding.h"
#include "bindings/js_constants.h"
#include "bindings/js_config.h"
#include "bindings/js_stdiolink_module.h"
#include "engine/js_engine.h"
#include "engine/console_bridge.h"

using namespace stdiolink_service;

// ── C++ resolveDriverPath Tests ──

class DriverResolveTest : public ::testing::Test {
protected:
    QTemporaryDir m_tmpDir;
    QString ext() {
#ifdef Q_OS_WIN
        return ".exe";
#else
        return "";
#endif
    }
    void createFakeDriver(const QString& dir, const QString& name) {
        QDir().mkpath(dir);
        QFile f(dir + "/" + name + ext());
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.close();
#ifndef Q_OS_WIN
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
#endif
    }
};

// T06 — dataRoot/drivers 下命中
TEST_F(DriverResolveTest, T06_HitInDataRootDrivers) {
    QString dataRoot = m_tmpDir.path() + "/data";
    createFakeDriver(dataRoot + "/drivers/my_drv", "stdio.drv.calc");
    auto r = resolveDriverPath("stdio.drv.calc", dataRoot, "/nonexist");
    EXPECT_FALSE(r.path.isEmpty());
    EXPECT_TRUE(QFileInfo::exists(r.path));
    EXPECT_TRUE(r.path.contains("my_drv"));
}

// T07 — dataRoot 无匹配，appDir 命中
TEST_F(DriverResolveTest, T07_FallbackToAppDir) {
    QString dataRoot = m_tmpDir.path() + "/data";
    QDir().mkpath(dataRoot + "/drivers");
    QString appDir = m_tmpDir.path() + "/app";
    createFakeDriver(appDir, "stdio.drv.calc");
    auto r = resolveDriverPath("stdio.drv.calc", dataRoot, appDir);
    EXPECT_FALSE(r.path.isEmpty());
    EXPECT_TRUE(r.path.contains("app"));
}

// T08 — 前两级无匹配，CWD 命中
TEST_F(DriverResolveTest, T08_FallbackToCwd) {
    QTemporaryDir cwdDir;
    ASSERT_TRUE(cwdDir.isValid());
    QString origCwd = QDir::currentPath();
    QDir::setCurrent(cwdDir.path());

    createFakeDriver(cwdDir.path(), "stdio.drv.cwdtest");
    auto r = resolveDriverPath("stdio.drv.cwdtest", "/nonexist", "/nonexist");
    EXPECT_FALSE(r.path.isEmpty());
    EXPECT_TRUE(r.path.startsWith(cwdDir.path()));

    QDir::setCurrent(origCwd);
}

// T09 — 三级均未命中
TEST_F(DriverResolveTest, T09_AllMiss) {
    auto r = resolveDriverPath("stdio.drv.ghost",
        m_tmpDir.path(), m_tmpDir.path() + "/app");
    EXPECT_TRUE(r.path.isEmpty());
    EXPECT_EQ(r.searchedPaths.size(), 3);
}

// T10 — dataRoot 为空时跳过第 1 级
TEST_F(DriverResolveTest, T10_EmptyDataRootSkipsLevel1) {
    auto r = resolveDriverPath("stdio.drv.none",
        "", m_tmpDir.path() + "/app");
    EXPECT_TRUE(r.path.isEmpty());
    EXPECT_EQ(r.searchedPaths.size(), 2);
}

// T14 — driverName 含路径分隔符被拒绝
TEST_F(DriverResolveTest, T14_PathSeparatorRejected) {
    auto r = resolveDriverPath("../etc/passwd",
        m_tmpDir.path(), m_tmpDir.path());
    EXPECT_TRUE(r.path.isEmpty());
}

// T15 — driverName 带 .exe 后缀被拒绝
TEST_F(DriverResolveTest, T15_ExeSuffixRejected) {
    createFakeDriver(m_tmpDir.path() + "/app", "stdio.drv.calc");
    auto r = resolveDriverPath("stdio.drv.calc.exe",
        "", m_tmpDir.path() + "/app");
    EXPECT_TRUE(r.path.isEmpty());
}

#ifndef Q_OS_WIN
// T16 — Unix 下文件无执行权限被跳过
TEST_F(DriverResolveTest, T16_NonExecutableSkipped) {
    QString appDir = m_tmpDir.path() + "/app";
    QDir().mkpath(appDir);
    QFile f(appDir + "/stdio.drv.noexec");
    f.open(QIODevice::WriteOnly);
    f.close();
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    auto r = resolveDriverPath("stdio.drv.noexec", "", appDir);
    EXPECT_TRUE(r.path.isEmpty());
}
#endif

// ── JS resolveDriver() Binding Tests ──

namespace {

QString writeScript(const QTemporaryDir& dir, const QString& name,
                    const QString& content) {
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

class JsDriverResolveTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
        ASSERT_NE(m_engine->context(), nullptr);
        ConsoleBridge::install(m_engine->context());

        JsConfigBinding::attachRuntime(m_engine->runtime());
        JsConstantsBinding::attachRuntime(m_engine->runtime());
        JsDriverResolveBinding::attachRuntime(m_engine->runtime());

        m_engine->registerModule("stdiolink", jsInitStdiolinkModule);
        m_engine->registerModule("stdiolink/constants",
                                 JsConstantsBinding::initModule);
        m_engine->registerModule("stdiolink/driver",
                                 JsDriverResolveBinding::initModule);
    }

    void TearDown() override {
        JsConstantsBinding::reset(m_engine->context());
        JsConfigBinding::reset(m_engine->context());
        m_engine.reset();
    }

    int runScript(const QString& code) {
        QString path = writeScript(m_tmpDir, "test.mjs", code);
        EXPECT_FALSE(path.isEmpty());
        return m_engine->evalFile(path);
    }

    QString ext() {
#ifdef Q_OS_WIN
        return ".exe";
#else
        return "";
#endif
    }

    void createFakeDriver(const QString& dir, const QString& name) {
        QDir().mkpath(dir);
        QFile f(dir + "/" + name + ext());
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.close();
#ifndef Q_OS_WIN
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner
                         | QFile::ExeOwner);
#endif
    }

    QTemporaryDir m_tmpDir;
    std::unique_ptr<JsEngine> m_engine;
};

// T11 — JS resolveDriver() 正常命中
TEST_F(JsDriverResolveTest, T11_JsResolveHit) {
    QString appDir = m_tmpDir.path() + "/app";
    createFakeDriver(appDir, "stdio.drv.calc");
    JsConstantsBinding::setPathContext(m_engine->context(), {
        "/usr/bin/svc", appDir, "/tmp", "/srv", "/srv/index.js",
        "/srv", "/tmp", "/home", ""
    });
    int ret = runScript(
        "import { resolveDriver } from 'stdiolink/driver';\n"
        "const p = resolveDriver('stdio.drv.calc');\n"
        "globalThis.ok = (typeof p === 'string' && p.length > 0) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// T12 — JS resolveDriver() 未命中抛出 Error
TEST_F(JsDriverResolveTest, T12_JsResolveNotFound) {
    JsConstantsBinding::setPathContext(m_engine->context(), {
        "/usr/bin/svc", "/empty", "/tmp", "/srv", "/srv/index.js",
        "/srv", "/tmp", "/home", ""
    });
    int ret = runScript(
        "import { resolveDriver } from 'stdiolink/driver';\n"
        "try {\n"
        "  resolveDriver('stdio.drv.nonexist');\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = (e.message.includes('driver not found')"
        " && e.message.includes('searched')) ? 1 : 0;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// T13 — JS resolveDriver() 空字符串参数抛出 TypeError
TEST_F(JsDriverResolveTest, T13_JsResolveEmptyString) {
    int ret = runScript(
        "import { resolveDriver } from 'stdiolink/driver';\n"
        "try {\n"
        "  resolveDriver('');\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = e.message.includes('non-empty string') ? 1 : 0;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// T17 — JS resolveDriver() driverName 含路径分隔符抛出 TypeError
TEST_F(JsDriverResolveTest, T17_JsResolvePathSeparator) {
    int ret = runScript(
        "import { resolveDriver } from 'stdiolink/driver';\n"
        "try {\n"
        "  resolveDriver('../etc/passwd');\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = e.message.includes('path separators') ? 1 : 0;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}

// T18 — JS resolveDriver() driverName 带 .exe 后缀抛出 TypeError
TEST_F(JsDriverResolveTest, T18_JsResolveExeSuffix) {
    int ret = runScript(
        "import { resolveDriver } from 'stdiolink/driver';\n"
        "try {\n"
        "  resolveDriver('stdio.drv.calc.exe');\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = e.message.includes('.exe') ? 1 : 0;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}
