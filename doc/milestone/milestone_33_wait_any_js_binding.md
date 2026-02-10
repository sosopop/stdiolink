# 里程碑 33：waitAny JS 绑定与多路事件监听

## 1. 目标

将 C++ `waitAnyNext` 能力暴露给 JS 层，实现非阻塞的多 Task 多路事件监听。与现有 `__scheduleTask`（仅 resolve 终结消息、丢弃中间 event）不同，新的 `waitAny` API 保留所有中间消息，使 JS 脚本能够监听多个 Driver 的进度事件、实时数据流等场景。

### 1.1 解决的问题

当前 JS 层存在两个局限：

| 现有 API | 局限 |
|----------|------|
| `task.waitNext()` | 同步阻塞，只能等一个 Task，阻塞期间无法处理其他 Task |
| `proxy.command()` (Promise) | 异步非阻塞，但丢弃所有中间 event 消息 |

新增 `waitAny` 填补空白：**异步非阻塞 + 保留中间消息 + 多路监听**。

### 1.2 API 层级对照（新增后）

| API 层级 | 调用方式 | 中间消息 | 多路监听 | 适用场景 |
|----------|---------|---------|---------|---------|
| `task.waitNext()` | 同步阻塞 | 保留 | 不支持 | 简单单 Task 场景 |
| `proxy.command()` | 异步 Promise | 丢弃 | 跨 Driver 支持 | 只关心最终结果 |
| **`waitAny(tasks)`** | **异步 Promise** | **保留** | **支持** | **进度监听、事件流、多路复用** |

## 2. 技术要点

### 2.1 调度模型：WaitAny 与现有 Scheduler 的关系

现有 `JsTaskScheduler` 的调度模型是 **"注册 Task → 等终结消息 → resolve Promise"**，每个 Task 只 settle 一次。`waitAny` 的调度模型不同：**"注册一组 Task → 任意一个有消息（含中间 event）→ 立即 resolve → 消费完毕"**，是一次性的。

两者共存于同一个事件循环，但职责分离：

```
JsTaskScheduler (现有)
  ├── __scheduleTask(task) → Promise    // 一对一，等终结
  └── poll() → waitAnyNext 驱动

WaitAnyScheduler (新增)
  ├── __waitAny(tasks) → Promise        // 多对一，等任意消息
  └── poll() → waitAnyNext 驱动
```

**不复用 `JsTaskScheduler`**，原因：
1. `JsTaskScheduler` 的 `poll()` 过滤了中间消息（`js_task_scheduler.cpp:89`），只在 `done`/`error` 时 settle
2. `JsTaskScheduler` 的 pending 模型是一个 Task 对应一个 Promise，而 `waitAny` 是一组 Task 对应一个 Promise
3. 两者的生命周期语义不同：`__scheduleTask` 的 Promise 跟随 Task 生命周期，`waitAny` 的 Promise 在收到第一条消息后立即 resolve

### 2.2 waitAny 的 Promise 语义

`waitAny(tasks, timeoutMs?)` 返回一个 Promise，其 resolve 值为：

```typescript
interface WaitAnyResult {
    taskIndex: number;   // 产生消息的 Task 在输入数组中的索引
    msg: {
        status: string;  // "event" | "done" | "error"
        code: number;
        data: any;
    };
}
```

语义规则：

| 情况 | 行为 |
|------|------|
| 任意 Task 有消息（含 event） | resolve `{ taskIndex, msg }` |
| 所有 Task 已完成且无排队消息 | resolve `null` |
| 超时（指定 timeoutMs） | resolve `null` |
| tasks 数组为空 | resolve `null` |

**与 `Promise.race` 的区别**：`Promise.race` 需要每个 Task 各自包装为独立 Promise，而 `waitAny` 在 C++ 层通过 `waitAnyNext` 统一监听所有 Task 的 I/O 事件，只需一次系统调用。

### 2.3 与主循环的协作

`WaitAnyScheduler` 的 `poll()` 需要与现有 `JsTaskScheduler` 和 `JsEngine` 的 pending jobs 协同工作。主循环扩展为：

```
while (scheduler.hasPending()
       || waitAnyScheduler.hasPending()
       || engine.hasPendingJobs()) {
    scheduler.poll(50);
    waitAnyScheduler.poll(50);
    engine.executePendingJobs();
}
```

两个 scheduler 的 `poll()` 内部各自调用 `waitAnyNext`，互不干扰。由于 `waitAnyNext` 使用 `QEventLoop` + 信号驱动，短超时（50ms）不会造成性能问题。

### 2.4 Task 所有权与生命周期

`waitAny` 不接管 Task 的所有权。JS 侧传入的 Task 数组中的 Task 对象仍由调用方持有，`waitAny` 仅在 C++ 侧拷贝 `stdiolink::Task`（共享 `TaskState` 的 `shared_ptr`）。

生命周期约束：
- `waitAny` resolve 后，Task 对象仍然有效，可以继续调用 `waitAny` 或 `waitNext`
- 如果 Task 对应的 Driver 被 `terminate()`，`waitAny` 会在下一次 poll 时检测到 `isDone()` 并正常 resolve
- JS 侧 GC 回收 Task 对象时，C++ 侧的 `shared_ptr<TaskState>` 引用计数减一，不影响其他持有者

## 3. 实现步骤

### 3.1 WaitAnyScheduler (C++ 侧)

```cpp
// bindings/js_wait_any_scheduler.h
#pragma once

#include <QVector>
#include <quickjs.h>
#include "stdiolink/host/task.h"

/// @brief waitAny 异步调度器
///
/// 管理 JS 端通过 waitAny() 发起的多路监听请求。每次调用 waitAny()
/// 注册一组 Task 和对应的 resolve/reject 回调；poll() 调用 waitAnyNext
/// 检查是否有任意 Task 产生消息，有则立即 resolve 对应的 Promise。
/// 与 JsTaskScheduler 不同，本调度器不过滤中间 event 消息。
class WaitAnyScheduler {
public:
    /// @brief 构造函数
    /// @param ctx QuickJS 上下文
    explicit WaitAnyScheduler(JSContext* ctx);

    /// @brief 析构函数，释放所有未完成请求的 resolve/reject 引用
    ~WaitAnyScheduler();

    WaitAnyScheduler(const WaitAnyScheduler&) = delete;
    WaitAnyScheduler& operator=(const WaitAnyScheduler&) = delete;

    /// @brief 添加一组待监听的 Task
    /// @param tasks Task 数组（拷贝，共享 TaskState）
    /// @param resolve Promise 的 resolve 回调（所有权转移给调度器）
    /// @param reject Promise 的 reject 回调（所有权转移给调度器）
    void addGroup(const QVector<stdiolink::Task>& tasks,
                  JSValue resolve, JSValue reject);

    /// @brief 轮询所有 pending 组，对有消息的组调用 resolve
    /// @param timeoutMs 单次轮询的超时时间（毫秒），默认 50ms
    /// @return 如果仍有未完成的组返回 true
    bool poll(int timeoutMs = 50);

    /// @brief 检查是否有未完成的监听组
    /// @return 有 pending 组返回 true
    bool hasPending() const;

    /// @brief 将 __waitAny 全局函数注册到 JS context
    /// @param ctx QuickJS 上下文
    /// @param scheduler 调度器实例指针
    static void installGlobal(JSContext* ctx, WaitAnyScheduler* scheduler);

private:
    /// @brief 待处理的监听组，关联一组 Task 与 Promise 回调
    struct PendingGroup {
        QVector<stdiolink::Task> tasks;    ///< 监听的 Task 组
        int timeoutMs = -1;                ///< 超时时间，-1 表示无超时
        JSValue resolve = JS_UNDEFINED;    ///< Promise resolve 回调
        JSValue reject = JS_UNDEFINED;     ///< Promise reject 回调
    };

    /// @brief 完成指定索引的组，调用 resolve 或 reject
    void settleGroup(int index, JSValue value, bool useReject);

    JSContext* m_ctx = nullptr;
    QVector<PendingGroup> m_pending;
};
```

### 3.2 WaitAnyScheduler 实现

```cpp
// bindings/js_wait_any_scheduler.cpp
#include "js_wait_any_scheduler.h"

#include <QHash>

#include "js_task.h"
#include "utils/js_convert.h"
#include "stdiolink/host/wait_any.h"

namespace {

QHash<quintptr, WaitAnyScheduler*> s_schedulers;

JSValue messageToJs(JSContext* ctx, const stdiolink::Message& msg) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "status",
                      JS_NewString(ctx, msg.status.toUtf8().constData()));
    JS_SetPropertyStr(ctx, obj, "code", JS_NewInt32(ctx, msg.code));
    JS_SetPropertyStr(ctx, obj, "data", qjsonToJsValue(ctx, msg.payload));
    return obj;
}

JSValue jsWaitAny(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    WaitAnyScheduler* scheduler =
        s_schedulers.value(reinterpret_cast<quintptr>(ctx), nullptr);
    if (!scheduler) {
        return JS_ThrowInternalError(ctx, "__waitAny is not installed");
    }

    // 参数 1：Task 数组
    if (argc < 1 || !JS_IsArray(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "__waitAny(tasks, timeoutMs?): tasks must be an array");
    }

    // 提取 Task 数组
    JSValue lenVal = JS_GetPropertyStr(ctx, argv[0], "length");
    uint32_t len = 0;
    JS_ToUint32(ctx, &len, lenVal);
    JS_FreeValue(ctx, lenVal);

    QVector<stdiolink::Task> tasks;
    tasks.reserve(static_cast<int>(len));
    for (uint32_t i = 0; i < len; ++i) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
        stdiolink::Task task;
        if (!JsTaskBinding::toTask(ctx, item, task)) {
            JS_FreeValue(ctx, item);
            return JS_ThrowTypeError(ctx,
                "__waitAny: array element %u is not a Task", i);
        }
        tasks.push_back(task);
        JS_FreeValue(ctx, item);
    }

    // 空数组快速路径
    if (tasks.isEmpty()) {
        JSValue resolvingFuncs[2] = {JS_UNDEFINED, JS_UNDEFINED};
        JSValue promise = JS_NewPromiseCapability(ctx, resolvingFuncs);
        if (JS_IsException(promise)) {
            return promise;
        }
        JSValue args[1] = {JS_NULL};
        JSValue callRet = JS_Call(ctx, resolvingFuncs[0],
                                  JS_UNDEFINED, 1, args);
        JS_FreeValue(ctx, callRet);
        JS_FreeValue(ctx, resolvingFuncs[0]);
        JS_FreeValue(ctx, resolvingFuncs[1]);
        return promise;
    }

    // 创建 Promise
    JSValue resolvingFuncs[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolvingFuncs);
    if (JS_IsException(promise)) {
        return promise;
    }

    scheduler->addGroup(tasks, resolvingFuncs[0], resolvingFuncs[1]);
    return promise;
}

} // namespace
```

**成员函数实现**：

```cpp
WaitAnyScheduler::WaitAnyScheduler(JSContext* ctx) : m_ctx(ctx) {}

WaitAnyScheduler::~WaitAnyScheduler() {
    s_schedulers.remove(reinterpret_cast<quintptr>(m_ctx));
    if (!m_ctx) {
        return;
    }
    for (const PendingGroup& group : m_pending) {
        JS_FreeValue(m_ctx, group.resolve);
        JS_FreeValue(m_ctx, group.reject);
    }
    m_pending.clear();
}

void WaitAnyScheduler::addGroup(const QVector<stdiolink::Task>& tasks,
                                JSValue resolve, JSValue reject) {
    PendingGroup group;
    group.tasks = tasks;
    group.resolve = resolve;
    group.reject = reject;
    m_pending.push_back(group);
}
```

**`poll()` 核心逻辑**：

```cpp
bool WaitAnyScheduler::poll(int timeoutMs) {
    if (!m_ctx || m_pending.isEmpty()) {
        return false;
    }

    // 逐组检查，倒序遍历以安全删除
    for (int g = m_pending.size() - 1; g >= 0; --g) {
        PendingGroup& group = m_pending[g];

        stdiolink::AnyItem anyItem;
        const bool gotMessage =
            stdiolink::waitAnyNext(group.tasks, anyItem, timeoutMs);

        if (gotMessage && anyItem.taskIndex >= 0
            && anyItem.taskIndex < group.tasks.size()) {
            // 构造 resolve 值：{ taskIndex, msg }
            JSValue result = JS_NewObject(m_ctx);
            JS_SetPropertyStr(m_ctx, result, "taskIndex",
                              JS_NewInt32(m_ctx, anyItem.taskIndex));
            JS_SetPropertyStr(m_ctx, result, "msg",
                              messageToJs(m_ctx, anyItem.msg));
            settleGroup(g, result, false);
            continue;
        }

        // 无消息：检查是否所有 Task 已完成
        bool allDone = true;
        for (const auto& t : group.tasks) {
            if (t.isValid() && !t.isDone()) {
                allDone = false;
                break;
            }
        }
        if (allDone) {
            settleGroup(g, JS_NULL, false);
        }
    }

    return !m_pending.isEmpty();
}
```

**辅助方法**：

```cpp
bool WaitAnyScheduler::hasPending() const {
    return !m_pending.isEmpty();
}

void WaitAnyScheduler::installGlobal(JSContext* ctx,
                                     WaitAnyScheduler* scheduler) {
    if (!ctx || !scheduler) {
        return;
    }
    s_schedulers.insert(reinterpret_cast<quintptr>(ctx), scheduler);
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "__waitAny",
                      JS_NewCFunction(ctx, jsWaitAny, "__waitAny", 2));
    JS_FreeValue(ctx, global);
}

void WaitAnyScheduler::settleGroup(int index, JSValue value,
                                   bool useReject) {
    PendingGroup group = m_pending[index];
    m_pending.removeAt(index);

    JSValue func = useReject ? group.reject : group.resolve;
    JSValue args[1] = {value};
    JSValue callRet = JS_Call(m_ctx, func, JS_UNDEFINED, 1, args);
    JS_FreeValue(m_ctx, callRet);
    JS_FreeValue(m_ctx, value);

    JS_FreeValue(m_ctx, group.resolve);
    JS_FreeValue(m_ctx, group.reject);
}
```

### 3.3 JS 层 waitAny 封装

`createWaitAnyFunction` 的实现方式与 `createOpenDriverFunction`（`proxy/driver_proxy.cpp`）一致，将 JS 代码内嵌为 C 字符串常量：

```cpp
// proxy/wait_any_wrapper.h
#pragma once
#include <quickjs.h>

/// @brief 创建 waitAny 包装函数
/// @param ctx QuickJS 上下文
/// @return waitAny 函数的 JSValue
JSValue createWaitAnyFunction(JSContext* ctx);
```

```cpp
// proxy/wait_any_wrapper.cpp
#include "wait_any_wrapper.h"
#include <cstring>

JSValue createWaitAnyFunction(JSContext* ctx) {
    static const char kSource[] =
        "(function(){\n"
        "  return async function waitAny(tasks, timeoutMs) {\n"
        "    if (!Array.isArray(tasks) || tasks.length === 0) {\n"
        "      return null;\n"
        "    }\n"
        "    const ms = (typeof timeoutMs === 'number') ? timeoutMs : -1;\n"
        "    return globalThis.__waitAny(tasks, ms);\n"
        "  };\n"
        "})";

    JSValue factory = JS_Eval(ctx, kSource, std::strlen(kSource),
                              "<stdiolink/wait_any_wrapper>",
                              JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(factory)) {
        return factory;
    }

    JSValue waitAnyFn = JS_Call(ctx, factory, JS_UNDEFINED, 0, nullptr);
    JS_FreeValue(ctx, factory);
    return waitAnyFn;
}
```

### 3.4 stdiolink 模块注册

在 `js_stdiolink_module.cpp` 中新增 `waitAny` 导出：

```cpp
// bindings/js_stdiolink_module.cpp — 新增部分

#include "proxy/wait_any_wrapper.h"

// 在 jsStdiolinkModuleInit 中添加：
JSValue waitAnyFn = createWaitAnyFunction(ctx);
if (JS_IsException(waitAnyFn)) {
    // ... 释放已创建的值
    return -1;
}
if (JS_SetModuleExport(ctx, module, "waitAny", waitAnyFn) < 0) {
    // ... 释放已创建的值
    return -1;
}

// 在 jsInitStdiolinkModule 中添加：
if (JS_AddModuleExport(ctx, module, "waitAny") < 0) {
    return nullptr;
}
```

### 3.5 主循环集成

在 `stdiolink_service` 的 `main.cpp` 中，创建 `WaitAnyScheduler` 实例并集成到主循环：

```cpp
// main.cpp — 修改部分

#include "bindings/js_wait_any_scheduler.h"

// ... 现有初始化代码 ...

// 创建 WaitAnyScheduler 并注册全局函数
WaitAnyScheduler waitAnyScheduler(engine.context());
WaitAnyScheduler::installGlobal(engine.context(), &waitAnyScheduler);

// 执行脚本
engine.evalFile(scriptPath);

// 扩展后的主循环（新增 waitAnyScheduler 条件和 poll 调用）
while (scheduler.hasPending()
       || waitAnyScheduler.hasPending()
       || engine.hasPendingJobs()) {
    scheduler.poll(50);
    waitAnyScheduler.poll(50);    // 新增
    engine.executePendingJobs();
}
```

### 3.6 典型使用场景

**场景 1：多 Driver 进度监听**

```javascript
import { Driver, waitAny } from "stdiolink";

const drvA = new Driver();
drvA.start("camera_driver");
const drvB = new Driver();
drvB.start("sensor_driver");

const taskA = drvA.request("scan", { fps: 30 });
const taskB = drvB.request("read", { interval: 100 });

while (!taskA.done || !taskB.done) {
    const result = await waitAny([taskA, taskB]);
    if (!result) break;

    const { taskIndex, msg } = result;
    if (msg.status === "event") {
        console.log(`Task ${taskIndex} progress:`, msg.data);
    } else if (msg.status === "done") {
        console.log(`Task ${taskIndex} completed:`, msg.data);
    } else if (msg.status === "error") {
        console.error(`Task ${taskIndex} failed:`, msg.data);
    }
}

drvA.terminate();
drvB.terminate();
```

**场景 2：单 Driver 带超时的事件流消费**

```javascript
import { Driver, waitAny } from "stdiolink";

const drv = new Driver();
drv.start("device_simulator_driver", ["--profile=keepalive"]);
const task = drv.request("stream", { duration: 10 });

while (!task.done) {
    const result = await waitAny([task], 3000);
    if (!result) {
        console.log("No data in 3s, checking again...");
        continue;
    }
    console.log(result.msg.status, result.msg.data);
}

drv.terminate();
```

## 4. 文件清单

| 文件 | 说明 |
|------|------|
| `src/stdiolink_service/bindings/js_wait_any_scheduler.h` | WaitAnyScheduler 头文件 |
| `src/stdiolink_service/bindings/js_wait_any_scheduler.cpp` | WaitAnyScheduler 实现 |
| `src/stdiolink_service/proxy/wait_any_wrapper.h` | waitAny JS 包装函数头文件 |
| `src/stdiolink_service/proxy/wait_any_wrapper.cpp` | waitAny JS 包装函数实现 |
| `src/stdiolink_service/bindings/js_stdiolink_module.cpp` | 更新：新增 waitAny 导出 |
| `src/stdiolink_service/main.cpp` | 更新：主循环集成 WaitAnyScheduler |

## 5. 验收标准

1. `import { waitAny } from "stdiolink"` 能正确导入
2. `waitAny([])` 传入空数组立即 resolve `null`
3. `waitAny([task])` 单 Task 有中间 event 消息时 resolve `{ taskIndex: 0, msg }` 且 `msg.status === "event"`
4. `waitAny([task])` 单 Task 完成时 resolve `{ taskIndex: 0, msg }` 且 `msg.status === "done"`
5. `waitAny([task])` 单 Task 出错时 resolve `{ taskIndex: 0, msg }` 且 `msg.status === "error"`
6. `waitAny([taskA, taskB])` 多 Task 时，返回最先有消息的 Task 的 `taskIndex`
7. 所有 Task 已完成且无排队消息时 resolve `null`
8. 指定 `timeoutMs` 超时后 resolve `null`
9. resolve 后 Task 对象仍然有效，可继续调用 `waitAny`
10. `WaitAnyScheduler` 与现有 `JsTaskScheduler` 在同一主循环中共存，互不干扰
11. `WaitAnyScheduler` 析构时正确释放所有 pending 的 resolve/reject 引用
12. 非 Task 对象传入数组时抛 `TypeError`

## 6. 单元测试用例

### 6.1 WaitAnyScheduler 单元测试

```cpp
#include <gtest/gtest.h>
#include "bindings/js_wait_any_scheduler.h"
#include "engine/js_engine.h"

class WaitAnySchedulerTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
    }
    void TearDown() override { engine.reset(); }

    std::unique_ptr<JsEngine> engine;
};

TEST_F(WaitAnySchedulerTest, InitiallyEmpty) {
    WaitAnyScheduler scheduler(engine->context());
    EXPECT_FALSE(scheduler.hasPending());
}

TEST_F(WaitAnySchedulerTest, PollEmptyReturnsFalse) {
    WaitAnyScheduler scheduler(engine->context());
    EXPECT_FALSE(scheduler.poll(10));
}
```

### 6.2 waitAny JS 集成测试

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
#include "bindings/js_wait_any_scheduler.h"

class JsWaitAnyTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        scheduler = std::make_unique<JsTaskScheduler>(engine->context());
        waitAnyScheduler =
            std::make_unique<WaitAnyScheduler>(engine->context());
        ModuleLoader::install(engine->context());
        ConsoleBridge::install(engine->context());
        ModuleLoader::addBuiltin("stdiolink", jsInitStdiolinkModule);
        JsTaskScheduler::installGlobal(engine->context(),
                                       scheduler.get());
        WaitAnyScheduler::installGlobal(engine->context(),
                                        waitAnyScheduler.get());

        driverPath = QCoreApplication::applicationDirPath()
                     + "/calculator_driver";
#ifdef Q_OS_WIN
        driverPath += ".exe";
#endif
    }
    void TearDown() override {
        waitAnyScheduler.reset();
        scheduler.reset();
        engine.reset();
    }

    int runScript(const QString& path) {
        int ret = engine->evalFile(path);
        while (scheduler->hasPending()
               || waitAnyScheduler->hasPending()
               || engine->hasPendingJobs()) {
            scheduler->poll(50);
            waitAnyScheduler->poll(50);
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
    std::unique_ptr<WaitAnyScheduler> waitAnyScheduler;
    QList<QTemporaryFile*> tempFiles;
    QString driverPath;
};
```

### 6.3 waitAny 导入测试

```cpp
TEST_F(JsWaitAnyTest, ImportWaitAny) {
    QString path = createScript(
        "import { waitAny } from 'stdiolink';\n"
        "globalThis.ok = (typeof waitAny === 'function') ? 1 : 0;\n"
    );
    int ret = runScript(path);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 6.4 空数组 waitAny 测试

```cpp
TEST_F(JsWaitAnyTest, EmptyArrayReturnsNull) {
    QString path = createScript(
        "import { waitAny } from 'stdiolink';\n"
        "const r = await waitAny([]);\n"
        "globalThis.ok = (r === null) ? 1 : 0;\n"
    );
    int ret = runScript(path);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 6.5 单 Task waitAny 集成测试（需要真实 Driver）

```cpp
TEST_F(JsWaitAnyTest, SingleTaskWaitAny) {
    QString path = createScript(QString(
        "import { Driver, waitAny } from 'stdiolink';\n"
        "const d = new Driver();\n"
        "d.start('%1');\n"
        "const task = d.request('add', { a: 10, b: 20 });\n"
        "const result = await waitAny([task]);\n"
        "globalThis.hasResult = result ? 1 : 0;\n"
        "globalThis.hasTaskIndex = "
        "  (result && result.taskIndex === 0) ? 1 : 0;\n"
        "globalThis.hasMsg = "
        "  (result && result.msg && result.msg.status) ? 1 : 0;\n"
        "d.terminate();\n"
    ).arg(driverPath));
    int ret = runScript(path);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("hasResult"), 1);
    EXPECT_EQ(getGlobalInt("hasTaskIndex"), 1);
    EXPECT_EQ(getGlobalInt("hasMsg"), 1);
}
```

### 6.6 多 Task waitAny 集成测试

```cpp
TEST_F(JsWaitAnyTest, MultiTaskWaitAny) {
    QString path = createScript(QString(
        "import { Driver, waitAny } from 'stdiolink';\n"
        "const d1 = new Driver();\n"
        "d1.start('%1');\n"
        "const d2 = new Driver();\n"
        "d2.start('%1');\n"
        "const t1 = d1.request('add', { a: 1, b: 2 });\n"
        "const t2 = d2.request('add', { a: 3, b: 4 });\n"
        "const r = await waitAny([t1, t2]);\n"
        "globalThis.hasResult = r ? 1 : 0;\n"
        "globalThis.validIndex = "
        "  (r && (r.taskIndex === 0 || r.taskIndex === 1)) ? 1 : 0;\n"
        "d1.terminate();\n"
        "d2.terminate();\n"
    ).arg(driverPath));
    int ret = runScript(path);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("hasResult"), 1);
    EXPECT_EQ(getGlobalInt("validIndex"), 1);
}
```

### 6.7 全部完成后 waitAny 返回 null

```cpp
TEST_F(JsWaitAnyTest, AllDoneReturnsNull) {
    QString path = createScript(QString(
        "import { Driver, waitAny } from 'stdiolink';\n"
        "const d = new Driver();\n"
        "d.start('%1');\n"
        "const task = d.request('add', { a: 1, b: 2 });\n"
        "// 先消费完所有消息\n"
        "while (true) {\n"
        "  const r = await waitAny([task]);\n"
        "  if (!r) break;\n"
        "}\n"
        "// 再次调用应立即返回 null\n"
        "const final = await waitAny([task]);\n"
        "globalThis.isNull = (final === null) ? 1 : 0;\n"
        "d.terminate();\n"
    ).arg(driverPath));
    int ret = runScript(path);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("isNull"), 1);
}
```

## 7. 依赖关系

### 7.1 前置依赖

| 依赖项 | 说明 |
|--------|------|
| 里程碑 23（Driver/Task JS 绑定） | `JsTaskBinding::toTask()` 用于从 JS 对象提取 C++ Task |
| 里程碑 25（Proxy 与 Scheduler） | `JsTaskScheduler` 的设计模式、`__scheduleTask` 全局函数注册方式、主循环结构 |
| stdiolink 核心库 `waitAnyNext` | `src/stdiolink/host/wait_any.h` 提供的多 Task 多路监听 C++ 实现 |

### 7.2 后置影响

本里程碑为独立的 JS API 扩展，不修改现有 API 的行为，无后置依赖。后续里程碑可基于 `waitAny` 构建更高层的抽象（如事件流迭代器、响应式管道等）。
