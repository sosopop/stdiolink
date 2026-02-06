# 里程碑 24：进程调用绑定

## 1. 目标

提供在 JS 中执行外部进程的能力，基于 `QProcess` 实现 `exec()` 函数绑定，注册到 `"stdiolink"` 内置模块导出。

## 2. 技术要点

### 2.1 exec() 函数

- 基于 `QProcess::start()` + `QProcess::waitForFinished()` 同步执行
- 返回 `{ exitCode, stdout, stderr }` 对象
- stdout/stderr 以 UTF-8 解码

### 2.2 options 支持

- `cwd`：设置工作目录
- `env`：额外环境变量（与当前环境合并，不替换）
- `timeout`：超时控制（默认 30s），超时后 kill 进程
- `input`：写入 stdin 的数据

### 2.3 错误处理

- 进程启动失败：抛 `Error`
- 进程超时：kill 进程并抛 `Error("Process timed out")`
- 正常退出（含非零退出码）：不抛异常，通过 `exitCode` 返回

## 3. 实现步骤

### 3.1 js_process 绑定

```cpp
// bindings/js_process.h
#pragma once

#include "quickjs.h"

class JsProcessBinding {
public:
    // 注册 exec 函数到 context
    static void registerFunctions(JSContext* ctx);

    // 获取 exec 函数值（供模块导出）
    static JSValue getExecFunction(JSContext* ctx);
};
```

### 3.2 exec() 实现核心逻辑

```cpp
// bindings/js_process.cpp (核心部分)

static JSValue jsExec(JSContext* ctx, JSValue thisVal,
                      int argc, JSValue* argv)
{
    // 参数1: program (string, 必填)
    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "exec: program must be a string");

    const char* program = JS_ToCString(ctx, argv[0]);
    QString prog = QString::fromUtf8(program);
    JS_FreeCString(ctx, program);

    // 参数2: args (string[], 可选)
    QStringList args;
    if (argc >= 2 && JS_IsArray(ctx, argv[1])) {
        JSValue lenVal = JS_GetPropertyStr(ctx, argv[1], "length");
        int32_t len = 0;
        JS_ToInt32(ctx, &len, lenVal);
        JS_FreeValue(ctx, lenVal);
        for (int i = 0; i < len; i++) {
            JSValue elem = JS_GetPropertyUint32(ctx, argv[1], i);
            const char* s = JS_ToCString(ctx, elem);
            if (s) { args << QString::fromUtf8(s); JS_FreeCString(ctx, s); }
            JS_FreeValue(ctx, elem);
        }
    }

    // 参数3: options (object, 可选)
    QString cwd;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    int timeout = 30000;
    QByteArray inputData;

    if (argc >= 3 && JS_IsObject(argv[2])) {
        // 解析 cwd
        JSValue cwdVal = JS_GetPropertyStr(ctx, argv[2], "cwd");
        if (JS_IsString(cwdVal)) {
            const char* s = JS_ToCString(ctx, cwdVal);
            cwd = QString::fromUtf8(s);
            JS_FreeCString(ctx, s);
        }
        JS_FreeValue(ctx, cwdVal);

        // 解析 timeout
        JSValue toVal = JS_GetPropertyStr(ctx, argv[2], "timeout");
        if (JS_IsNumber(toVal)) {
            JS_ToInt32(ctx, &timeout, toVal);
        }
        JS_FreeValue(ctx, toVal);

        // 解析 input
        JSValue inVal = JS_GetPropertyStr(ctx, argv[2], "input");
        if (JS_IsString(inVal)) {
            const char* s = JS_ToCString(ctx, inVal);
            inputData = QByteArray(s);
            JS_FreeCString(ctx, s);
        }
        JS_FreeValue(ctx, inVal);

        // 解析 env (合并到系统环境)
        JSValue envVal = JS_GetPropertyStr(ctx, argv[2], "env");
        if (JS_IsObject(envVal)) {
            // 遍历属性并合并
            // ...
        }
        JS_FreeValue(ctx, envVal);
    }

    // 执行进程
    QProcess proc;
    if (!cwd.isEmpty()) proc.setWorkingDirectory(cwd);
    proc.setProcessEnvironment(env);
    proc.start(prog, args);

    if (!proc.waitForStarted(5000)) {
        return JS_ThrowInternalError(ctx,
            "exec: failed to start process: %s",
            prog.toUtf8().constData());
    }

    if (!inputData.isEmpty()) {
        proc.write(inputData);
        proc.closeWriteChannel();
    }

    if (!proc.waitForFinished(timeout)) {
        proc.kill();
        proc.waitForFinished(1000);
        return JS_ThrowInternalError(ctx, "exec: process timed out");
    }

    // 构造返回对象
    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "exitCode",
                      JS_NewInt32(ctx, proc.exitCode()));
    JS_SetPropertyStr(ctx, result, "stdout",
        JS_NewString(ctx, proc.readAllStandardOutput()
                              .constData()));
    JS_SetPropertyStr(ctx, result, "stderr",
        JS_NewString(ctx, proc.readAllStandardError()
                              .constData()));
    return result;
}
```

### 3.3 注册到 stdiolink 模块

在 `js_stdiolink_module.cpp` 中新增 `exec` 导出：

```cpp
JS_AddModuleExport(ctx, m, "exec");
// 在 jsModuleInit 中：
JS_SetModuleExport(ctx, m, "exec",
                   JsProcessBinding::getExecFunction(ctx));
```

## 4. 文件清单

| 文件 | 说明 |
|------|------|
| `src/stdiolink_service/bindings/js_process.h` | 进程绑定头文件 |
| `src/stdiolink_service/bindings/js_process.cpp` | 进程绑定实现 |
| `src/stdiolink_service/bindings/js_stdiolink_module.cpp` | 更新：新增 exec 导出 |

## 5. 验收标准

1. `import { exec } from "stdiolink"` 能正确导入
2. `exec("echo", ["hello"])` 返回 `{ exitCode: 0, stdout: "hello\n", stderr: "" }`
3. 非零退出码正确返回，不抛异常
4. `cwd` 选项能正确设置工作目录
5. `env` 选项能合并环境变量
6. `timeout` 选项超时后 kill 进程并抛异常
7. `input` 选项能写入 stdin
8. 启动不存在的程序时抛异常
9. stdout/stderr 以 UTF-8 正确解码

## 6. 单元测试用例

### 6.1 exec 基础测试

```cpp
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextStream>
#include "engine/js_engine.h"
#include "engine/module_loader.h"
#include "engine/console_bridge.h"
#include "bindings/js_stdiolink_module.h"

class JsProcessBindingTest : public ::testing::Test {
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

    QString getGlobalString(const char* name) {
        JSValue global = JS_GetGlobalObject(engine->context());
        JSValue val = JS_GetPropertyStr(engine->context(), global, name);
        const char* s = JS_ToCString(engine->context(), val);
        QString result = s ? QString::fromUtf8(s) : QString();
        if (s) JS_FreeCString(engine->context(), s);
        JS_FreeValue(engine->context(), val);
        JS_FreeValue(engine->context(), global);
        return result;
    }

    std::unique_ptr<JsEngine> engine;
    QList<QTemporaryFile*> tempFiles;
};
```

### 6.2 exec 功能测试

```cpp
TEST_F(JsProcessBindingTest, ImportExec) {
    QString path = createTempScript(
        "import { exec } from 'stdiolink';\n"
        "globalThis.ok = (typeof exec === 'function') ? 1 : 0;\n"
    );
    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsProcessBindingTest, ExecEcho) {
    // Windows: cmd /c echo hello; Linux: echo hello
    QString path = createTempScript(
#ifdef Q_OS_WIN
        "import { exec } from 'stdiolink';\n"
        "const r = exec('cmd', ['/c', 'echo', 'hello']);\n"
#else
        "import { exec } from 'stdiolink';\n"
        "const r = exec('echo', ['hello']);\n"
#endif
        "globalThis.exitCode = r.exitCode;\n"
        "globalThis.hasStdout = r.stdout.length > 0 ? 1 : 0;\n"
    );
    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("exitCode"), 0);
    EXPECT_EQ(getGlobalInt("hasStdout"), 1);
}

TEST_F(JsProcessBindingTest, ExecNonZeroExit) {
    QString path = createTempScript(
#ifdef Q_OS_WIN
        "import { exec } from 'stdiolink';\n"
        "const r = exec('cmd', ['/c', 'exit', '42']);\n"
#else
        "import { exec } from 'stdiolink';\n"
        "const r = exec('bash', ['-c', 'exit 42']);\n"
#endif
        "globalThis.exitCode = r.exitCode;\n"
    );
    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("exitCode"), 42);
}
```

### 6.3 exec 错误与选项测试

```cpp
TEST_F(JsProcessBindingTest, ExecNonexistentProgram) {
    QString path = createTempScript(
        "import { exec } from 'stdiolink';\n"
        "try {\n"
        "    exec('__nonexistent_program__');\n"
        "    globalThis.caught = 0;\n"
        "} catch(e) {\n"
        "    globalThis.caught = 1;\n"
        "}\n"
    );
    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("caught"), 1);
}

TEST_F(JsProcessBindingTest, ExecWithCwd) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    QString path = createTempScript(QString(
#ifdef Q_OS_WIN
        "import { exec } from 'stdiolink';\n"
        "const r = exec('cmd', ['/c', 'cd'], { cwd: '%1' });\n"
#else
        "import { exec } from 'stdiolink';\n"
        "const r = exec('pwd', [], { cwd: '%1' });\n"
#endif
        "globalThis.exitCode = r.exitCode;\n"
        "globalThis.output = r.stdout;\n"
    ).arg(tmpDir.path()));

    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("exitCode"), 0);
}

TEST_F(JsProcessBindingTest, ExecNoArgs) {
    // 只传 program，不传 args
    QString path = createTempScript(
        "import { exec } from 'stdiolink';\n"
        "globalThis.ok = (typeof exec === 'function') ? 1 : 0;\n"
    );
    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsProcessBindingTest, ExecBadArgType) {
    // program 不是 string 应抛 TypeError
    QString path = createTempScript(
        "import { exec } from 'stdiolink';\n"
        "try {\n"
        "    exec(123);\n"
        "    globalThis.caught = 0;\n"
        "} catch(e) {\n"
        "    globalThis.caught = 1;\n"
        "}\n"
    );
    int ret = engine->evalFile(path);
    while (engine->executePendingJobs()) {}
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("caught"), 1);
}
```

## 7. 依赖关系

- **前置依赖**：
  - 里程碑 21（JS 引擎脚手架）：JsEngine 基础封装
  - 里程碑 22（ES Module 加载器）：内置模块注册机制
  - 里程碑 23（Driver/Task 绑定）：stdiolink 模块框架、js_convert 工具
- **后续依赖**：
  - 里程碑 27（集成测试）：exec 功能的端到端验证
