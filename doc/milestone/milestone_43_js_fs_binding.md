# 里程碑 43：JS FS 基础库绑定

> **前置条件**: 里程碑 42 已完成
> **目标**: 实现 `stdiolink/fs` 的同步文件系统最小可用集合，覆盖脚本常用读写场景

---

## 1. 目标

- 新增 `stdiolink/fs` 内置模块
- 提供 API：`exists/readText/writeText/readJson/writeJson/mkdir/listDir/stat`
- 建立统一错误模型，避免脚本层反复处理 Qt 细节

---

## 2. 设计原则（强约束）

- **简约**: 同步 API 先行，不引入文件流/监听器/权限系统
- **可靠**: IO 错误必须可定位（含路径与动作）
- **稳定**: 默认行为可预测（如 UTF-8、列表排序）
- **避免过度设计**: 不实现 copy/move/chmod/chown 等扩展操作

---

## 3. 范围与非目标

### 3.1 范围（M43 内）

- `stdiolink/fs` 模块与最小同步 API
- 统一错误格式
- 单元测试覆盖正常、异常与边界

### 3.2 非目标（M43 外）

- 不实现异步文件 IO
- 不实现文件监控与事件回调
- 不实现大文件流式读写 API

---

## 4. 技术方案

### 4.1 模块接口

```js
import { exists, readText, writeText, readJson, writeJson,
         mkdir, listDir, stat } from "stdiolink/fs";
```

### 4.2 选项定义

- `writeText/writeJson`:
  - `append?: boolean`（默认 `false`）
  - `ensureParent?: boolean`（默认 `false`）
- `mkdir`:
  - `recursive?: boolean`（默认 `true`）
- `listDir`:
  - `recursive?: boolean`（默认 `false`）
  - `filesOnly?: boolean`（默认 `false`）
  - `dirsOnly?: boolean`（默认 `false`，与 `filesOnly` 互斥）

### 4.3 实现约束

- `readText` 仅支持 UTF-8（非 UTF-8 明确报错）
- `listDir` 默认按字典序排序，保证测试稳定
- `readJson` 必须对非法 JSON 抛出明确错误
- 所有 IO 错误统一使用 `JS_ThrowInternalError`，且消息格式固定为 `"fs.<func>: <message> (path: <path>)"`

### 4.4 创建 bindings/js_fs.h

```cpp
#pragma once

#include <quickjs.h>

namespace stdiolink_service {

/// @brief stdiolink/fs 内置模块绑定
///
/// 提供同步文件系统操作 API。所有函数均为同步调用，
/// IO 错误抛出 InternalError（含路径信息），类型错误抛出 TypeError。
/// 底层使用 QFile/QDir/QFileInfo/QJsonDocument 实现。
class JsFsBinding {
public:
    /// 模块初始化回调（注册给 ModuleLoader）
    static JSModuleDef* initModule(JSContext* ctx, const char* name);
};

} // namespace stdiolink_service
```

### 4.5 创建 bindings/js_fs.cpp（关键实现）

```cpp
#include "js_fs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <quickjs.h>

#include "utils/js_convert.h"

namespace stdiolink_service {

namespace {

/// 从 argv[index] 提取 QString，失败抛 TypeError
bool extractStringArg(JSContext* ctx, JSValueConst val,
                      const char* func, int index, QString& out) {
    if (!JS_IsString(val)) {
        JS_ThrowTypeError(ctx, "fs.%s: argument %d must be a string",
                          func, index);
        return false;
    }
    const char* s = JS_ToCString(ctx, val);
    if (!s) return false;
    out = QString::fromUtf8(s);
    JS_FreeCString(ctx, s);
    return true;
}

/// 从 options 对象提取 bool 属性，缺失返回 defaultVal
bool optBool(JSContext* ctx, JSValueConst opts,
             const char* key, bool defaultVal) {
    JSValue v = JS_GetPropertyStr(ctx, opts, key);
    bool result = defaultVal;
    if (JS_IsBool(v)) {
        result = JS_ToBool(ctx, v);
    }
    JS_FreeValue(ctx, v);
    return result;
}

JSValue jsExists(JSContext* ctx, JSValueConst,
                 int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "fs.exists: path argument required");
    }
    QString path;
    if (!extractStringArg(ctx, argv[0], "exists", 0, path))
        return JS_EXCEPTION;
    if (path.isEmpty()) {
        return JS_ThrowTypeError(ctx,
            "fs.exists: path must not be empty");
    }
    return JS_NewBool(ctx, QFileInfo::exists(path));
}

JSValue jsReadText(JSContext* ctx, JSValueConst,
                   int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "fs.readText: path argument required");
    }
    QString path;
    if (!extractStringArg(ctx, argv[0], "readText", 0, path))
        return JS_EXCEPTION;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return JS_ThrowInternalError(ctx,
            "fs.readText: cannot open file (path: %s)",
            path.toUtf8().constData());
    }
    QByteArray data = file.readAll();
    // UTF-8 有效性检查（无效字节会在 round-trip 时变化）
    QString text = QString::fromUtf8(data.constData(), data.size());
    if (text.toUtf8() != data) {
        return JS_ThrowInternalError(ctx,
            "fs.readText: file is not valid UTF-8 (path: %s)",
            path.toUtf8().constData());
    }
    return JS_NewStringLen(ctx, data.constData(),
                           static_cast<size_t>(data.size()));
}

JSValue jsWriteText(JSContext* ctx, JSValueConst,
                    int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx,
            "fs.writeText: path and text arguments required");
    }
    QString path, text;
    if (!extractStringArg(ctx, argv[0], "writeText", 0, path))
        return JS_EXCEPTION;
    if (!extractStringArg(ctx, argv[1], "writeText", 1, text))
        return JS_EXCEPTION;

    bool append = false, ensureParent = false;
    if (argc >= 3 && JS_IsObject(argv[2])) {
        append = optBool(ctx, argv[2], "append", false);
        ensureParent = optBool(ctx, argv[2], "ensureParent", false);
    }

    if (ensureParent) {
        QDir().mkpath(QFileInfo(path).absolutePath());
    }

    QIODevice::OpenMode mode = QIODevice::WriteOnly | QIODevice::Text;
    if (append) mode |= QIODevice::Append;

    QFile file(path);
    if (!file.open(mode)) {
        return JS_ThrowInternalError(ctx,
            "fs.writeText: cannot open file for writing (path: %s)",
            path.toUtf8().constData());
    }
    file.write(text.toUtf8());
    return JS_UNDEFINED;
}

JSValue jsStat(JSContext* ctx, JSValueConst,
               int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "fs.stat: path argument required");
    }
    QString path;
    if (!extractStringArg(ctx, argv[0], "stat", 0, path))
        return JS_EXCEPTION;

    QFileInfo fi(path);
    if (!fi.exists()) {
        return JS_ThrowInternalError(ctx,
            "fs.stat: path does not exist (path: %s)",
            path.toUtf8().constData());
    }

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "isFile",
                      JS_NewBool(ctx, fi.isFile()));
    JS_SetPropertyStr(ctx, obj, "isDir",
                      JS_NewBool(ctx, fi.isDir()));
    JS_SetPropertyStr(ctx, obj, "size",
                      JS_NewFloat64(ctx, fi.size()));
    JS_SetPropertyStr(ctx, obj, "mtimeMs",
        JS_NewFloat64(ctx,
            fi.lastModified().toMSecsSinceEpoch()));
    return obj;
}

// readJson, writeJson, mkdir, listDir 需完整实现，并满足以下约束：
// 1) 参数校验与 exists/readText/writeText/stat 一致（类型错误抛 TypeError）
// 2) IO 失败统一抛 InternalError，且错误消息带 path
// 3) listDir 默认字典序排序；filesOnly/dirsOnly 互斥校验
// 4) readJson 对非法 JSON 抛明确错误；writeJson 默认紧凑输出

int fsModuleInit(JSContext* ctx, JSModuleDef* module) {
    JS_SetModuleExport(ctx, module, "exists",
        JS_NewCFunction(ctx, jsExists, "exists", 1));
    JS_SetModuleExport(ctx, module, "readText",
        JS_NewCFunction(ctx, jsReadText, "readText", 1));
    JS_SetModuleExport(ctx, module, "writeText",
        JS_NewCFunction(ctx, jsWriteText, "writeText", 2));
    JS_SetModuleExport(ctx, module, "readJson",
        JS_NewCFunction(ctx, jsReadJson, "readJson", 1));
    JS_SetModuleExport(ctx, module, "writeJson",
        JS_NewCFunction(ctx, jsWriteJson, "writeJson", 2));
    JS_SetModuleExport(ctx, module, "mkdir",
        JS_NewCFunction(ctx, jsMkdir, "mkdir", 1));
    JS_SetModuleExport(ctx, module, "listDir",
        JS_NewCFunction(ctx, jsListDir, "listDir", 1));
    JS_SetModuleExport(ctx, module, "stat",
        JS_NewCFunction(ctx, jsStat, "stat", 1));
    return 0;
}

} // namespace

JSModuleDef* JsFsBinding::initModule(JSContext* ctx,
                                      const char* name) {
    JSModuleDef* module = JS_NewCModule(ctx, name, fsModuleInit);
    if (!module) return nullptr;
    const char* exports[] = {
        "exists", "readText", "writeText", "readJson",
        "writeJson", "mkdir", "listDir", "stat"
    };
    for (const char* e : exports) {
        JS_AddModuleExport(ctx, module, e);
    }
    return module;
}

} // namespace stdiolink_service
```

### 4.6 main.cpp 集成

```cpp
#include "bindings/js_fs.h"

// 模块注册（在 stdiolink/path 之后）：
engine.registerModule("stdiolink/fs", JsFsBinding::initModule);
```

### 4.7 JS 使用示例

```js
import { exists, readText, writeText, readJson,
         writeJson, mkdir, listDir, stat } from "stdiolink/fs";

// 写入并回读
writeText("/tmp/hello.txt", "Hello World");
const text = readText("/tmp/hello.txt");

// JSON 读写
writeJson("/tmp/config.json", { port: 8080 });
const cfg = readJson("/tmp/config.json");

// 目录操作
mkdir("/tmp/mydir/sub", { recursive: true });
const files = listDir("/tmp/mydir", { recursive: true });

// 文件信息
const info = stat("/tmp/hello.txt");
console.log(info.isFile, info.size, info.mtimeMs);
```

---

## 5. 实现步骤

1. 新增 `js_fs` 绑定并注册模块
2. 用 `QFile/QDir/QFileInfo/QJsonDocument` 实现 API
3. 统一错误封装（`TypeError` + `InternalError("fs.xxx: ...")`）
4. 编写覆盖完整的单元测试
5. 更新 manual 文档

---

## 6. 文件改动清单

### 6.1 新增文件

- `src/stdiolink_service/bindings/js_fs.h`
- `src/stdiolink_service/bindings/js_fs.cpp`
- `src/tests/test_fs_binding.cpp`
- `doc/manual/10-js-service/fs-binding.md`

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
// test_fs_binding.cpp
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include "engine/js_engine.h"
#include "engine/module_loader.h"
#include "engine/console_bridge.h"
#include "bindings/js_fs.h"

using namespace stdiolink_service;

class JsFsTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        ModuleLoader::install(engine->context());
        ConsoleBridge::install(engine->context());
        ModuleLoader::addBuiltin("stdiolink/fs",
                                 JsFsBinding::initModule);
        tmpDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(tmpDir->isValid());
    }
    void TearDown() override {
        engine.reset();
        tmpDir.reset();
    }

    /// 注入 tmpDir 路径到 JS 全局变量后执行脚本
    int runScript(const QString& code) {
        QString wrapped = QString(
            "globalThis.__tmpDir = '%1';\n%2"
        ).arg(tmpDir->path().replace('\\', '/'), code);
        QString path = tmpDir->path() + "/test.mjs";
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(wrapped.toUtf8());
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
    std::unique_ptr<QTemporaryDir> tmpDir;
};
```

### 7.2 正向用例

```cpp
TEST_F(JsFsTest, WriteTextAndReadText) {
    int ret = runScript(
        "import { writeText, readText } from 'stdiolink/fs';\n"
        "const p = __tmpDir + '/hello.txt';\n"
        "writeText(p, 'Hello World');\n"
        "globalThis.ok = (readText(p) === 'Hello World') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
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
    EXPECT_EQ(getGlobalInt("ok"), 1);
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
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.3 选项行为

```cpp
TEST_F(JsFsTest, AppendMode) {
    int ret = runScript(
        "import { writeText, readText } from 'stdiolink/fs';\n"
        "const p = __tmpDir + '/append.txt';\n"
        "writeText(p, 'A');\n"
        "writeText(p, 'B', { append: true });\n"
        "globalThis.ok = (readText(p) === 'AB') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsFsTest, EnsureParentCreatesDir) {
    int ret = runScript(
        "import { writeText, exists } from 'stdiolink/fs';\n"
        "const p = __tmpDir + '/deep/nested/file.txt';\n"
        "writeText(p, 'ok', { ensureParent: true });\n"
        "globalThis.ok = exists(p) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsFsTest, ListDirSorted) {
    int ret = runScript(
        "import { writeText, listDir } from 'stdiolink/fs';\n"
        "writeText(__tmpDir + '/c.txt', '');\n"
        "writeText(__tmpDir + '/a.txt', '');\n"
        "writeText(__tmpDir + '/b.txt', '');\n"
        "const list = listDir(__tmpDir);\n"
        "// 应按字典序排列\n"
        "const sorted = list.filter(f => f.endsWith('.txt'));\n"
        "globalThis.ok = (sorted[0] === 'a.txt'"
        " && sorted[1] === 'b.txt' && sorted[2] === 'c.txt') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.4 异常与边界

```cpp
TEST_F(JsFsTest, ReadNonExistentFileThrows) {
    int ret = runScript(
        "import { readText } from 'stdiolink/fs';\n"
        "try { readText('/nonexistent/path.txt'); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = 1; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
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
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsFsTest, StatNonExistentThrows) {
    int ret = runScript(
        "import { stat } from 'stdiolink/fs';\n"
        "try { stat('/no/such/path'); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = e.message.includes('/no/such/path') ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsFsTest, ExistsEmptyStringThrowsTypeError) {
    int ret = runScript(
        "import { exists } from 'stdiolink/fs';\n"
        "try { exists(''); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof TypeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsFsTest, NonStringArgThrowsTypeError) {
    int ret = runScript(
        "import { readText } from 'stdiolink/fs';\n"
        "try { readText(123); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof TypeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
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
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.5 回归测试

- 全量 JS 绑定核心测试保持通过

---

## 8. 验收标准（DoD）

- `stdiolink/fs` API 全部可用
- 错误模型与文档一致
- 单测覆盖核心、异常、边界并通过
- 无现有功能回归

---

## 9. 风险与控制

- **风险 1**：不同平台编码/换行差异
  - 控制：统一 UTF-8，断言按语义比较
- **风险 2**：目录遍历顺序不稳定
  - 控制：API 层排序后返回
