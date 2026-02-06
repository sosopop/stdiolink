#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include "engine/console_bridge.h"
#include "engine/js_engine.h"
#include "engine/module_loader.h"
#include "quickjs.h"

namespace {

QString writeFile(const QTemporaryDir& dir, const QString& relativePath, const QString& content) {
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

QString escapeForSingleQuotedJs(const QString& text) {
    QString escaped = text;
    escaped.replace("\\", "\\\\");
    escaped.replace("'", "\\'");
    return escaped;
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

int initBuiltinMagic(JSContext* ctx, JSModuleDef* module) {
    JSValue v = JS_NewInt32(ctx, 999);
    if (JS_SetModuleExport(ctx, module, "MAGIC", v) < 0) {
        JS_FreeValue(ctx, v);
        return -1;
    }
    return 0;
}

JSModuleDef* createBuiltinMagic(JSContext* ctx, const char* name) {
    JSModuleDef* module = JS_NewCModule(ctx, name, initBuiltinMagic);
    if (!module) {
        return nullptr;
    }
    if (JS_AddModuleExport(ctx, module, "MAGIC") < 0) {
        return nullptr;
    }
    return module;
}

} // namespace

class EsModuleLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
        ConsoleBridge::install(m_engine->context());
    }

    int evalAndDrain(const QString& scriptPath) {
        const int ret = m_engine->evalFile(scriptPath);
        while (m_engine->hasPendingJobs()) {
            m_engine->executePendingJobs();
        }
        return ret;
    }

    std::unique_ptr<JsEngine> m_engine;
    QTemporaryDir m_tmpDir;
};

TEST_F(EsModuleLoaderTest, ImportRelativePath) {
    writeFile(m_tmpDir, "lib/math.js", "export function square(x) { return x * x; }\n");
    const QString mainPath = writeFile(
        m_tmpDir,
        "main.js",
        "import { square } from './lib/math.js';\n"
        "globalThis.result = square(4);\n");
    ASSERT_FALSE(mainPath.isEmpty());

    EXPECT_EQ(evalAndDrain(mainPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "result"), 16);
}

TEST_F(EsModuleLoaderTest, ImportParentPath) {
    writeFile(m_tmpDir, "shared/utils.js", "export function double(x) { return x * 2; }\n");
    const QString mainPath = writeFile(
        m_tmpDir,
        "app/main.js",
        "import { double } from '../shared/utils.js';\n"
        "globalThis.result = double(5);\n");
    ASSERT_FALSE(mainPath.isEmpty());

    EXPECT_EQ(evalAndDrain(mainPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "result"), 10);
}

TEST_F(EsModuleLoaderTest, ImportAbsolutePath) {
    const QString libPath = writeFile(m_tmpDir, "lib/value.js", "export const VALUE = 42;\n");
    ASSERT_FALSE(libPath.isEmpty());

    const QString mainPath = writeFile(
        m_tmpDir,
        "main.js",
        QString("import { VALUE } from '%1';\n"
                "globalThis.result = VALUE;\n")
            .arg(QDir::fromNativeSeparators(libPath)));
    ASSERT_FALSE(mainPath.isEmpty());

    EXPECT_EQ(evalAndDrain(mainPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "result"), 42);
}

TEST_F(EsModuleLoaderTest, ImportNonexistentFileFails) {
    const QString mainPath = writeFile(
        m_tmpDir,
        "main.js",
        "import { missing } from './nope.js';\n"
        "globalThis.result = missing;\n");
    ASSERT_FALSE(mainPath.isEmpty());

    EXPECT_NE(evalAndDrain(mainPath), 0);
}

TEST_F(EsModuleLoaderTest, ExportDefaultWorks) {
    writeFile(m_tmpDir, "config.js", "export default { port: 8080 };\n");
    const QString mainPath = writeFile(
        m_tmpDir,
        "main.js",
        "import config from './config.js';\n"
        "globalThis.result = config.port;\n");
    ASSERT_FALSE(mainPath.isEmpty());

    EXPECT_EQ(evalAndDrain(mainPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "result"), 8080);
}

TEST_F(EsModuleLoaderTest, BuiltinModuleIntercept) {
    ModuleLoader::addBuiltin("test_builtin_magic", createBuiltinMagic);
    const QString mainPath = writeFile(
        m_tmpDir,
        "main.js",
        "import { MAGIC } from 'test_builtin_magic';\n"
        "globalThis.result = MAGIC;\n");
    ASSERT_FALSE(mainPath.isEmpty());

    EXPECT_EQ(evalAndDrain(mainPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "result"), 999);
}

TEST_F(EsModuleLoaderTest, ImportWithoutExtensionFails) {
    writeFile(m_tmpDir, "lib/math.js", "export const X = 1;\n");
    const QString mainPath = writeFile(
        m_tmpDir,
        "main.js",
        "import { X } from './lib/math';\n"
        "globalThis.result = X;\n");
    ASSERT_FALSE(mainPath.isEmpty());

    EXPECT_NE(evalAndDrain(mainPath), 0);
}

TEST_F(EsModuleLoaderTest, DirectoryImportIndexJsIsNotAllowed) {
    writeFile(m_tmpDir, "pkg/index.js", "export const X = 2;\n");
    const QString mainPath = writeFile(
        m_tmpDir,
        "main.js",
        "import { X } from './pkg';\n"
        "globalThis.result = X;\n");
    ASSERT_FALSE(mainPath.isEmpty());

    EXPECT_NE(evalAndDrain(mainPath), 0);
}

TEST_F(EsModuleLoaderTest, BareSpecifierRejected) {
    const QString mainPath = writeFile(
        m_tmpDir,
        "main.js",
        "import { X } from 'not_builtin';\n"
        "globalThis.result = X;\n");
    ASSERT_FALSE(mainPath.isEmpty());

    EXPECT_NE(evalAndDrain(mainPath), 0);
}

TEST_F(EsModuleLoaderTest, NormalizedEquivalentRelativePathsLoadOnce) {
    writeFile(
        m_tmpDir,
        "lib/once.js",
        "globalThis.__onceLoads = (globalThis.__onceLoads || 0) + 1;\n"
        "export const VALUE = 7;\n");
    const QString mainPath = writeFile(
        m_tmpDir,
        "main.js",
        "import { VALUE as A } from './lib/once.js';\n"
        "import { VALUE as B } from './lib/../lib/once.js';\n"
        "globalThis.result = A + B;\n"
        "globalThis.loads = globalThis.__onceLoads;\n");
    ASSERT_FALSE(mainPath.isEmpty());

    EXPECT_EQ(evalAndDrain(mainPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "result"), 14);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "loads"), 1);
}

TEST_F(EsModuleLoaderTest, RelativeAndAbsolutePathShareCache) {
    const QString modPath = writeFile(
        m_tmpDir,
        "lib/shared.js",
        "globalThis.__sharedLoads = (globalThis.__sharedLoads || 0) + 1;\n"
        "export const V = 11;\n");
    ASSERT_FALSE(modPath.isEmpty());
    const QString absPath = QDir::fromNativeSeparators(QFileInfo(modPath).absoluteFilePath());

    const QString mainPath = writeFile(
        m_tmpDir,
        "main.js",
        QString(
            "import { V as A } from './lib/shared.js';\n"
            "import { V as B } from '%1';\n"
            "globalThis.result = A + B;\n"
            "globalThis.loads = globalThis.__sharedLoads;\n")
            .arg(escapeForSingleQuotedJs(absPath)));
    ASSERT_FALSE(mainPath.isEmpty());

    EXPECT_EQ(evalAndDrain(mainPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "result"), 22);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "loads"), 1);
}

TEST_F(EsModuleLoaderTest, MjsExtensionSupported) {
    writeFile(m_tmpDir, "lib/value.mjs", "export const VALUE = 321;\n");
    const QString mainPath = writeFile(
        m_tmpDir,
        "main.js",
        "import { VALUE } from './lib/value.mjs';\n"
        "globalThis.result = VALUE;\n");
    ASSERT_FALSE(mainPath.isEmpty());

    EXPECT_EQ(evalAndDrain(mainPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "result"), 321);
}

#ifdef Q_OS_WIN
TEST_F(EsModuleLoaderTest, WindowsBackslashSpecifierSupportedAndDeduped) {
    writeFile(
        m_tmpDir,
        "lib/win_once.js",
        "globalThis.__winLoads = (globalThis.__winLoads || 0) + 1;\n"
        "export const VALUE = 5;\n");
    const QString mainPath = writeFile(
        m_tmpDir,
        "main.js",
        "import { VALUE as A } from './lib/win_once.js';\n"
        "import { VALUE as B } from '.\\\\lib\\\\win_once.js';\n"
        "globalThis.result = A + B;\n"
        "globalThis.loads = globalThis.__winLoads;\n");
    ASSERT_FALSE(mainPath.isEmpty());

    EXPECT_EQ(evalAndDrain(mainPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "result"), 10);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "loads"), 1);
}
#endif
