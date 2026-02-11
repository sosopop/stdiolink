# 里程碑 42：JS Path 基础库绑定

> **前置条件**: 里程碑 41 已完成
> **目标**: 实现 `stdiolink/path`，提供跨平台、可预测的路径操作基础 API

---

## 1. 目标

- 新增 `stdiolink/path` 内置模块
- 提供 API：`join/resolve/dirname/basename/extname/normalize/isAbsolute`
- 统一输出路径格式，降低脚本侧跨平台分支复杂度

---

## 2. 设计原则（强约束）

- **简约**: 只提供纯函数，不引入状态与缓存
- **可靠**: 所有函数参数校验明确，错误可预测
- **稳定**: 输出路径规范固定，不随调用场景漂移
- **避免过度设计**: 不实现 glob、path matcher、URL path 等高级能力

---

## 3. 范围与非目标

### 3.1 范围（M42 内）

- `stdiolink/path` 模块
- 7 个核心路径函数
- 完整单元测试（正常 + 边界 + 错误）

### 3.2 非目标（M42 外）

- 不实现 `relative()`、`parse()`、`format()` 等扩展 API
- 不与文件系统访问耦合（不检查路径存在性）

---

## 4. 技术方案

### 4.1 模块接口

```js
import { join, resolve, dirname, basename, extname,
         normalize, isAbsolute } from "stdiolink/path";
```

### 4.2 语义约束

- 参数必须为字符串（`join/resolve` 的每个 segment 也是字符串），非字符串抛 `TypeError`
- 返回路径统一使用 `/` 分隔（Windows 反斜杠转为正斜杠）
- `resolve()` 基于 `QDir::currentPath()` 解析相对路径
- `extname("a.tar.gz")` → `".gz"`（取最后一个 `.` 后缀）
- `join()` 零参数返回 `"."`
- `basename("/a/b/")` 末尾分隔符忽略，返回 `"b"`

### 4.3 创建 bindings/js_path.h

```cpp
#pragma once

#include <quickjs.h>

namespace stdiolink_service {

/// @brief stdiolink/path 内置模块绑定
///
/// 提供纯函数式路径操作 API，无状态，无需 runtime 隔离。
/// 底层使用 QDir/QFileInfo 实现，输出路径统一使用 '/' 分隔。
class JsPathBinding {
public:
    /// 模块初始化回调（注册给 ModuleLoader）
    static JSModuleDef* initModule(JSContext* ctx, const char* name);
};

} // namespace stdiolink_service
```

### 4.4 创建 bindings/js_path.cpp

```cpp
#include "js_path.h"

#include <QDir>
#include <QFileInfo>
#include <quickjs.h>

namespace stdiolink_service {

namespace {

/// 统一路径分隔符为 '/'
QString normalizeSeparators(const QString& path) {
    QString result = path;
    result.replace('\\', '/');
    return result;
}

/// 从 JS 参数提取字符串，非字符串抛 TypeError
/// @return 成功返回 true，失败返回 false（已抛异常）
bool extractString(JSContext* ctx, JSValueConst val,
                   const char* funcName, int argIndex,
                   QString& out) {
    if (!JS_IsString(val)) {
        JS_ThrowTypeError(ctx, "%s: argument %d must be a string",
                          funcName, argIndex);
        return false;
    }
    const char* str = JS_ToCString(ctx, val);
    if (!str) return false;
    out = QString::fromUtf8(str);
    JS_FreeCString(ctx, str);
    return true;
}

JSValue jsJoin(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc == 0) {
        return JS_NewString(ctx, ".");
    }
    QStringList segments;
    for (int i = 0; i < argc; ++i) {
        QString seg;
        if (!extractString(ctx, argv[i], "join", i)) {
            return JS_EXCEPTION;
        }
        const char* str = JS_ToCString(ctx, argv[i]);
        segments.append(QString::fromUtf8(str));
        JS_FreeCString(ctx, str);
    }
    QString joined = segments.join('/');
    QString result = normalizeSeparators(
        QDir::cleanPath(joined));
    return JS_NewString(ctx, result.toUtf8().constData());
}

JSValue jsResolve(JSContext* ctx, JSValueConst,
                  int argc, JSValueConst* argv) {
    QString result = QDir::currentPath();
    for (int i = 0; i < argc; ++i) {
        if (!JS_IsString(argv[i])) {
            return JS_ThrowTypeError(ctx,
                "resolve: argument %d must be a string", i);
        }
        const char* str = JS_ToCString(ctx, argv[i]);
        QString seg = QString::fromUtf8(str);
        JS_FreeCString(ctx, str);
        if (QDir::isAbsolutePath(seg)) {
            result = seg;
        } else {
            result = QDir(result).filePath(seg);
        }
    }
    return JS_NewString(ctx,
        normalizeSeparators(QDir::cleanPath(result))
            .toUtf8().constData());
}

JSValue jsDirname(JSContext* ctx, JSValueConst,
                  int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "dirname: argument must be a string");
    }
    const char* str = JS_ToCString(ctx, argv[0]);
    QString path = QString::fromUtf8(str);
    JS_FreeCString(ctx, str);
    QFileInfo fi(path);
    return JS_NewString(ctx,
        normalizeSeparators(fi.path()).toUtf8().constData());
}

JSValue jsBasename(JSContext* ctx, JSValueConst,
                   int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "basename: argument must be a string");
    }
    const char* str = JS_ToCString(ctx, argv[0]);
    QString path = QString::fromUtf8(str);
    JS_FreeCString(ctx, str);
    // 去除末尾分隔符
    while (path.endsWith('/') || path.endsWith('\\')) {
        path.chop(1);
    }
    QFileInfo fi(path);
    return JS_NewString(ctx,
        fi.fileName().toUtf8().constData());
}

JSValue jsExtname(JSContext* ctx, JSValueConst,
                  int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "extname: argument must be a string");
    }
    const char* str = JS_ToCString(ctx, argv[0]);
    QString path = QString::fromUtf8(str);
    JS_FreeCString(ctx, str);
    QFileInfo fi(path);
    QString suffix = fi.suffix();
    if (suffix.isEmpty()) {
        return JS_NewString(ctx, "");
    }
    QString result = "." + suffix;
    return JS_NewString(ctx, result.toUtf8().constData());
}

JSValue jsNormalize(JSContext* ctx, JSValueConst,
                    int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "normalize: argument must be a string");
    }
    const char* str = JS_ToCString(ctx, argv[0]);
    QString path = QString::fromUtf8(str);
    JS_FreeCString(ctx, str);
    return JS_NewString(ctx,
        normalizeSeparators(QDir::cleanPath(path))
            .toUtf8().constData());
}

JSValue jsIsAbsolute(JSContext* ctx, JSValueConst,
                     int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "isAbsolute: argument must be a string");
    }
    const char* str = JS_ToCString(ctx, argv[0]);
    QString path = QString::fromUtf8(str);
    JS_FreeCString(ctx, str);
    return JS_NewBool(ctx, QDir::isAbsolutePath(path));
}

int pathModuleInit(JSContext* ctx, JSModuleDef* module) {
    JS_SetModuleExport(ctx, module, "join",
        JS_NewCFunction(ctx, jsJoin, "join", 0));
    JS_SetModuleExport(ctx, module, "resolve",
        JS_NewCFunction(ctx, jsResolve, "resolve", 0));
    JS_SetModuleExport(ctx, module, "dirname",
        JS_NewCFunction(ctx, jsDirname, "dirname", 1));
    JS_SetModuleExport(ctx, module, "basename",
        JS_NewCFunction(ctx, jsBasename, "basename", 1));
    JS_SetModuleExport(ctx, module, "extname",
        JS_NewCFunction(ctx, jsExtname, "extname", 1));
    JS_SetModuleExport(ctx, module, "normalize",
        JS_NewCFunction(ctx, jsNormalize, "normalize", 1));
    JS_SetModuleExport(ctx, module, "isAbsolute",
        JS_NewCFunction(ctx, jsIsAbsolute, "isAbsolute", 1));
    return 0;
}

} // namespace

JSModuleDef* JsPathBinding::initModule(JSContext* ctx,
                                        const char* name) {
    JSModuleDef* module = JS_NewCModule(ctx, name, pathModuleInit);
    if (!module) return nullptr;
    const char* exports[] = {
        "join", "resolve", "dirname", "basename",
        "extname", "normalize", "isAbsolute"
    };
    for (const char* e : exports) {
        JS_AddModuleExport(ctx, module, e);
    }
    return module;
}

} // namespace stdiolink_service
```

### 4.5 main.cpp 集成

```cpp
// main.cpp — 新增部分
#include "bindings/js_path.h"

// 模块注册（在 stdiolink/constants 之后）：
engine.registerModule("stdiolink/path",
                      JsPathBinding::initModule);
```

### 4.6 JS 使用示例

```js
import { join, resolve, dirname, basename, extname,
         normalize, isAbsolute } from "stdiolink/path";

console.log(join("a", "b", "c"));       // "a/b/c"
console.log(dirname("/a/b/c.txt"));      // "/a/b"
console.log(basename("/a/b/c.txt"));     // "c.txt"
console.log(extname("archive.tar.gz"));  // ".gz"
console.log(normalize("a/./b/../c"));    // "a/c"
console.log(isAbsolute("/usr/bin"));     // true
console.log(resolve("a", "b"));         // "<cwd>/a/b"
```

---

## 5. 实现步骤

1. 新增 `js_path` 绑定文件并注册 `stdiolink/path`
2. 使用 `QDir/QFileInfo` 实现核心路径逻辑
3. 统一参数校验与错误消息格式
4. 编写单元测试
5. 更新 manual 文档

---

## 6. 文件改动清单

### 6.1 新增文件

- `src/stdiolink_service/bindings/js_path.h`
- `src/stdiolink_service/bindings/js_path.cpp`
- `src/tests/test_path_binding.cpp`
- `doc/manual/10-js-service/path-binding.md`

### 6.2 修改文件

- `src/stdiolink_service/main.cpp`
- `src/stdiolink_service/CMakeLists.txt`
- `src/tests/CMakeLists.txt`
- `doc/manual/10-js-service/module-system.md`
- `doc/manual/10-js-service/README.md`

---

## 7. 单元测试计划（全面覆盖）

### 7.1 测试 Fixture

```cpp
// test_path_binding.cpp
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QTextStream>
#include "engine/js_engine.h"
#include "engine/module_loader.h"
#include "engine/console_bridge.h"
#include "bindings/js_path.h"

using namespace stdiolink_service;

class JsPathTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        ModuleLoader::install(engine->context());
        ConsoleBridge::install(engine->context());
        ModuleLoader::addBuiltin("stdiolink/path",
                                 JsPathBinding::initModule);
    }
    void TearDown() override { engine.reset(); }

    int runScript(const QString& code) {
        QTemporaryDir dir;
        QString path = dir.path() + "/test.mjs";
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        QTextStream out(&f);
        out << code;
        out.flush();
        f.close();
        return engine->evalFile(path);
    }

    int32_t getGlobalInt(const char* name) {
        JSValue g = JS_GetGlobalObject(engine->context());
        JSValue v = JS_GetPropertyStr(engine->context(), g, name);
        int32_t r = 0;
        JS_ToInt32(engine->context(), &r, v);
        JS_FreeValue(engine->context(), v);
        JS_FreeValue(engine->context(), g);
        return r;
    }

    std::unique_ptr<JsEngine> engine;
};
```

### 7.2 基础功能

```cpp
TEST_F(JsPathTest, JoinBasic) {
    int ret = runScript(
        "import { join } from 'stdiolink/path';\n"
        "globalThis.ok = (join('a','b','c') === 'a/b/c') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsPathTest, DirnameBasic) {
    int ret = runScript(
        "import { dirname } from 'stdiolink/path';\n"
        "globalThis.ok = (dirname('/a/b/c.txt') === '/a/b') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsPathTest, BasenameBasic) {
    int ret = runScript(
        "import { basename } from 'stdiolink/path';\n"
        "globalThis.ok = (basename('/a/b/c.txt') === 'c.txt') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsPathTest, ExtnameBasic) {
    int ret = runScript(
        "import { extname } from 'stdiolink/path';\n"
        "globalThis.ok = (extname('/a/b/c.txt') === '.txt') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.3 规范化与绝对路径

```cpp
TEST_F(JsPathTest, NormalizeRemovesDotDot) {
    int ret = runScript(
        "import { normalize } from 'stdiolink/path';\n"
        "globalThis.ok = (normalize('a/./b/../c') === 'a/c') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsPathTest, ResolveReturnsAbsolute) {
    int ret = runScript(
        "import { resolve, isAbsolute } from 'stdiolink/path';\n"
        "const r = resolve('a', 'b');\n"
        "globalThis.ok = isAbsolute(r) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsPathTest, IsAbsoluteUnixPath) {
    int ret = runScript(
        "import { isAbsolute } from 'stdiolink/path';\n"
        "globalThis.ok = isAbsolute('/usr/bin') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsPathTest, IsAbsoluteRelativePath) {
    int ret = runScript(
        "import { isAbsolute } from 'stdiolink/path';\n"
        "globalThis.ok = isAbsolute('a/b') ? 0 : 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.4 边界行为

```cpp
TEST_F(JsPathTest, JoinZeroArgs) {
    int ret = runScript(
        "import { join } from 'stdiolink/path';\n"
        "globalThis.ok = (join() === '.') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsPathTest, BasenameTrailingSeparator) {
    int ret = runScript(
        "import { basename } from 'stdiolink/path';\n"
        "globalThis.ok = (basename('/a/b/') === 'b') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsPathTest, ExtnameMultiSuffix) {
    int ret = runScript(
        "import { extname } from 'stdiolink/path';\n"
        "globalThis.ok = (extname('archive.tar.gz') === '.gz') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsPathTest, ExtnameNoSuffix) {
    int ret = runScript(
        "import { extname } from 'stdiolink/path';\n"
        "globalThis.ok = (extname('Makefile') === '') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.5 错误路径

```cpp
TEST_F(JsPathTest, DirnameNonStringThrows) {
    int ret = runScript(
        "import { dirname } from 'stdiolink/path';\n"
        "try { dirname(123); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof TypeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsPathTest, JoinNonStringArgThrows) {
    int ret = runScript(
        "import { join } from 'stdiolink/path';\n"
        "try { join('a', 123); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof TypeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.6 回归测试

- 运行现有 JS 绑定核心测试，确保无导出冲突

---

## 8. 验收标准（DoD）

- `stdiolink/path` API 全部可用
- 路径输出语义一致且跨平台可预测
- 新增与回归测试全部通过
- 文档更新完成

---

## 9. 风险与控制

- **风险 1**：平台路径差异导致断言不稳
  - 控制：测试断言按平台分支，核心语义保持一致
- **风险 2**：语义与 Node.js path 混淆
  - 控制：文档明确“仅实现本项目定义语义”
