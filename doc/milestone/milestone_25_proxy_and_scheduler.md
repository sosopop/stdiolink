# 里程碑 25：Proxy 代理调用与并发调度

## 1. 目标

实现 `openDriver()` 工厂函数，通过 JS Proxy 将 Driver 命令映射为对象方法调用；实现 `JsTaskScheduler` 基于 `waitAnyNext` 的单线程多任务调度器，支持不同 Driver 实例的并行调用。

## 2. 技术要点

### 2.1 Proxy 代理机制

- `openDriver(program, args?)` 启动 Driver 进程并调用 `queryMeta()` 获取元数据
- 返回 `Proxy` 对象，拦截属性访问：命令名 → 返回**异步**调用函数
- 保留字段：`$driver`、`$meta`、`$rawRequest`、`$close`
- 单实例并发保护：busy flag + `DriverBusyError`
- **命令调用返回 Promise**，内部通过 `__scheduleTask()` 注册到 `JsTaskScheduler`

### 2.2 并发调度器 (JsTaskScheduler)

- 基于现有 `waitAnyNext` 实现单线程多任务调度
- 维护活动 Task 列表，关联到 JS Promise resolve/reject
- 调度循环：`poll()` 调用 `waitAnyNext`，完成的 Task 触发对应 Promise
- 与 Promise job pump 协作
- **Proxy 命令调用通过调度器异步完成**，而非同步阻塞

### 2.3 并发语义

| 场景 | 行为 |
|------|------|
| 不同实例并行调用 | 正常，由调度器统一调度，`Promise.all` 真正并发 |
| 同一实例并发调用 | 抛 `DriverBusyError`，Proxy 层拦截 |

### 2.4 同步 vs 异步 API 对照

| API 层级 | 调用方式 | 说明 |
|----------|---------|------|
| `task.waitNext()` | 同步阻塞 | 底层 API，阻塞 JS 线程 |
| `calc.add(params)` | 异步 Promise | Proxy 层，通过调度器非阻塞 |
| `$rawRequest(cmd, data)` | 返回 Task | 底层 API，用户自行决定同步/异步 |

## 3. 实现步骤

### 3.1 JsTaskScheduler (C++ 侧)

```cpp
// bindings/js_task_scheduler.h
#pragma once

#include <QVector>
#include "stdiolink/host/task.h"
#include "stdiolink/host/wait_any.h"
#include "quickjs.h"

class JsTaskScheduler {
public:
    explicit JsTaskScheduler(JSContext* ctx);
    ~JsTaskScheduler();

    // 注册一个活动 Task，关联到 JS Promise resolve/reject
    void addTask(stdiolink::Task task, JSValue resolve, JSValue reject);

    // 驱动一轮调度：调用 waitAnyNext，完成的 Task 触发对应 Promise
    // 返回是否还有活动 Task
    bool poll(int timeoutMs = 50);

    // 是否有待处理任务
    bool hasPending() const;

    // 注册 __scheduleTask 全局函数到 JS context（供 Proxy 层调用）
    static void installGlobal(JSContext* ctx, JsTaskScheduler* scheduler);

private:
    struct PendingTask {
        stdiolink::Task task;
        JSValue resolve;
        JSValue reject;
    };

    JSContext* m_ctx;
    QVector<PendingTask> m_pending;
};
```

`installGlobal` 向 JS 全局注册 `__scheduleTask(task)` 函数，该函数：
1. 创建一个 `new Promise((resolve, reject) => ...)`
2. 将 C++ Task 对象与 resolve/reject 一起注册到调度器
3. 返回该 Promise 给 JS 调用方

### 3.2 driver_proxy.js（内嵌 JS 代码）

```js
// proxy/driver_proxy.js — 编译期嵌入为 C 字符串常量
export async function openDriver(program, args = []) {
    const driver = new Driver();
    if (!driver.start(program, args)) {
        throw new Error(`Failed to start driver: ${program}`);
    }

    const meta = driver.queryMeta();
    if (!meta) {
        driver.terminate();
        throw new Error(`Failed to query metadata from: ${program}`);
    }

    const commands = new Set(meta.commands.map(c => c.name));
    let busy = false;

    return new Proxy(driver, {
        get(target, prop) {
            if (prop === "$driver") return target;
            if (prop === "$meta") return meta;
            if (prop === "$rawRequest")
                return (cmd, data) => target.request(cmd, data);
            if (prop === "$close") return () => target.terminate();

            if (typeof prop === "string" && commands.has(prop)) {
                return (params = {}) => {
                    if (busy)
                        throw new Error(
                            "DriverBusyError: request already in flight");
                    busy = true;
                    let task;
                    try {
                        task = target.request(prop, params);
                    } catch (e) {
                        busy = false;
                        throw e;
                    }
                    // 通过调度器异步等待，返回 Promise
                    return globalThis.__scheduleTask(task).then(
                        msg => {
                            busy = false;
                            if (!msg)
                                throw new Error(
                                    `No response for command: ${prop}`);
                            if (msg.status === "error") {
                                const err = new Error(
                                    msg.data?.message
                                    || `Command failed: ${prop}`);
                                err.code = msg.code;
                                err.data = msg.data;
                                throw err;
                            }
                            return msg.data;
                        },
                        err => {
                            busy = false;
                            throw err;
                        }
                    );
                };
            }
            return undefined;
        }
    });
}
```

> **关键变更**：命令调用不再同步调用 `task.waitNext()`，而是通过 `__scheduleTask(task)` 将 Task 注册到 C++ 调度器并返回 Promise。这使得 `Promise.all([drvA.add(...), drvB.add(...)])` 能真正并发——两个请求同时注册到调度器，由主循环 `poll()` 通过 `waitAnyNext` 统一驱动。

### 3.3 主循环协作

```
evalFile() 执行脚本
  → 脚本中 Promise.all([drvA.scan(), drvB.scan()])
  → 两个请求注册到 JsTaskScheduler
  → 主循环：
      while (scheduler.hasPending() || engine.hasPendingJobs()) {
          scheduler.poll();           // 驱动 I/O，完成的 Task resolve Promise
          engine.executePendingJobs(); // 驱动 Promise 链
      }
```

### 3.4 注册到 stdiolink 模块

在 `js_stdiolink_module.cpp` 中新增 `openDriver` 导出：

```cpp
JS_AddModuleExport(ctx, m, "openDriver");
```

## 4. 文件清单

| 文件 | 说明 |
|------|------|
| `src/stdiolink_service/bindings/js_task_scheduler.h` | 并发调度器头文件 |
| `src/stdiolink_service/bindings/js_task_scheduler.cpp` | 并发调度器实现 |
| `src/stdiolink_service/proxy/driver_proxy.js` | Proxy 代理 JS 实现 |
| `src/stdiolink_service/proxy/driver_proxy_gen.h` | 内嵌 JS 代码生成头文件 |
| `src/stdiolink_service/proxy/driver_proxy_gen.cpp` | openDriver C++ 辅助 |
| `src/stdiolink_service/bindings/js_stdiolink_module.cpp` | 更新：新增 openDriver 导出 |
| `src/stdiolink_service/main.cpp` | 更新：主循环集成调度器 |

## 5. 验收标准

1. `import { openDriver } from "stdiolink"` 能正确导入
2. `await openDriver(program)` 返回 Proxy 对象
3. `await calc.add({ a: 5, b: 3 })` 异步调用 Driver 命令并返回结果
4. `calc.$meta` 返回元数据对象
5. `calc.$driver` 返回底层 Driver 实例
6. `calc.$rawRequest(cmd, data)` 返回 Task 对象
7. `calc.$close()` 终止 Driver 进程
8. 同一实例并发调用抛 `DriverBusyError`
9. 不同实例可通过 `Promise.all` 并行调用
10. 访问不存在的命令名返回 `undefined`
11. Driver 返回 error 状态时 Proxy 自动抛异常
12. `openDriver` 启动失败时抛异常
13. `openDriver` 获取 meta 失败时抛异常并终止进程

## 6. 单元测试用例

### 6.1 JsTaskScheduler 单元测试

```cpp
#include <gtest/gtest.h>
#include "bindings/js_task_scheduler.h"
#include "engine/js_engine.h"

class JsTaskSchedulerTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
    }
    void TearDown() override { engine.reset(); }

    std::unique_ptr<JsEngine> engine;
};

TEST_F(JsTaskSchedulerTest, InitiallyEmpty) {
    JsTaskScheduler scheduler(engine->context());
    EXPECT_FALSE(scheduler.hasPending());
}

TEST_F(JsTaskSchedulerTest, PollEmptyReturnsFalse) {
    JsTaskScheduler scheduler(engine->context());
    EXPECT_FALSE(scheduler.poll(10));
}
```

### 6.2 openDriver Proxy 集成测试

```cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QTextStream>
#include "engine/js_engine.h"
#include "engine/module_loader.h"
#include "engine/console_bridge.h"
#include "bindings/js_stdiolink_module.h"
#include "bindings/js_task_scheduler.h"

class JsProxyTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        scheduler = std::make_unique<JsTaskScheduler>(engine->context());
        ModuleLoader::install(engine->context());
        ConsoleBridge::install(engine->context());
        ModuleLoader::addBuiltin("stdiolink",
                                 jsInitStdiolinkModule);
        // 注册 __scheduleTask 全局函数，供 Proxy 层调用
        JsTaskScheduler::installGlobal(engine->context(),
                                       scheduler.get());
        driverPath = QCoreApplication::applicationDirPath()
                     + "/calculator_driver";
#ifdef Q_OS_WIN
        driverPath += ".exe";
#endif
    }
    void TearDown() override {
        scheduler.reset();
        engine.reset();
    }

    // 执行脚本并驱动调度器 + Promise 队列直到全部完成
    int runScript(const QString& path) {
        int ret = engine->evalFile(path);
        while (scheduler->hasPending()
               || engine->hasPendingJobs()) {
            scheduler->poll(50);
            engine->executePendingJobs();
        }
        return ret;
    }

    QString createScript(const QString& code) {
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
        JSValue g = JS_GetGlobalObject(engine->context());
        JSValue v = JS_GetPropertyStr(engine->context(), g, name);
        int32_t r = 0;
        JS_ToInt32(engine->context(), &r, v);
        JS_FreeValue(engine->context(), v);
        JS_FreeValue(engine->context(), g);
        return r;
    }

    std::unique_ptr<JsEngine> engine;
    std::unique_ptr<JsTaskScheduler> scheduler;
    QList<QTemporaryFile*> tempFiles;
    QString driverPath;
};
```

### 6.3 Proxy 功能测试

```cpp
TEST_F(JsProxyTest, ImportOpenDriver) {
    QString path = createScript(
        "import { openDriver } from 'stdiolink';\n"
        "globalThis.ok = (typeof openDriver === 'function') ? 1 : 0;\n"
    );
    int ret = runScript(path);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsProxyTest, OpenDriverStartFail) {
    QString path = createScript(
        "import { openDriver } from 'stdiolink';\n"
        "try {\n"
        "    await openDriver('__nonexistent__');\n"
        "    globalThis.caught = 0;\n"
        "} catch(e) {\n"
        "    globalThis.caught = 1;\n"
        "}\n"
    );
    int ret = runScript(path);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("caught"), 1);
}
```

### 6.4 Proxy 命令调用与保留字段测试（需要真实 Driver）

```cpp
TEST_F(JsProxyTest, ProxyCommandCall) {
    QString path = createScript(QString(
        "import { openDriver } from 'stdiolink';\n"
        "const calc = await openDriver('%1');\n"
        "const r = await calc.add({ a: 5, b: 3 });\n"
        "globalThis.hasResult = (r !== undefined) ? 1 : 0;\n"
        "calc.$close();\n"
    ).arg(driverPath));
    int ret = runScript(path);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("hasResult"), 1);
}
```

### 6.5 保留字段测试

```cpp
TEST_F(JsProxyTest, ProxyMetaField) {
    QString path = createScript(QString(
        "import { openDriver } from 'stdiolink';\n"
        "const calc = await openDriver('%1');\n"
        "globalThis.hasMeta = calc.$meta ? 1 : 0;\n"
        "globalThis.hasCommands = "
        "  (calc.$meta && calc.$meta.commands) ? 1 : 0;\n"
        "calc.$close();\n"
    ).arg(driverPath));
    int ret = runScript(path);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("hasMeta"), 1);
    EXPECT_EQ(getGlobalInt("hasCommands"), 1);
}

TEST_F(JsProxyTest, ProxyDriverField) {
    QString path = createScript(QString(
        "import { openDriver } from 'stdiolink';\n"
        "const calc = await openDriver('%1');\n"
        "globalThis.ok = calc.$driver ? 1 : 0;\n"
        "calc.$close();\n"
    ).arg(driverPath));
    int ret = runScript(path);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsProxyTest, ProxyUndefinedCommand) {
    QString path = createScript(QString(
        "import { openDriver } from 'stdiolink';\n"
        "const calc = await openDriver('%1');\n"
        "globalThis.ok = "
        "  (calc.nonexistent_cmd === undefined) ? 1 : 0;\n"
        "calc.$close();\n"
    ).arg(driverPath));
    int ret = runScript(path);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsProxyTest, ProxyClose) {
    QString path = createScript(QString(
        "import { openDriver } from 'stdiolink';\n"
        "const calc = await openDriver('%1');\n"
        "calc.$close();\n"
        "globalThis.ok = 1;\n"
    ).arg(driverPath));
    int ret = runScript(path);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

## 7. 依赖关系

- **前置依赖**：
  - 里程碑 23（Driver/Task 绑定）：Driver/Task JS 类、js_convert 工具
  - 里程碑 22（ES Module 加载器）：内置模块注册
  - stdiolink 核心库 `waitAnyNext`（`src/stdiolink/host/wait_any.h`）
- **后续依赖**：
  - 里程碑 27（集成测试）：Proxy 调用的端到端验证
