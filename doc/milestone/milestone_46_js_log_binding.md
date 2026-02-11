# 里程碑 46：JS 结构化日志绑定

> **前置条件**: 里程碑 45 已完成
> **目标**: 新增 `stdiolink/log` 模块，提供稳定的结构化日志 API，提升服务排障能力

---

## 1. 目标

- 新增 `stdiolink/log` 模块
- 支持 `debug/info/warn/error` 和 `child(baseFields)`
- 输出结构化日志（JSON line）到 Qt 日志通道

---

## 2. 设计原则（强约束）

- **简约**: 统一 API，不拆分多套 logger 实现
- **可靠**: 日志序列化失败不影响主流程
- **稳定**: 日志字段结构固定
- **避免过度设计**: 不做日志采样、远程上报、动态级别热更新

---

## 3. 范围与非目标

### 3.1 范围（M46 内）

- `stdiolink/log` 模块
- 结构化日志输出格式
- `child` 字段继承机制
- 单元测试与文档

### 3.2 非目标（M46 外）

- 不接入第三方日志后端（ELK/OTel）
- 不实现日志文件轮转（由外层系统处理）

---

## 4. 技术方案

### 4.1 模块接口

> **与技术路线偏差说明**：技术路线 P1-2 原定导出顶层函数 `debug/info/warn/error/child`。
> 经评估，改为工厂模式 `createLogger()`，优势：支持基础字段注入、`child` 继承链更自然、
> 避免模块级全局状态。此变更不影响其他模块。

```js
import { createLogger } from "stdiolink/log";

const log = createLogger({ service: "demo" });
log.info("started", { projectId: "p1" });
```

导出：

- `createLogger(baseFields?)`：创建 Logger 实例，`baseFields` 可选（默认空对象）
- Logger 实例方法：
  - `debug(msg, fields?)`
  - `info(msg, fields?)`
  - `warn(msg, fields?)`
  - `error(msg, fields?)`
  - `child(extraFields)`：返回新 Logger，继承父级 baseFields 并合并 extraFields

### 4.2 输出格式

统一 JSON line 字段：

- `ts`
- `level`
- `msg`
- `fields`

映射到 Qt：

- `debug -> qDebug`
- `info -> qInfo`
- `warn -> qWarning`
- `error -> qCritical`

### 4.3 创建 bindings/js_log.h

```cpp
#pragma once

#include <quickjs.h>

namespace stdiolink_service {

/// @brief stdiolink/log 内置模块绑定
///
/// 提供结构化日志 API（createLogger → Logger 实例）。
/// Logger 对象通过 QuickJS class 机制实现，支持 child 继承链。
/// 日志输出为 JSON line 格式，映射到 Qt 日志通道。
/// 无需 runtime 级状态隔离（Logger 自身持有 baseFields）。
class JsLogBinding {
public:
    /// 模块初始化回调（注册给 ModuleLoader）
    static JSModuleDef* initModule(JSContext* ctx, const char* name);

    /// 注册 Logger class（在 initModule 内部调用）
    static void registerLoggerClass(JSContext* ctx);
};

} // namespace stdiolink_service
```

### 4.4 创建 bindings/js_log.cpp（关键实现）

```cpp
#include "js_log.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <quickjs.h>

#include "utils/js_convert.h"

namespace stdiolink_service {

namespace {

/// Logger 实例的 opaque 数据
struct LoggerData {
    QJsonObject baseFields;
};

JSClassID s_loggerClassId = 0;

void loggerFinalizer(JSRuntime*, JSValue val) {
    auto* data = static_cast<LoggerData*>(
        JS_GetOpaque(val, s_loggerClassId));
    delete data;
}

JSClassDef s_loggerClassDef = {
    "Logger",
    loggerFinalizer,
    nullptr, nullptr, nullptr
};

/// 将 JS 值转为 QString（非字符串自动 toString）
QString valueToString(JSContext* ctx, JSValueConst val) {
    if (JS_IsString(val)) {
        const char* s = JS_ToCString(ctx, val);
        QString result = QString::fromUtf8(s);
        JS_FreeCString(ctx, s);
        return result;
    }
    // 非字符串：尝试 toString
    JSValue str = JS_ToString(ctx, val);
    if (JS_IsException(str)) return QStringLiteral("[object]");
    const char* s = JS_ToCString(ctx, str);
    QString result = QString::fromUtf8(s);
    JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, str);
    return result;
}

/// 核心日志输出函数
JSValue emitLog(JSContext* ctx, const char* level,
                JSValueConst thisVal,
                int argc, JSValueConst* argv) {
    auto* data = static_cast<LoggerData*>(
        JS_GetOpaque(thisVal, s_loggerClassId));
    if (!data) {
        return JS_ThrowTypeError(ctx,
            "log.%s: invalid logger", level);
    }

    // msg 参数（必填，非字符串自动转换）
    QString msg;
    if (argc >= 1) {
        msg = valueToString(ctx, argv[0]);
    }

    // fields 参数（可选，必须为对象）
    QJsonObject mergedFields = data->baseFields;
    if (argc >= 2 && JS_IsObject(argv[1])
        && !JS_IsNull(argv[1])) {
        QJsonObject callFields =
            jsValueToQJsonObject(ctx, argv[1]);
        // 调用时字段覆盖 baseFields
        for (auto it = callFields.begin();
             it != callFields.end(); ++it) {
            mergedFields[it.key()] = it.value();
        }
    } else if (argc >= 2 && !JS_IsUndefined(argv[1])
               && !JS_IsNull(argv[1])) {
        return JS_ThrowTypeError(ctx,
            "log.%s: fields must be an object", level);
    }

    // 构建 JSON line
    QJsonObject logObj;
    logObj["ts"] = QDateTime::currentDateTimeUtc()
                       .toString(Qt::ISODateWithMs);
    logObj["level"] = QString::fromUtf8(level);
    logObj["msg"] = msg;
    if (!mergedFields.isEmpty()) {
        logObj["fields"] = mergedFields;
    }

    QByteArray line = QJsonDocument(logObj)
                          .toJson(QJsonDocument::Compact);

    // 映射到 Qt 日志通道
    if (qstrcmp(level, "debug") == 0) {
        qDebug().noquote() << line;
    } else if (qstrcmp(level, "info") == 0) {
        qInfo().noquote() << line;
    } else if (qstrcmp(level, "warn") == 0) {
        qWarning().noquote() << line;
    } else if (qstrcmp(level, "error") == 0) {
        qCritical().noquote() << line;
    }
    return JS_UNDEFINED;
}

JSValue jsLogDebug(JSContext* ctx, JSValueConst thisVal,
                   int argc, JSValueConst* argv) {
    return emitLog(ctx, "debug", thisVal, argc, argv);
}

JSValue jsLogInfo(JSContext* ctx, JSValueConst thisVal,
                  int argc, JSValueConst* argv) {
    return emitLog(ctx, "info", thisVal, argc, argv);
}

JSValue jsLogWarn(JSContext* ctx, JSValueConst thisVal,
                  int argc, JSValueConst* argv) {
    return emitLog(ctx, "warn", thisVal, argc, argv);
}

JSValue jsLogError(JSContext* ctx, JSValueConst thisVal,
                   int argc, JSValueConst* argv) {
    return emitLog(ctx, "error", thisVal, argc, argv);
}

/// 创建 Logger JS 对象（内部复用）
JSValue createLoggerObject(JSContext* ctx,
                           const QJsonObject& baseFields) {
    JSValue obj = JS_NewObjectClass(ctx, s_loggerClassId);
    if (JS_IsException(obj)) return obj;

    auto* data = new LoggerData{baseFields};
    JS_SetOpaque(obj, data);

    JS_SetPropertyStr(ctx, obj, "debug",
        JS_NewCFunction(ctx, jsLogDebug, "debug", 2));
    JS_SetPropertyStr(ctx, obj, "info",
        JS_NewCFunction(ctx, jsLogInfo, "info", 2));
    JS_SetPropertyStr(ctx, obj, "warn",
        JS_NewCFunction(ctx, jsLogWarn, "warn", 2));
    JS_SetPropertyStr(ctx, obj, "error",
        JS_NewCFunction(ctx, jsLogError, "error", 2));

    // child 方法通过闭包捕获当前 baseFields
    // 使用 JS 包装实现更自然
    return obj;
}

JSValue jsChild(JSContext* ctx, JSValueConst thisVal,
                int argc, JSValueConst* argv) {
    auto* data = static_cast<LoggerData*>(
        JS_GetOpaque(thisVal, s_loggerClassId));
    if (!data) {
        return JS_ThrowTypeError(ctx,
            "log.child: invalid logger");
    }

    QJsonObject merged = data->baseFields;
    if (argc >= 1 && JS_IsObject(argv[0])
        && !JS_IsNull(argv[0])) {
        QJsonObject extra =
            jsValueToQJsonObject(ctx, argv[0]);
        for (auto it = extra.begin();
             it != extra.end(); ++it) {
            merged[it.key()] = it.value();
        }
    } else if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "log.child: extraFields must be an object");
    }

    JSValue child = createLoggerObject(ctx, merged);
    if (JS_IsException(child)) return child;
    JS_SetPropertyStr(ctx, child, "child",
        JS_NewCFunction(ctx, jsChild, "child", 1));
    return child;
}

JSValue jsCreateLogger(JSContext* ctx, JSValueConst,
                       int argc, JSValueConst* argv) {
    QJsonObject baseFields;
    if (argc >= 1 && JS_IsObject(argv[0])
        && !JS_IsNull(argv[0])) {
        baseFields = jsValueToQJsonObject(ctx, argv[0]);
    }

    JSValue logger = createLoggerObject(ctx, baseFields);
    if (JS_IsException(logger)) return logger;
    JS_SetPropertyStr(ctx, logger, "child",
        JS_NewCFunction(ctx, jsChild, "child", 1));
    return logger;
}

int logModuleInit(JSContext* ctx, JSModuleDef* module) {
    JS_SetModuleExport(ctx, module, "createLogger",
        JS_NewCFunction(ctx, jsCreateLogger,
                        "createLogger", 1));
    return 0;
}

} // namespace

void JsLogBinding::registerLoggerClass(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    if (s_loggerClassId == 0) {
        JS_NewClassID(rt, &s_loggerClassId);
        JS_NewClass(rt, s_loggerClassId, &s_loggerClassDef);
    }
}

JSModuleDef* JsLogBinding::initModule(JSContext* ctx,
                                       const char* name) {
    registerLoggerClass(ctx);
    JSModuleDef* module = JS_NewCModule(ctx, name,
                                         logModuleInit);
    if (!module) return nullptr;
    JS_AddModuleExport(ctx, module, "createLogger");
    return module;
}

} // namespace stdiolink_service
```

### 4.5 main.cpp 集成

```cpp
#include "bindings/js_log.h"

// 模块注册（在 stdiolink/http 之后）：
engine.registerModule("stdiolink/log", JsLogBinding::initModule);
```

### 4.6 JS 使用示例

```js
import { createLogger } from "stdiolink/log";

// 创建带基础字段的 logger
const log = createLogger({ service: "demo", version: "1.0" });

log.info("service started");
// → {"ts":"2025-01-01T00:00:00.000Z","level":"info",
//    "msg":"service started","fields":{"service":"demo","version":"1.0"}}

log.warn("high latency", { latencyMs: 500 });
// → fields 合并：{"service":"demo","version":"1.0","latencyMs":500}

// child 继承
const reqLog = log.child({ requestId: "r-123" });
reqLog.info("processing");
// → fields: {"service":"demo","version":"1.0","requestId":"r-123"}

// 多级 child
const subLog = reqLog.child({ step: "validate" });
subLog.debug("checking input");
// → fields: {"service":"demo","version":"1.0","requestId":"r-123","step":"validate"}

// 无参数创建
const bare = createLogger();
bare.error("something failed");
// → {"ts":"...","level":"error","msg":"something failed"}
```

---

## 5. 实现步骤

1. 新增 `js_log` 绑定并注册模块
2. 实现 logger 对象与 child 继承
3. 实现 JSON 序列化与输出
4. 编写日志捕获测试
5. 更新 manual 文档

---

## 6. 文件改动清单

### 6.1 新增文件

- `src/stdiolink_service/bindings/js_log.h`
- `src/stdiolink_service/bindings/js_log.cpp`
- `src/tests/test_log_binding.cpp`
- `doc/manual/10-js-service/log-binding.md`

### 6.2 修改文件

- `src/stdiolink_service/main.cpp`
- `src/stdiolink_service/CMakeLists.txt`
- `src/tests/CMakeLists.txt`
- `doc/manual/10-js-service/module-system.md`
- `doc/manual/10-js-service/README.md`

---

## 7. 单元测试计划（全面覆盖）

新增测试文件：`test_log_binding.cpp`

### 7.1 测试 Fixture

```cpp
// test_log_binding.cpp
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include "engine/js_engine.h"
#include "engine/module_loader.h"
#include "engine/console_bridge.h"
#include "bindings/js_log.h"

using namespace stdiolink_service;

class JsLogTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        ModuleLoader::install(engine->context());
        ConsoleBridge::install(engine->context());
        ModuleLoader::addBuiltin("stdiolink/log",
                                 JsLogBinding::initModule);
        capturedLines.clear();
        // 安装 Qt 消息处理器捕获日志输出
        previousHandler = qInstallMessageHandler(
            logCapture);
    }
    void TearDown() override {
        qInstallMessageHandler(previousHandler);
        engine.reset();
    }

    static void logCapture(QtMsgType type,
                           const QMessageLogContext&,
                           const QString& msg) {
        Q_UNUSED(type);
        capturedLines.append(msg.trimmed());
    }

    int runScript(const QString& code) {
        QTemporaryDir dir;
        QString path = dir.path() + "/test.mjs";
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(code.toUtf8());
        f.close();
        return engine->evalFile(path);
    }

    int32_t getGlobalInt(const char* name) {
        JSValue g = JS_GetGlobalObject(engine->context());
        JSValue v = JS_GetPropertyStr(
            engine->context(), g, name);
        int32_t r = 0;
        JS_ToInt32(engine->context(), &r, v);
        JS_FreeValue(engine->context(), v);
        JS_FreeValue(engine->context(), g);
        return r;
    }

    /// 解析最后一条捕获的日志为 JSON
    QJsonObject lastLogJson() const {
        if (capturedLines.isEmpty()) return {};
        return QJsonDocument::fromJson(
            capturedLines.last().toUtf8()).object();
    }

    std::unique_ptr<JsEngine> engine;
    static QStringList capturedLines;
    static QtMessageHandler previousHandler;
};

QStringList JsLogTest::capturedLines;
QtMessageHandler JsLogTest::previousHandler = nullptr;
```

### 7.2 基础输出

```cpp
TEST_F(JsLogTest, InfoOutputsJsonLine) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger();\n"
        "log.info('hello');\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
    QJsonObject obj = lastLogJson();
    EXPECT_EQ(obj["level"].toString(), "info");
    EXPECT_EQ(obj["msg"].toString(), "hello");
    EXPECT_FALSE(obj["ts"].toString().isEmpty());
}

TEST_F(JsLogTest, AllFourLevelsWork) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger();\n"
        "log.debug('d');\n"
        "log.info('i');\n"
        "log.warn('w');\n"
        "log.error('e');\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_GE(capturedLines.size(), 4);
    // 验证最后四条日志的 level
    QStringList levels;
    for (int i = capturedLines.size() - 4;
         i < capturedLines.size(); ++i) {
        QJsonObject obj = QJsonDocument::fromJson(
            capturedLines[i].toUtf8()).object();
        levels.append(obj["level"].toString());
    }
    EXPECT_TRUE(levels.contains("debug"));
    EXPECT_TRUE(levels.contains("info"));
    EXPECT_TRUE(levels.contains("warn"));
    EXPECT_TRUE(levels.contains("error"));
}

TEST_F(JsLogTest, OutputContainsTsLevelMsgFields) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger({ svc: 'test' });\n"
        "log.info('msg', { key: 'val' });\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    QJsonObject obj = lastLogJson();
    EXPECT_TRUE(obj.contains("ts"));
    EXPECT_TRUE(obj.contains("level"));
    EXPECT_TRUE(obj.contains("msg"));
    EXPECT_TRUE(obj.contains("fields"));
    QJsonObject fields = obj["fields"].toObject();
    EXPECT_EQ(fields["svc"].toString(), "test");
    EXPECT_EQ(fields["key"].toString(), "val");
}
```

### 7.3 字段继承

```cpp
TEST_F(JsLogTest, BaseFieldsIncluded) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger({ service: 'demo' });\n"
        "log.info('test');\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    QJsonObject fields = lastLogJson()["fields"].toObject();
    EXPECT_EQ(fields["service"].toString(), "demo");
}

TEST_F(JsLogTest, ChildInheritsAndMerges) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger({ a: 1 });\n"
        "const child = log.child({ b: 2 });\n"
        "child.info('test');\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    QJsonObject fields = lastLogJson()["fields"].toObject();
    EXPECT_EQ(fields["a"].toInt(), 1);
    EXPECT_EQ(fields["b"].toInt(), 2);
}

TEST_F(JsLogTest, ChildChainMergesCorrectly) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger({ a: 1 });\n"
        "const c1 = log.child({ b: 2 });\n"
        "const c2 = c1.child({ c: 3 });\n"
        "c2.info('test');\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    QJsonObject fields = lastLogJson()["fields"].toObject();
    EXPECT_EQ(fields["a"].toInt(), 1);
    EXPECT_EQ(fields["b"].toInt(), 2);
    EXPECT_EQ(fields["c"].toInt(), 3);
}

TEST_F(JsLogTest, CallFieldsOverrideBaseFields) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger({ key: 'base' });\n"
        "const child = log.child({ key: 'child' });\n"
        "child.info('test', { key: 'call' });\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    QJsonObject fields = lastLogJson()["fields"].toObject();
    // 调用时字段优先级最高
    EXPECT_EQ(fields["key"].toString(), "call");
}

TEST_F(JsLogTest, ChildOverridesParentField) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger({ key: 'parent' });\n"
        "const child = log.child({ key: 'child' });\n"
        "child.info('test');\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    QJsonObject fields = lastLogJson()["fields"].toObject();
    EXPECT_EQ(fields["key"].toString(), "child");
}
```

### 7.4 稳定性与容错

```cpp
TEST_F(JsLogTest, FieldsNonObjectThrowsTypeError) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger();\n"
        "try { log.info('msg', 'not-object');\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = (e instanceof TypeError) ? 1 : 0;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsLogTest, MsgNonStringAutoConverts) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger();\n"
        "log.info(42);\n"
        "log.info({ key: 'val' });\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
    // 不崩溃即通过
}

TEST_F(JsLogTest, CreateLoggerNoArgsWorks) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger();\n"
        "log.info('bare logger');\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
    QJsonObject obj = lastLogJson();
    EXPECT_EQ(obj["msg"].toString(), "bare logger");
}

TEST_F(JsLogTest, FieldsNullOrUndefinedIgnored) {
    int ret = runScript(
        "import { createLogger } from 'stdiolink/log';\n"
        "const log = createLogger();\n"
        "log.info('test', null);\n"
        "log.info('test', undefined);\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.5 回归测试

- 与 `console.*` 并存，不影响现有日志桥接测试

---

## 8. 验收标准（DoD）

- `stdiolink/log` 可用，输出结构一致
- `child` 行为符合预期
- 新增与回归测试全部通过

---

## 9. 风险与控制

- **风险 1**：日志格式随实现变化漂移
  - 控制：测试固定字段集合与类型
- **风险 2**：字段合并冲突导致信息丢失
  - 控制：文档明确覆盖优先级（调用时字段优先）
