# 里程碑 22：ES Module 加载器

## 1. 目标

实现 ES Module 加载器，支持 `import`/`export` 模块系统，可加载本地 `.js` 文件模块和内置模块。

## 2. 技术要点

### 2.1 模块解析规则

| import 路径 | 解析方式 |
|-------------|---------|
| `"stdiolink"` | 内置模块，C++ 注册 |
| `"./foo.js"` / `"../lib/bar.js"` | 相对路径，基于当前文件目录解析 |
| `"/abs/path.js"` | 绝对路径，直接加载 |

### 2.2 QuickJS 模块回调

- `module_normalize`：规范化模块名，将相对路径转为绝对路径
- `module_loader`：加载模块内容，内置模块直接返回预注册的 `JSModuleDef`，文件模块读取 `.js` 文件

### 2.3 内置模块注册

- 通过 `addBuiltin(name, initFunc)` 注册内置模块
- 内置模块名称匹配拦截，不走文件系统

## 3. 实现步骤

### 3.1 ModuleLoader 类

```cpp
// engine/module_loader.h
#pragma once

#include <QString>
#include <QHash>

struct JSContext;
struct JSModuleDef;

class ModuleLoader {
public:
    // 注册为 QuickJS 的 module_normalize + module_loader
    static void install(JSContext* ctx);

    // 注册内置模块名称
    static void addBuiltin(const QString& name,
                           JSModuleDef* (*init)(JSContext*, const char*));

private:
    // QuickJS 回调：规范化模块名
    static char* normalize(JSContext* ctx,
                           const char* baseName,
                           const char* name, void* opaque);

    // QuickJS 回调：加载模块
    static JSModuleDef* loader(JSContext* ctx,
                               const char* moduleName, void* opaque);

    // 内置模块注册表
    static QHash<QString, JSModuleDef* (*)(JSContext*, const char*)> s_builtins;
};
```

### 3.2 模块路径规范化

```cpp
// engine/module_loader.cpp (normalize 部分)

char* ModuleLoader::normalize(JSContext* ctx,
                              const char* baseName,
                              const char* name, void* opaque)
{
    Q_UNUSED(opaque);
    QString moduleName = QString::fromUtf8(name);

    // 内置模块：原样返回
    if (s_builtins.contains(moduleName)) {
        return js_strdup(ctx, name);
    }

    // 绝对路径：原样返回
    if (QDir::isAbsolutePath(moduleName)) {
        QString cleaned = QDir::cleanPath(moduleName);
        return js_strdup(ctx, cleaned.toUtf8().constData());
    }

    // 相对路径：基于 baseName 所在目录解析
    QString baseDir = QFileInfo(QString::fromUtf8(baseName)).absolutePath();
    QString resolved = QDir::cleanPath(baseDir + "/" + moduleName);
    return js_strdup(ctx, resolved.toUtf8().constData());
}
```

### 3.3 模块加载

```cpp
// engine/module_loader.cpp (loader 部分)

JSModuleDef* ModuleLoader::loader(JSContext* ctx,
                                  const char* moduleName, void* opaque)
{
    Q_UNUSED(opaque);
    QString name = QString::fromUtf8(moduleName);

    // 内置模块
    if (s_builtins.contains(name)) {
        auto initFunc = s_builtins.value(name);
        return initFunc(ctx, moduleName);
    }

    // 文件模块
    QFile file(name);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        JS_ThrowReferenceError(ctx, "Module not found: %s", moduleName);
        return nullptr;
    }

    QByteArray code = file.readAll();
    file.close();

    JSValue val = JS_Eval(ctx, code.constData(), code.size(),
                          moduleName, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(val)) {
        return nullptr;
    }

    // 注意：不要对 val 调用 JS_FreeValue。
    // JS_EVAL_FLAG_COMPILE_ONLY 返回的 JSValue 持有模块对象引用，
    // 提前释放会导致引用计数归零，模块被 GC 回收，返回的指针悬垂。
    // Runtime 会在内部管理模块的生命周期。
    return JS_VALUE_GET_PTR(val);
}
```

## 4. 文件清单

| 文件 | 说明 |
|------|------|
| `src/stdiolink_service/engine/module_loader.h` | 模块加载器头文件 |
| `src/stdiolink_service/engine/module_loader.cpp` | 模块加载器实现 |

## 5. 验收标准

1. 相对路径 `import { foo } from "./lib/foo.js"` 能正确加载
2. 绝对路径 `import` 能正确加载
3. 内置模块名称（如 `"stdiolink"`）能正确拦截
4. 模块文件不存在时抛出 `ReferenceError`
5. 模块路径规范化正确（消除 `..`、`.`）
6. 模块不会重复加载（QuickJS 内置缓存）
7. 模块间 `export`/`import` 值传递正确
8. 循环依赖不会导致崩溃

## 6. 单元测试用例

### 6.1 模块路径规范化测试

```cpp
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include "engine/js_engine.h"
#include "engine/module_loader.h"
#include "engine/console_bridge.h"

class ModuleLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        ModuleLoader::install(engine->context());
        ConsoleBridge::install(engine->context());
        tmpDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(tmpDir->isValid());
    }

    void TearDown() override {
        engine.reset();
        tmpDir.reset();
    }

    // 在临时目录中创建文件
    QString createFile(const QString& relativePath, const QString& content) {
        QString fullPath = tmpDir->path() + "/" + relativePath;
        QFileInfo fi(fullPath);
        fi.absoluteDir().mkpath(".");
        QFile f(fullPath);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        QTextStream out(&f);
        out << content;
        return fullPath;
    }

    std::unique_ptr<JsEngine> engine;
    std::unique_ptr<QTemporaryDir> tmpDir;
};

TEST_F(ModuleLoaderTest, ImportRelativePath) {
    createFile("lib/math.js",
        "export function square(x) { return x * x; }\n"
    );
    QString mainPath = createFile("main.js",
        "import { square } from './lib/math.js';\n"
        "globalThis.testResult = square(4);\n"
    );

    int ret = engine->evalFile(mainPath);
    EXPECT_EQ(ret, 0);
    while (engine->executePendingJobs()) {}

    JSValue global = JS_GetGlobalObject(engine->context());
    JSValue val = JS_GetPropertyStr(engine->context(), global, "testResult");
    int32_t result = 0;
    JS_ToInt32(engine->context(), &result, val);
    EXPECT_EQ(result, 16);
    JS_FreeValue(engine->context(), val);
    JS_FreeValue(engine->context(), global);
}

TEST_F(ModuleLoaderTest, ImportParentPath) {
    createFile("shared/utils.js",
        "export function double(x) { return x * 2; }\n"
    );
    QString mainPath = createFile("app/main.js",
        "import { double } from '../shared/utils.js';\n"
        "globalThis.testResult = double(5);\n"
    );

    int ret = engine->evalFile(mainPath);
    EXPECT_EQ(ret, 0);
    while (engine->executePendingJobs()) {}

    JSValue global = JS_GetGlobalObject(engine->context());
    JSValue val = JS_GetPropertyStr(engine->context(), global, "testResult");
    int32_t result = 0;
    JS_ToInt32(engine->context(), &result, val);
    EXPECT_EQ(result, 10);
    JS_FreeValue(engine->context(), val);
    JS_FreeValue(engine->context(), global);
}

TEST_F(ModuleLoaderTest, ImportAbsolutePath) {
    QString libPath = createFile("lib.js",
        "export const VALUE = 42;\n"
    );
    // 使用绝对路径 import
    QString mainPath = createFile("main.js",
        QString("import { VALUE } from '%1';\n"
                "globalThis.testResult = VALUE;\n").arg(libPath)
    );

    int ret = engine->evalFile(mainPath);
    EXPECT_EQ(ret, 0);
    while (engine->executePendingJobs()) {}

    JSValue global = JS_GetGlobalObject(engine->context());
    JSValue val = JS_GetPropertyStr(engine->context(), global, "testResult");
    int32_t result = 0;
    JS_ToInt32(engine->context(), &result, val);
    EXPECT_EQ(result, 42);
    JS_FreeValue(engine->context(), val);
    JS_FreeValue(engine->context(), global);
}
```

### 6.2 模块加载错误测试

```cpp
TEST_F(ModuleLoaderTest, ImportNonexistentFile) {
    QString mainPath = createFile("main.js",
        "import { foo } from './nonexistent.js';\n"
    );

    int ret = engine->evalFile(mainPath);
    EXPECT_NE(ret, 0);
}

TEST_F(ModuleLoaderTest, ImportSyntaxErrorModule) {
    createFile("bad.js", "export function { broken syntax");
    QString mainPath = createFile("main.js",
        "import { foo } from './bad.js';\n"
    );

    int ret = engine->evalFile(mainPath);
    EXPECT_NE(ret, 0);
}
```

### 6.3 模块功能测试

```cpp
TEST_F(ModuleLoaderTest, ExportDefault) {
    createFile("config.js",
        "export default { host: 'localhost', port: 8080 };\n"
    );
    QString mainPath = createFile("main.js",
        "import config from './config.js';\n"
        "globalThis.testResult = config.port;\n"
    );

    int ret = engine->evalFile(mainPath);
    EXPECT_EQ(ret, 0);
    while (engine->executePendingJobs()) {}

    JSValue global = JS_GetGlobalObject(engine->context());
    JSValue val = JS_GetPropertyStr(engine->context(), global, "testResult");
    int32_t result = 0;
    JS_ToInt32(engine->context(), &result, val);
    EXPECT_EQ(result, 8080);
    JS_FreeValue(engine->context(), val);
    JS_FreeValue(engine->context(), global);
}

TEST_F(ModuleLoaderTest, ReExport) {
    createFile("a.js", "export const A = 1;\n");
    createFile("b.js", "export { A } from './a.js';\n");
    QString mainPath = createFile("main.js",
        "import { A } from './b.js';\n"
        "globalThis.testResult = A;\n"
    );

    int ret = engine->evalFile(mainPath);
    EXPECT_EQ(ret, 0);
    while (engine->executePendingJobs()) {}

    JSValue global = JS_GetGlobalObject(engine->context());
    JSValue val = JS_GetPropertyStr(engine->context(), global, "testResult");
    int32_t result = 0;
    JS_ToInt32(engine->context(), &result, val);
    EXPECT_EQ(result, 1);
    JS_FreeValue(engine->context(), val);
    JS_FreeValue(engine->context(), global);
}

TEST_F(ModuleLoaderTest, ChainedImports) {
    createFile("a.js", "export const X = 10;\n");
    createFile("b.js",
        "import { X } from './a.js';\n"
        "export const Y = X + 5;\n"
    );
    QString mainPath = createFile("main.js",
        "import { Y } from './b.js';\n"
        "globalThis.testResult = Y;\n"
    );

    int ret = engine->evalFile(mainPath);
    EXPECT_EQ(ret, 0);
    while (engine->executePendingJobs()) {}

    JSValue global = JS_GetGlobalObject(engine->context());
    JSValue val = JS_GetPropertyStr(engine->context(), global, "testResult");
    int32_t result = 0;
    JS_ToInt32(engine->context(), &result, val);
    EXPECT_EQ(result, 15);
    JS_FreeValue(engine->context(), val);
    JS_FreeValue(engine->context(), global);
}
```

### 6.4 内置模块注册测试

```cpp
TEST_F(ModuleLoaderTest, BuiltinModuleIntercept) {
    // 注册一个测试用内置模块
    ModuleLoader::addBuiltin("test_builtin", [](JSContext* ctx, const char* name) -> JSModuleDef* {
        JSModuleDef* m = JS_NewCModule(ctx, name, [](JSContext* ctx, JSModuleDef* m) -> int {
            JSValue val = JS_NewInt32(ctx, 999);
            JS_SetModuleExport(ctx, m, "MAGIC", val);
            return 0;
        });
        JS_AddModuleExport(ctx, m, "MAGIC");
        return m;
    });

    QString mainPath = createFile("main.js",
        "import { MAGIC } from 'test_builtin';\n"
        "globalThis.testResult = MAGIC;\n"
    );

    int ret = engine->evalFile(mainPath);
    EXPECT_EQ(ret, 0);
    while (engine->executePendingJobs()) {}

    JSValue global = JS_GetGlobalObject(engine->context());
    JSValue val = JS_GetPropertyStr(engine->context(), global, "testResult");
    int32_t result = 0;
    JS_ToInt32(engine->context(), &result, val);
    EXPECT_EQ(result, 999);
    JS_FreeValue(engine->context(), val);
    JS_FreeValue(engine->context(), global);
}
```

## 7. 依赖关系

- **前置依赖**：
  - 里程碑 21（JS 引擎脚手架）：JsEngine 基础封装
- **后续依赖**：
  - 里程碑 23（Driver/Task 绑定）：需要模块加载器注册 `"stdiolink"` 内置模块
  - 里程碑 24（进程调用绑定）：同上
