# 里程碑 21：JS 引擎脚手架与基础能力

## 1. 目标

搭建 `stdiolink_service` 可执行文件项目，集成 quickjs-ng 引擎，实现 JsEngine RAII 封装、console 桥接和 main.cpp 入口，使其能加载并运行简单 JS 脚本。

## 2. 技术要点

### 2.1 quickjs-ng 集成

- 通过项目已有的 vcpkg 包管理方案引入 quickjs-ng（端口名 `quickjs-ng`），静态链接
- vcpkg baseline 锁定版本，确保构建可复现
- 顶层 CMakeLists.txt 添加 `find_package(qjs CONFIG REQUIRED)`
- 链接 stdiolink 核心库、Qt::Core 和 qjs
- **遇到编译问题时以 quickjs-ng 的头文件为准**，不要参考官方 QuickJS 文档

> **quickjs-ng 与官方 QuickJS 的关键差异**
>
> quickjs-ng 是 QuickJS 的活跃维护分支，API 和行为与官方版本存在多处不兼容，开发时务必注意：
>
> | 差异项 | 官方 QuickJS | quickjs-ng |
> |--------|-------------|------------|
> | `JS_NewClassID` | 全局静态分配，`JS_NewClassID(&id)` | **基于 Runtime 分配**，`JS_NewClassID(rt, &id)`，解决全局 ID 耗尽问题 |
> | 构建系统 | 纯 Makefile | **CMake 构建系统**，易于跨平台集成（vcpkg 已封装） |
> | ES 标准兼容性 | ES2020 部分支持 | **ES2023+ 更完善**，Test262 通过率大幅提升（Atomics、WeakRef 等） |
> | Promise Job 队列 | 执行时机有 bug | **修复了执行时机**，Top-level await 支持更健壮 |
> | 非标扩展 | BigFloat / BigDecimal | **已移除**，专注标准 BigInt 实现 |
> | Error 堆栈 | 基础堆栈信息 | **优化堆栈生成**，调用栈更准确、可读性更高 |
> | 模块解析 | 基础模块加载 | **增强模块加载器**，支持更灵活的路径解析逻辑 |
>
> **实践要点**：
> - 所有 QuickJS API 调用以 `#include <quickjs.h>` 中的实际签名为准
> - 不要从网上搜索官方 QuickJS 的用法直接套用，签名可能不同
> - `JS_NewClassID` 必须传入 `JSRuntime*` 参数，这是最常见的编译错误来源

### 2.2 JsEngine RAII 封装

- 创建/销毁 QuickJS Runtime 和 Context
- 配置内存限制、栈大小
- 以 ES Module 模式加载并执行入口文件
- Promise job pump：脚本执行后循环调用 `JS_ExecutePendingJob()` 驱动 microtask 队列
- 统一错误处理：捕获 JS 异常并输出到 stderr

### 2.3 console 桥接

- `console.log/info/warn/error` 映射到 `qDebug/qInfo/qWarning/qCritical`
- 支持多参数拼接
- 对象/数组参数使用 `JSON.stringify` 格式化

### 2.4 main.cpp 入口

- 命令行参数解析（`--help`、`--version`、`<script.js>`）
- QCoreApplication 初始化
- 退出码：0 正常、1 JS 错误、2 参数/文件错误

## 3. 实现步骤

### 3.1 CMakeLists.txt

```cmake
# src/stdiolink_service/CMakeLists.txt

add_executable(stdiolink_service
    main.cpp
    engine/js_engine.cpp
    engine/console_bridge.cpp
)

target_link_libraries(stdiolink_service PRIVATE
    stdiolink
    Qt::Core
    qjs                # QuickJS-NG 引擎（vcpkg: quickjs-ng）
)
```

> **vcpkg 集成说明**：quickjs-ng 已添加到项目 `vcpkg.json` 依赖列表，顶层 `CMakeLists.txt` 通过 `find_package(qjs CONFIG REQUIRED)` 引入，子项目直接链接 `qjs` 即可。

### 3.2 JsEngine 类

```cpp
// engine/js_engine.h
#pragma once

#include <QString>

struct JSRuntime;
struct JSContext;
struct JSModuleDef;

class JsEngine {
public:
    JsEngine();
    ~JsEngine();

    // 注册内置模块（在 eval 之前调用）
    void registerModule(const QString& name,
                        JSModuleDef* (*init)(JSContext*, const char*));

    // 执行脚本文件（作为 ES Module）
    int evalFile(const QString& filePath);

    // 驱动 Promise microtask 队列，返回是否还有待执行任务
    bool executePendingJobs();

    // 查询是否有待执行的 Promise job（不执行，仅查询）
    bool hasPendingJobs() const;

    JSContext* context() const;
    JSRuntime* runtime() const;

private:
    JSRuntime* m_rt = nullptr;
    JSContext* m_ctx = nullptr;
};
```

```cpp
// engine/js_engine.cpp
#include "js_engine.h"
#include <QFile>
#include <QDebug>
#include <quickjs.h>

JsEngine::JsEngine() {
    m_rt = JS_NewRuntime();
    m_ctx = JS_NewContext(m_rt);
    // 配置内存限制和栈大小
    JS_SetMemoryLimit(m_rt, 256 * 1024 * 1024); // 256MB
    JS_SetMaxStackSize(m_rt, 8 * 1024 * 1024);  // 8MB
}

JsEngine::~JsEngine() {
    if (m_ctx) JS_FreeContext(m_ctx);
    if (m_rt) JS_FreeRuntime(m_rt);
}

int JsEngine::evalFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical() << "Cannot open file:" << filePath;
        return 2;
    }
    QByteArray code = file.readAll();
    file.close();

    JSValue val = JS_Eval(m_ctx, code.constData(), code.size(),
                          filePath.toUtf8().constData(),
                          JS_EVAL_TYPE_MODULE);
    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(m_ctx);
        const char* str = JS_ToCString(m_ctx, exc);
        if (str) {
            qCritical() << str;
            JS_FreeCString(m_ctx, str);
        }
        // 输出栈信息
        JSValue stack = JS_GetPropertyStr(m_ctx, exc, "stack");
        if (!JS_IsUndefined(stack)) {
            const char* stackStr = JS_ToCString(m_ctx, stack);
            if (stackStr) {
                qCritical() << stackStr;
                JS_FreeCString(m_ctx, stackStr);
            }
        }
        JS_FreeValue(m_ctx, stack);
        JS_FreeValue(m_ctx, exc);
        JS_FreeValue(m_ctx, val);
        return 1;
    }
    JS_FreeValue(m_ctx, val);
    return 0;
}

bool JsEngine::executePendingJobs() {
    JSContext* pctx = nullptr;
    int ret = JS_ExecutePendingJob(m_rt, &pctx);
    if (ret < 0) {
        // 异常处理
        JSValue exc = JS_GetException(pctx);
        const char* str = JS_ToCString(pctx, exc);
        if (str) {
            qCritical() << "Promise error:" << str;
            JS_FreeCString(pctx, str);
        }
        JS_FreeValue(pctx, exc);
    }
    return ret > 0; // 还有待执行任务
}

bool JsEngine::hasPendingJobs() const {
    return JS_IsJobPending(m_rt);
}

JSContext* JsEngine::context() const { return m_ctx; }
JSRuntime* JsEngine::runtime() const { return m_rt; }
```

### 3.3 console 桥接

```cpp
// engine/console_bridge.h
#pragma once

struct JSContext;

class ConsoleBridge {
public:
    static void install(JSContext* ctx);
};
```

```cpp
// engine/console_bridge.cpp
#include "console_bridge.h"
#include <QDebug>
#include <quickjs.h>

// 将多个 JS 参数拼接为字符串
static QString argsToString(JSContext* ctx, int argc, JSValue* argv) {
    QStringList parts;
    for (int i = 0; i < argc; i++) {
        if (JS_IsObject(argv[i])) {
            JSValue json = JS_JSONStringify(ctx, argv[i], JS_UNDEFINED, JS_UNDEFINED);
            const char* s = JS_ToCString(ctx, json);
            if (s) {
                parts << QString::fromUtf8(s);
                JS_FreeCString(ctx, s);
            }
            JS_FreeValue(ctx, json);
        } else {
            const char* s = JS_ToCString(ctx, argv[i]);
            if (s) {
                parts << QString::fromUtf8(s);
                JS_FreeCString(ctx, s);
            }
        }
    }
    return parts.join(' ');
}

static JSValue jsConsoleLog(JSContext* ctx, JSValue, int argc, JSValue* argv) {
    qDebug().noquote() << argsToString(ctx, argc, argv);
    return JS_UNDEFINED;
}

static JSValue jsConsoleInfo(JSContext* ctx, JSValue, int argc, JSValue* argv) {
    qInfo().noquote() << argsToString(ctx, argc, argv);
    return JS_UNDEFINED;
}

static JSValue jsConsoleWarn(JSContext* ctx, JSValue, int argc, JSValue* argv) {
    qWarning().noquote() << argsToString(ctx, argc, argv);
    return JS_UNDEFINED;
}

static JSValue jsConsoleError(JSContext* ctx, JSValue, int argc, JSValue* argv) {
    qCritical().noquote() << argsToString(ctx, argc, argv);
    return JS_UNDEFINED;
}

void ConsoleBridge::install(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, console, "log",
        JS_NewCFunction(ctx, jsConsoleLog, "log", 0));
    JS_SetPropertyStr(ctx, console, "info",
        JS_NewCFunction(ctx, jsConsoleInfo, "info", 0));
    JS_SetPropertyStr(ctx, console, "warn",
        JS_NewCFunction(ctx, jsConsoleWarn, "warn", 0));
    JS_SetPropertyStr(ctx, console, "error",
        JS_NewCFunction(ctx, jsConsoleError, "error", 0));

    JS_SetPropertyStr(ctx, global, "console", console);
    JS_FreeValue(ctx, global);
}
```

### 3.4 main.cpp

```cpp
#include <QCoreApplication>
#include <QFile>
#include <QDebug>
#include "engine/js_engine.h"
#include "engine/console_bridge.h"

static void printHelp() {
    QTextStream err(stderr);
    err << "Usage: stdiolink_service <script.js>\n"
        << "Options:\n"
        << "  --help     Show this help\n"
        << "  --version  Show version\n";
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        qCritical() << "Usage: stdiolink_service <script.js>";
        return 2;
    }

    QString arg1 = QString::fromLocal8Bit(argv[1]);
    if (arg1 == "--help" || arg1 == "-h") {
        printHelp();
        return 0;
    }
    if (arg1 == "--version" || arg1 == "-v") {
        QTextStream(stderr) << "stdiolink_service 0.1.0\n";
        return 0;
    }

    if (!QFile::exists(arg1)) {
        qCritical() << "File not found:" << arg1;
        return 2;
    }

    JsEngine engine;
    ConsoleBridge::install(engine.context());

    int ret = engine.evalFile(arg1);

    // 驱动 Promise microtask 队列直到全部完成
    while (engine.executePendingJobs()) {}

    return ret;
}
```

## 4. 文件清单

| 文件 | 说明 |
|------|------|
| `src/stdiolink_service/CMakeLists.txt` | 构建配置 |
| `src/stdiolink_service/main.cpp` | 程序入口 |
| `src/stdiolink_service/engine/js_engine.h` | 引擎封装头文件 |
| `src/stdiolink_service/engine/js_engine.cpp` | 引擎封装实现 |
| `src/stdiolink_service/engine/console_bridge.h` | console 桥接头文件 |
| `src/stdiolink_service/engine/console_bridge.cpp` | console 桥接实现 |

## 5. 验收标准

1. `stdiolink_service --help` 输出帮助信息到 stderr，退出码 0
2. `stdiolink_service --version` 输出版本信息到 stderr，退出码 0
3. `stdiolink_service nonexistent.js` 输出错误信息，退出码 2
4. `stdiolink_service test_basic.js` 能执行简单脚本，退出码 0
5. `console.log("hello")` 输出到 stderr（通过 qDebug）
6. `console.warn("warning")` 输出到 stderr（通过 qWarning）
7. `console.error("error")` 输出到 stderr（通过 qCritical）
8. `console.log("a=", 1, {x:2})` 多参数正确拼接
9. JS 语法错误时输出错误信息到 stderr，退出码 1
10. Promise 链能正确执行完成

## 6. 单元测试用例

### 6.1 JsEngine 基础测试

```cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QTextStream>
#include "engine/js_engine.h"
#include "engine/console_bridge.h"

class JsEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
    }

    void TearDown() override {
        engine.reset();
    }

    // 创建临时 JS 文件并返回路径
    QString createTempScript(const QString& code) {
        auto* f = new QTemporaryFile("XXXXXX.js");
        f->setAutoRemove(true);
        f->open();
        QTextStream out(f);
        out << code;
        out.flush();
        tempFiles.append(f);
        return f->fileName();
    }

    std::unique_ptr<JsEngine> engine;
    QList<QTemporaryFile*> tempFiles;
};

TEST_F(JsEngineTest, ContextNotNull) {
    EXPECT_NE(engine->context(), nullptr);
    EXPECT_NE(engine->runtime(), nullptr);
}

TEST_F(JsEngineTest, EvalSimpleScript) {
    QString path = createTempScript("var x = 1 + 2;");
    int ret = engine->evalFile(path);
    EXPECT_EQ(ret, 0);
}

TEST_F(JsEngineTest, EvalFileNotFound) {
    int ret = engine->evalFile("__nonexistent_file__.js");
    EXPECT_EQ(ret, 2);
}

TEST_F(JsEngineTest, EvalSyntaxError) {
    QString path = createTempScript("var x = {{{;");
    int ret = engine->evalFile(path);
    EXPECT_EQ(ret, 1);
}

TEST_F(JsEngineTest, EvalRuntimeError) {
    QString path = createTempScript("throw new Error('test error');");
    int ret = engine->evalFile(path);
    EXPECT_EQ(ret, 1);
}

TEST_F(JsEngineTest, PromiseJobPump) {
    // Promise 链应能正确执行
    QString path = createTempScript(
        "var result = 0;\n"
        "Promise.resolve(42).then(v => { result = v; });\n"
    );
    int ret = engine->evalFile(path);
    EXPECT_EQ(ret, 0);

    // 驱动 microtask 队列
    while (engine->executePendingJobs()) {}

    // 验证 Promise 已执行（通过全局变量检查）
    JSValue global = JS_GetGlobalObject(engine->context());
    JSValue val = JS_GetPropertyStr(engine->context(), global, "result");
    int32_t result = 0;
    JS_ToInt32(engine->context(), &result, val);
    EXPECT_EQ(result, 42);
    JS_FreeValue(engine->context(), val);
    JS_FreeValue(engine->context(), global);
}

TEST_F(JsEngineTest, AsyncAwait) {
    QString path = createTempScript(
        "var result = 0;\n"
        "async function main() {\n"
        "    result = await Promise.resolve(100);\n"
        "}\n"
        "main();\n"
    );
    int ret = engine->evalFile(path);
    EXPECT_EQ(ret, 0);

    while (engine->executePendingJobs()) {}

    JSValue global = JS_GetGlobalObject(engine->context());
    JSValue val = JS_GetPropertyStr(engine->context(), global, "result");
    int32_t result = 0;
    JS_ToInt32(engine->context(), &result, val);
    EXPECT_EQ(result, 100);
    JS_FreeValue(engine->context(), val);
    JS_FreeValue(engine->context(), global);
}

TEST_F(JsEngineTest, PromiseChain) {
    QString path = createTempScript(
        "var result = 0;\n"
        "Promise.resolve(1)\n"
        "  .then(v => v + 1)\n"
        "  .then(v => v * 10)\n"
        "  .then(v => { result = v; });\n"
    );
    int ret = engine->evalFile(path);
    EXPECT_EQ(ret, 0);

    while (engine->executePendingJobs()) {}

    JSValue global = JS_GetGlobalObject(engine->context());
    JSValue val = JS_GetPropertyStr(engine->context(), global, "result");
    int32_t result = 0;
    JS_ToInt32(engine->context(), &result, val);
    EXPECT_EQ(result, 20);
    JS_FreeValue(engine->context(), val);
    JS_FreeValue(engine->context(), global);
}
```

### 6.2 console 桥接测试

```cpp
#include <gtest/gtest.h>
#include "engine/js_engine.h"
#include "engine/console_bridge.h"

class ConsoleBridgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        ConsoleBridge::install(engine->context());
    }

    void TearDown() override {
        engine.reset();
    }

    int evalCode(const QString& code) {
        // 使用 JS_EVAL_TYPE_GLOBAL 直接执行代码片段
        QByteArray utf8 = code.toUtf8();
        JSValue val = JS_Eval(engine->context(), utf8.constData(),
                              utf8.size(), "<test>", JS_EVAL_TYPE_GLOBAL);
        bool isExc = JS_IsException(val);
        JS_FreeValue(engine->context(), val);
        return isExc ? 1 : 0;
    }

    std::unique_ptr<JsEngine> engine;
};

TEST_F(ConsoleBridgeTest, ConsoleLogExists) {
    // console 对象应存在
    int ret = evalCode("typeof console === 'object'");
    EXPECT_EQ(ret, 0);
}

TEST_F(ConsoleBridgeTest, ConsoleLogCallable) {
    int ret = evalCode("console.log('test message');");
    EXPECT_EQ(ret, 0);
}

TEST_F(ConsoleBridgeTest, ConsoleInfoCallable) {
    int ret = evalCode("console.info('info message');");
    EXPECT_EQ(ret, 0);
}

TEST_F(ConsoleBridgeTest, ConsoleWarnCallable) {
    int ret = evalCode("console.warn('warn message');");
    EXPECT_EQ(ret, 0);
}

TEST_F(ConsoleBridgeTest, ConsoleErrorCallable) {
    int ret = evalCode("console.error('error message');");
    EXPECT_EQ(ret, 0);
}

TEST_F(ConsoleBridgeTest, ConsoleLogMultiArgs) {
    // 多参数不应抛异常
    int ret = evalCode("console.log('a=', 1, 'b=', true);");
    EXPECT_EQ(ret, 0);
}

TEST_F(ConsoleBridgeTest, ConsoleLogObject) {
    // 对象参数应 JSON 序列化
    int ret = evalCode("console.log({x: 1, y: 'hello'});");
    EXPECT_EQ(ret, 0);
}

TEST_F(ConsoleBridgeTest, ConsoleLogArray) {
    int ret = evalCode("console.log([1, 2, 3]);");
    EXPECT_EQ(ret, 0);
}

TEST_F(ConsoleBridgeTest, ConsoleLogNoArgs) {
    // 无参数调用不应崩溃
    int ret = evalCode("console.log();");
    EXPECT_EQ(ret, 0);
}

TEST_F(ConsoleBridgeTest, ConsoleLogNull) {
    int ret = evalCode("console.log(null, undefined);");
    EXPECT_EQ(ret, 0);
}
```

### 6.3 main 入口测试

```cpp
#include <gtest/gtest.h>
#include <QProcess>
#include <QCoreApplication>

class MainEntryTest : public ::testing::Test {
protected:
    QString servicePath() {
        // 根据构建目录定位 stdiolink_service 可执行文件
        QString path = QCoreApplication::applicationDirPath()
                       + "/stdiolink_service";
#ifdef Q_OS_WIN
        path += ".exe";
#endif
        return path;
    }

    struct RunResult {
        int exitCode;
        QString stdoutStr;
        QString stderrStr;
    };

    RunResult runService(const QStringList& args) {
        QProcess proc;
        proc.start(servicePath(), args);
        proc.waitForFinished(10000);
        return {
            proc.exitCode(),
            QString::fromUtf8(proc.readAllStandardOutput()),
            QString::fromUtf8(proc.readAllStandardError())
        };
    }
};

TEST_F(MainEntryTest, NoArgs) {
    auto r = runService({});
    EXPECT_EQ(r.exitCode, 2);
}

TEST_F(MainEntryTest, HelpFlag) {
    auto r = runService({"--help"});
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrStr.contains("Usage"));
}

TEST_F(MainEntryTest, VersionFlag) {
    auto r = runService({"--version"});
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrStr.contains("stdiolink_service"));
}

TEST_F(MainEntryTest, FileNotFound) {
    auto r = runService({"__nonexistent__.js"});
    EXPECT_EQ(r.exitCode, 2);
}

TEST_F(MainEntryTest, BasicScript) {
    // 需要准备一个测试脚本文件
    QTemporaryFile tmpFile("XXXXXX.js");
    tmpFile.setAutoRemove(true);
    tmpFile.open();
    tmpFile.write("console.log('hello from js');");
    tmpFile.close();

    auto r = runService({tmpFile.fileName()});
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrStr.contains("hello from js"));
}

TEST_F(MainEntryTest, SyntaxErrorScript) {
    QTemporaryFile tmpFile("XXXXXX.js");
    tmpFile.setAutoRemove(true);
    tmpFile.open();
    tmpFile.write("var x = {{{;");
    tmpFile.close();

    auto r = runService({tmpFile.fileName()});
    EXPECT_EQ(r.exitCode, 1);
}

TEST_F(MainEntryTest, StdoutClean) {
    // console.log 应输出到 stderr，stdout 应为空
    QTemporaryFile tmpFile("XXXXXX.js");
    tmpFile.setAutoRemove(true);
    tmpFile.open();
    tmpFile.write("console.log('test');");
    tmpFile.close();

    auto r = runService({tmpFile.fileName()});
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stdoutStr.isEmpty());
    EXPECT_FALSE(r.stderrStr.isEmpty());
}
```

## 7. 依赖关系

- **前置依赖**：
  - 无（本里程碑为 JS Runtime 系列的起点）
  - 需要 stdiolink 核心库已构建完成（里程碑 1-20）
- **后续依赖**：
  - 里程碑 22（ES Module 加载器）：基于 JsEngine 扩展模块加载
  - 里程碑 23（Driver/Task 绑定）：基于 JsEngine 注册绑定
