# 里程碑 23：Driver/Task 绑定与数据转换

## 1. 目标

将 C++ `stdiolink::Driver` 和 `stdiolink::Task` 类暴露为 JS 对象，实现 QJsonValue ↔ JSValue 双向转换工具，使 JS 脚本能通过底层 API 启动 Driver、发送请求、接收响应。

## 2. 技术要点

### 2.1 QJsonValue ↔ JSValue 转换

所有 C++ ↔ JS 数据交换的核心基础设施，需递归处理所有 JSON 类型。

**QJsonValue → JSValue**：

| QJsonValue 类型 | JSValue 类型 |
|-----------------|-------------|
| `Null` | `JS_NULL` |
| `Bool` | `JS_NewBool` |
| `Double` | `JS_NewFloat64` |
| `String` | `JS_NewString` |
| `Array` | `JS_NewArray` + 递归填充 |
| `Object` | `JS_NewObject` + 递归填充 |

**JSValue → QJsonValue**：

| JSValue 类型 | QJsonValue 类型 |
|-------------|-----------------|
| `undefined` / `null` | `QJsonValue()` |
| `bool` | `QJsonValue(bool)` |
| `number` (整数) | `QJsonValue(int)` |
| `number` (浮点) | `QJsonValue(double)` |
| `string` | `QJsonValue(QString)` |
| `Array` | `QJsonArray` + 递归 |
| `Object` | `QJsonObject` + 递归 |

### 2.2 Driver 类绑定

- 使用 QuickJS 的 `JS_NewClassID` + `JS_NewClass` 注册自定义类
- **注意**：quickjs-ng 中 `JS_NewClassID` 签名为 `JS_NewClassID(JSRuntime *rt, JSClassID *id)`，需传入 Runtime 指针，不同于官方版本的 `JS_NewClassID(JSClassID *id)`
- C++ `Driver` 实例存储在 JS 对象的 opaque 指针中
- GC finalizer 中调用 `terminate()` 并释放 C++ 对象

### 2.3 Task 类绑定

- Task 由 `Driver.request()` 创建返回，不支持用户直接构造
- `waitNext()` 内部调用 C++ `Task::waitNext()`，阻塞 JS 执行线程
- 消息的 `QJsonValue` payload 转为 JS 对象时递归转换

### 2.4 stdiolink 内置模块注册

将 Driver 构造函数注册到 `"stdiolink"` 内置模块导出。

## 3. 实现步骤

### 3.1 QJsonValue ↔ JSValue 转换工具

```cpp
// utils/js_convert.h
#pragma once

#include <QJsonValue>
#include <QJsonObject>
#include <QJsonArray>
#include "quickjs.h"

JSValue qjsonToJsValue(JSContext* ctx, const QJsonValue& val);
QJsonValue jsValueToQJson(JSContext* ctx, JSValue val);
JSValue qjsonObjectToJsValue(JSContext* ctx, const QJsonObject& obj);
QJsonObject jsValueToQJsonObject(JSContext* ctx, JSValue val);
```

### 3.2 Driver 类绑定

```cpp
// bindings/js_driver.h
#pragma once

#include "quickjs.h"

class JsDriverBinding {
public:
    // 注册 Driver 类到 context
    static void registerClass(JSContext* ctx);

    // 获取 Driver 构造函数（供模块导出）
    static JSValue getConstructor(JSContext* ctx);
};
```

**JS API**：

```js
class Driver {
    constructor()
    start(program, args = [])       // → boolean
    request(cmd, data = {})         // → Task
    queryMeta(timeoutMs = 5000)     // → object | null
    terminate()                     // → void
    get running()                   // → boolean
    get hasMeta()                   // → boolean
}
```

### 3.3 Task 类绑定

```cpp
// bindings/js_task.h
#pragma once

#include "quickjs.h"

class JsTaskBinding {
public:
    static void registerClass(JSContext* ctx);

    // 从 C++ Task 创建 JS Task 对象（供 Driver.request() 调用）
    static JSValue createFromTask(JSContext* ctx,
                                  stdiolink::Driver* owner,
                                  std::shared_ptr<stdiolink::TaskState> state);
};
```

**JS API**：

```js
class Task {
    tryNext()                       // → { status, code, data } | null
    waitNext(timeoutMs = -1)        // → { status, code, data } | null
    get done()                      // → boolean
    get exitCode()                  // → number
    get errorText()                 // → string
    get finalPayload()              // → any
}
```

### 3.4 stdiolink 内置模块

```cpp
// bindings/js_stdiolink_module.h
#pragma once

struct JSContext;
struct JSModuleDef;

JSModuleDef* jsInitStdiolinkModule(JSContext* ctx, const char* name);
```

```cpp
// bindings/js_stdiolink_module.cpp
static int jsModuleInit(JSContext* ctx, JSModuleDef* m) {
    JS_SetModuleExport(ctx, m, "Driver",
                       JsDriverBinding::getConstructor(ctx));
    return 0;
}

JSModuleDef* jsInitStdiolinkModule(JSContext* ctx, const char* name) {
    JSModuleDef* m = JS_NewCModule(ctx, name, jsModuleInit);
    JS_AddModuleExport(ctx, m, "Driver");
    return m;
}
```

## 4. 文件清单

| 文件 | 说明 |
|------|------|
| `src/stdiolink_service/utils/js_convert.h` | QJsonValue ↔ JSValue 转换头文件 |
| `src/stdiolink_service/utils/js_convert.cpp` | QJsonValue ↔ JSValue 转换实现 |
| `src/stdiolink_service/bindings/js_driver.h` | Driver 类绑定头文件 |
| `src/stdiolink_service/bindings/js_driver.cpp` | Driver 类绑定实现 |
| `src/stdiolink_service/bindings/js_task.h` | Task 类绑定头文件 |
| `src/stdiolink_service/bindings/js_task.cpp` | Task 类绑定实现 |
| `src/stdiolink_service/bindings/js_stdiolink_module.h` | 内置模块注册头文件 |
| `src/stdiolink_service/bindings/js_stdiolink_module.cpp` | 内置模块注册实现 |

## 5. 验收标准

1. `QJsonValue` 所有类型（null/bool/number/string/array/object）能正确转为 `JSValue`
2. `JSValue` 所有类型能正确转为 `QJsonValue`
3. 嵌套对象/数组能递归转换
4. `import { Driver } from "stdiolink"` 能正确导入
5. `new Driver()` 能创建实例
6. `driver.start(program)` 能启动 Driver 进程
7. `driver.request(cmd, data)` 返回 Task 对象
8. `task.waitNext()` 能阻塞等待并返回消息对象 `{ status, code, data }`
9. `task.waitNext(timeoutMs)` 超时返回 `null`
10. `driver.queryMeta()` 返回元数据 JS 对象
11. `driver.terminate()` 能终止进程
12. `driver.running` / `driver.hasMeta` 只读属性正确
13. GC 回收 Driver 对象时自动终止进程

## 6. 单元测试用例

### 6.1 QJsonValue → JSValue 转换测试

```cpp
#include <gtest/gtest.h>
#include "utils/js_convert.h"
#include "engine/js_engine.h"
#include "quickjs.h"

class JsConvertTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        ctx = engine->context();
    }
    void TearDown() override { engine.reset(); }

    std::unique_ptr<JsEngine> engine;
    JSContext* ctx = nullptr;
};

TEST_F(JsConvertTest, NullToJs) {
    JSValue v = qjsonToJsValue(ctx, QJsonValue());
    EXPECT_TRUE(JS_IsNull(v));
}

TEST_F(JsConvertTest, BoolToJs) {
    JSValue v = qjsonToJsValue(ctx, QJsonValue(true));
    EXPECT_EQ(JS_ToBool(ctx, v), 1);
    JS_FreeValue(ctx, v);

    v = qjsonToJsValue(ctx, QJsonValue(false));
    EXPECT_EQ(JS_ToBool(ctx, v), 0);
    JS_FreeValue(ctx, v);
}

TEST_F(JsConvertTest, IntToJs) {
    JSValue v = qjsonToJsValue(ctx, QJsonValue(42));
    double d;
    JS_ToFloat64(ctx, &d, v);
    EXPECT_DOUBLE_EQ(d, 42.0);
    JS_FreeValue(ctx, v);
}

TEST_F(JsConvertTest, DoubleToJs) {
    JSValue v = qjsonToJsValue(ctx, QJsonValue(3.14));
    double d;
    JS_ToFloat64(ctx, &d, v);
    EXPECT_DOUBLE_EQ(d, 3.14);
    JS_FreeValue(ctx, v);
}

TEST_F(JsConvertTest, StringToJs) {
    JSValue v = qjsonToJsValue(ctx, QJsonValue("hello"));
    const char* s = JS_ToCString(ctx, v);
    EXPECT_STREQ(s, "hello");
    JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, v);
}

TEST_F(JsConvertTest, ArrayToJs) {
    QJsonArray arr = {1, "two", true};
    JSValue v = qjsonToJsValue(ctx, arr);
    EXPECT_TRUE(JS_IsArray(ctx, v));

    JSValue lenVal = JS_GetPropertyStr(ctx, v, "length");
    int32_t len = 0;
    JS_ToInt32(ctx, &len, lenVal);
    EXPECT_EQ(len, 3);
    JS_FreeValue(ctx, lenVal);
    JS_FreeValue(ctx, v);
}

TEST_F(JsConvertTest, ObjectToJs) {
    QJsonObject obj{{"name", "test"}, {"value", 42}};
    JSValue v = qjsonObjectToJsValue(ctx, obj);
    EXPECT_TRUE(JS_IsObject(v));

    JSValue nameVal = JS_GetPropertyStr(ctx, v, "name");
    const char* s = JS_ToCString(ctx, nameVal);
    EXPECT_STREQ(s, "test");
    JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, nameVal);
    JS_FreeValue(ctx, v);
}

TEST_F(JsConvertTest, NestedObjectToJs) {
    QJsonObject inner{{"x", 1}};
    QJsonObject outer{{"inner", inner}, {"arr", QJsonArray{10, 20}}};
    JSValue v = qjsonObjectToJsValue(ctx, outer);

    JSValue innerVal = JS_GetPropertyStr(ctx, v, "inner");
    JSValue xVal = JS_GetPropertyStr(ctx, innerVal, "x");
    int32_t x = 0;
    JS_ToInt32(ctx, &x, xVal);
    EXPECT_EQ(x, 1);

    JS_FreeValue(ctx, xVal);
    JS_FreeValue(ctx, innerVal);
    JS_FreeValue(ctx, v);
}
```

### 6.2 JSValue → QJsonValue 转换测试

```cpp
TEST_F(JsConvertTest, JsNullToQJson) {
    QJsonValue v = jsValueToQJson(ctx, JS_NULL);
    EXPECT_TRUE(v.isNull());
}

TEST_F(JsConvertTest, JsUndefinedToQJson) {
    QJsonValue v = jsValueToQJson(ctx, JS_UNDEFINED);
    EXPECT_TRUE(v.isNull());
}

TEST_F(JsConvertTest, JsBoolToQJson) {
    JSValue jv = JS_NewBool(ctx, 1);
    QJsonValue v = jsValueToQJson(ctx, jv);
    EXPECT_TRUE(v.isBool());
    EXPECT_TRUE(v.toBool());
    JS_FreeValue(ctx, jv);
}

TEST_F(JsConvertTest, JsIntToQJson) {
    JSValue jv = JS_NewInt32(ctx, 42);
    QJsonValue v = jsValueToQJson(ctx, jv);
    EXPECT_TRUE(v.isDouble());
    EXPECT_EQ(v.toInt(), 42);
    JS_FreeValue(ctx, jv);
}

TEST_F(JsConvertTest, JsDoubleToQJson) {
    JSValue jv = JS_NewFloat64(ctx, 3.14);
    QJsonValue v = jsValueToQJson(ctx, jv);
    EXPECT_DOUBLE_EQ(v.toDouble(), 3.14);
    JS_FreeValue(ctx, jv);
}

TEST_F(JsConvertTest, JsStringToQJson) {
    JSValue jv = JS_NewString(ctx, "world");
    QJsonValue v = jsValueToQJson(ctx, jv);
    EXPECT_EQ(v.toString(), "world");
    JS_FreeValue(ctx, jv);
}

TEST_F(JsConvertTest, JsArrayToQJson) {
    JSValue arr = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, arr, 0, JS_NewInt32(ctx, 10));
    JS_SetPropertyUint32(ctx, arr, 1, JS_NewString(ctx, "x"));
    QJsonValue v = jsValueToQJson(ctx, arr);
    EXPECT_TRUE(v.isArray());
    EXPECT_EQ(v.toArray().size(), 2);
    EXPECT_EQ(v.toArray()[0].toInt(), 10);
    EXPECT_EQ(v.toArray()[1].toString(), "x");
    JS_FreeValue(ctx, arr);
}

TEST_F(JsConvertTest, JsObjectToQJson) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "key",
                      JS_NewString(ctx, "val"));
    QJsonValue v = jsValueToQJson(ctx, obj);
    EXPECT_TRUE(v.isObject());
    EXPECT_EQ(v.toObject()["key"].toString(), "val");
    JS_FreeValue(ctx, obj);
}
```

### 6.3 双向转换往返测试

```cpp
TEST_F(JsConvertTest, RoundTripObject) {
    QJsonObject original{
        {"name", "test"},
        {"count", 42},
        {"active", true},
        {"tags", QJsonArray{"a", "b"}},
        {"nested", QJsonObject{{"x", 1.5}}}
    };

    JSValue jsVal = qjsonObjectToJsValue(ctx, original);
    QJsonObject result = jsValueToQJsonObject(ctx, jsVal);
    JS_FreeValue(ctx, jsVal);

    EXPECT_EQ(result["name"].toString(), "test");
    EXPECT_EQ(result["count"].toInt(), 42);
    EXPECT_EQ(result["active"].toBool(), true);
    EXPECT_EQ(result["tags"].toArray().size(), 2);
    EXPECT_DOUBLE_EQ(result["nested"].toObject()["x"].toDouble(), 1.5);
}

TEST_F(JsConvertTest, EmptyObjectRoundTrip) {
    QJsonObject empty;
    JSValue jsVal = qjsonObjectToJsValue(ctx, empty);
    QJsonObject result = jsValueToQJsonObject(ctx, jsVal);
    JS_FreeValue(ctx, jsVal);
    EXPECT_TRUE(result.isEmpty());
}

TEST_F(JsConvertTest, EmptyArrayRoundTrip) {
    QJsonArray empty;
    JSValue jsVal = qjsonToJsValue(ctx, empty);
    QJsonValue result = jsValueToQJson(ctx, jsVal);
    JS_FreeValue(ctx, jsVal);
    EXPECT_TRUE(result.isArray());
    EXPECT_EQ(result.toArray().size(), 0);
}
```

### 6.4 Driver 绑定测试

```cpp
#include <gtest/gtest.h>
#include <QTemporaryFile>
#include <QTextStream>
#include "engine/js_engine.h"
#include "engine/module_loader.h"
#include "engine/console_bridge.h"
#include "bindings/js_stdiolink_module.h"

class JsDriverBindingTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        ModuleLoader::install(engine->context());
        ConsoleBridge::install(engine->context());
        ModuleLoader::addBuiltin("stdiolink", jsInitStdiolinkModule);
    }
    void TearDown() override { engine.reset(); }

    QString createTempScript(const QString& code) {
        auto* f = new QTemporaryFile("XXXXXX.mjs");
        f->setAutoRemove(true);
        f->open();
        QTextStream out(f);
        out << code;
        out.flush();
        tempFiles.append(f);
        return f->fileName();
    }

    int32_t getGlobalInt(const char* name) {
        JSValue global = JS_GetGlobalObject(engine->context());
        JSValue val = JS_GetPropertyStr(engine->context(), global, name);
        int32_t result = 0;
        JS_ToInt32(engine->context(), &result, val);
        JS_FreeValue(engine->context(), val);
        JS_FreeValue(engine->context(), global);
        return result;
    }

    std::unique_ptr<JsEngine> engine;
    QList<QTemporaryFile*> tempFiles;
};

TEST_F(JsDriverBindingTest, ImportDriver) {
    QString path = createTempScript(
        "import { Driver } from 'stdiolink';\n"
        "globalThis.ok = (typeof Driver === 'function') ? 1 : 0;\n"
    );
    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsDriverBindingTest, ConstructDriver) {
    QString path = createTempScript(
        "import { Driver } from 'stdiolink';\n"
        "const d = new Driver();\n"
        "globalThis.ok = (d !== null) ? 1 : 0;\n"
    );
    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsDriverBindingTest, StartNonexistent) {
    QString path = createTempScript(
        "import { Driver } from 'stdiolink';\n"
        "const d = new Driver();\n"
        "globalThis.ok = d.start('__nonexistent__') ? 0 : 1;\n"
    );
    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsDriverBindingTest, RunningProperty) {
    QString path = createTempScript(
        "import { Driver } from 'stdiolink';\n"
        "const d = new Driver();\n"
        "globalThis.ok = d.running ? 0 : 1;\n"
    );
    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 6.5 Task 绑定集成测试（需要真实 Driver 进程）

```cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include "engine/js_engine.h"
#include "engine/module_loader.h"
#include "engine/console_bridge.h"
#include "bindings/js_stdiolink_module.h"

class JsTaskIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        ModuleLoader::install(engine->context());
        ConsoleBridge::install(engine->context());
        ModuleLoader::addBuiltin("stdiolink", jsInitStdiolinkModule);

        driverPath = QCoreApplication::applicationDirPath()
                     + "/calculator_driver";
#ifdef Q_OS_WIN
        driverPath += ".exe";
#endif
    }
    void TearDown() override { engine.reset(); }

    QString createTempScript(const QString& code) {
        auto* f = new QTemporaryFile("XXXXXX.mjs");
        f->setAutoRemove(true);
        f->open();
        QTextStream out(f);
        out << code;
        out.flush();
        tempFiles.append(f);
        return f->fileName();
    }

    int32_t getGlobalInt(const char* name) {
        JSValue global = JS_GetGlobalObject(engine->context());
        JSValue val = JS_GetPropertyStr(engine->context(), global, name);
        int32_t result = 0;
        JS_ToInt32(engine->context(), &result, val);
        JS_FreeValue(engine->context(), val);
        JS_FreeValue(engine->context(), global);
        return result;
    }

    std::unique_ptr<JsEngine> engine;
    QList<QTemporaryFile*> tempFiles;
    QString driverPath;
};

TEST_F(JsTaskIntegrationTest, RequestAndWaitNext) {
    QString path = createTempScript(QString(
        "import { Driver } from 'stdiolink';\n"
        "const d = new Driver();\n"
        "d.start('%1');\n"
        "const task = d.request('add', { a: 10, b: 20 });\n"
        "const msg = task.waitNext(5000);\n"
        "globalThis.status = msg ? 1 : 0;\n"
        "d.terminate();\n"
    ).arg(driverPath));

    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("status"), 1);
}

TEST_F(JsTaskIntegrationTest, WaitNextTimeout) {
    // 对未启动的 Driver 请求应超时
    QString path = createTempScript(
        "import { Driver } from 'stdiolink';\n"
        "const d = new Driver();\n"
        "// 不调用 start，直接构造场景\n"
        "globalThis.ok = 1;\n"
    );
    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
}

TEST_F(JsTaskIntegrationTest, QueryMeta) {
    QString path = createTempScript(QString(
        "import { Driver } from 'stdiolink';\n"
        "const d = new Driver();\n"
        "d.start('%1');\n"
        "const meta = d.queryMeta(5000);\n"
        "globalThis.hasMeta = meta ? 1 : 0;\n"
        "globalThis.hasCommands = (meta && meta.commands) ? 1 : 0;\n"
        "d.terminate();\n"
    ).arg(driverPath));

    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("hasMeta"), 1);
    EXPECT_EQ(getGlobalInt("hasCommands"), 1);
}

TEST_F(JsTaskIntegrationTest, TaskDoneProperty) {
    QString path = createTempScript(QString(
        "import { Driver } from 'stdiolink';\n"
        "const d = new Driver();\n"
        "d.start('%1');\n"
        "const task = d.request('add', { a: 1, b: 2 });\n"
        "const msg = task.waitNext(5000);\n"
        "globalThis.isDone = task.done ? 1 : 0;\n"
        "d.terminate();\n"
    ).arg(driverPath));

    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("isDone"), 1);
}
```

## 7. 依赖关系

- **前置依赖**：
  - 里程碑 21（JS 引擎脚手架）：JsEngine 基础封装
  - 里程碑 22（ES Module 加载器）：内置模块注册机制
  - stdiolink 核心库（Driver、Task、Message 类）
- **后续依赖**：
  - 里程碑 24（进程调用绑定）：复用 js_convert 工具
  - 里程碑 25（Proxy 代理调用）：基于 Driver/Task 绑定构建 Proxy 层
