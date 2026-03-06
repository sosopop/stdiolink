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
    if (scheduler.hasPending()) {
        scheduler.poll(50);
    }
    if (waitAnyScheduler.hasPending()) {
        waitAnyScheduler.poll(50);
    }
    while (engine.hasPendingJobs()) {
        engine.executePendingJobs();
    }
}
```

两个 scheduler 的 `poll()` 内部各自调用 `waitAnyNext`，互不干扰。由于 `waitAnyNext` 使用 `QEventLoop` + 信号驱动，短超时（50ms）不会造成性能问题。

### 2.4 Task 所有权与生命周期

`waitAny` 不接管 Task 的所有权。JS 侧传入的 Task 数组中的 Task 对象仍由调用方持有，`waitAny` 仅在 C++ 侧拷贝 `stdiolink::Task`（共享 `TaskState` 的 `shared_ptr`）。

生命周期约束：
- `waitAny` resolve 后，Task 对象仍然有效，可以继续调用 `waitAny` 或 `waitNext`
- 如果 Task 对应的 Driver 被 `terminate()`，当前 `Driver::terminate()` 不会将 `TaskState::terminal` 置为 `true`，因此 `Task::isDone()` 不可达。`waitAny` 的 `poll()` 需要额外检测进程退出状态：当 `Driver::isRunning()` 返回 `false` 且 Task 未收到终结消息时，`poll()` 通过 `Task::forceTerminal()` 将 `TaskState` 标记为终态，然后构造合成 error result 并 settle group。这确保 `TaskState::terminal` 被置位，后续 `waitAny` 消费完剩余排队消息后 `task.done`（即 `terminal && queue.empty()`）变为 `true`，避免 `while (!task.done)` 死循环（参见 3.2 中 `poll()` 的进程退出检测逻辑）。`Task::forceTerminal()` 是本里程碑要求核心库新增的方法（参见 7.1 前置依赖）
- JS 侧 GC 回收 Task 对象时，C++ 侧的 `shared_ptr<TaskState>` 引用计数减一，不影响其他持有者
- **同一个 Task 不应同时被 `proxy.command()`（`__scheduleTask`）和 `waitAny` 监听**。两者共享同一个 `TaskState` 消息队列，`tryNext()` 是消费性操作（pop），消息被其中一方消费后另一方无法获取。如果需要对同一个 Driver 同时使用两种 API，应分别发起独立的 `request()` 调用
- **同一个 Task 不应同时出现在多个 pending 的 `waitAny` 调用中**。`poll()` 合并所有 group 的 tasks 后统一调用 `waitAnyNext`，`tryNext()` 的消费性 pop 会导致某些 group 丢消息。`addGroup` 通过 `Task::stateId()` 做精确身份比较来检测冲突并 reject（参见 3.2 实现）

## 3. 实现步骤

### 3.0 核心库 Task 扩展（前置变更）

本里程碑需要在 `stdiolink::Task` 类上新增两个公开方法：

```cpp
// src/stdiolink/host/task.h — 新增部分

/// @brief 获取 TaskState 的身份标识（shared_ptr 原始指针）
/// 用于判断两个 Task 是否共享同一个底层状态
const TaskState* stateId() const { return m_st.get(); }

/// @brief 强制将 TaskState 标记为终态
/// 用于 Driver 进程异常退出但未发送终结消息的场景
/// @param code 错误码
/// @param error 错误描述
void forceTerminal(int code, const QString& error);
```

```cpp
// src/stdiolink/host/task.cpp — 新增部分

void Task::forceTerminal(int code, const QString& error) {
    if (!m_st || m_st->terminal)
        return;
    m_st->terminal = true;
    m_st->exitCode = code;
    m_st->errorText = error;
}
```

**设计说明**：

- `stateId()` 返回 `TaskState*` 原始指针，仅用于身份比较（`==`），不用于解引用。两个 Task 拷贝共享同一个 `shared_ptr<TaskState>`，`stateId()` 相同即为同一 Task。比用 `owner()` 判断更精确——`owner()` 相同只能说明来自同一个 Driver，不能区分该 Driver 的不同请求。
- `forceTerminal()` 直接操作 `TaskState`，由于 `shared_ptr` 共享语义，调用方和 JS 侧持有的 Task 对象同步生效，`isDone()` 在 queue 清空后返回 `true`。

### 3.1 WaitAnyScheduler (C++ 侧)

```cpp
// bindings/js_wait_any_scheduler.h
#pragma once

#include <QVector>
#include <QElapsedTimer>
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
    /// @param timeoutMs 用户指定的超时时间（毫秒），-1 表示无超时
    /// @param resolve Promise 的 resolve 回调（所有权转移给调度器）
    /// @param reject Promise 的 reject 回调（所有权转移给调度器）
    void addGroup(const QVector<stdiolink::Task>& tasks, int timeoutMs,
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
        int timeoutMs = -1;                ///< 用户指定的超时时间，-1 表示无超时
        QElapsedTimer elapsed;             ///< 注册时启动，用于判断是否超时
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

    // 参数 2：可选 timeoutMs
    int timeoutMs = -1;
    if (argc >= 2 && JS_IsNumber(argv[1])) {
        JS_ToInt32(ctx, &timeoutMs, argv[1]);
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

    scheduler->addGroup(tasks, timeoutMs, resolvingFuncs[0], resolvingFuncs[1]);
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
    // 对未完成的 Promise 主动 reject，避免 JS 侧 Promise 永远 pending
    for (const PendingGroup& group : m_pending) {
        JSValue err = JS_NewString(m_ctx, "WaitAnyScheduler destroyed");
        JSValue args[1] = {err};
        JSValue ret = JS_Call(m_ctx, group.reject, JS_UNDEFINED, 1, args);
        JS_FreeValue(m_ctx, ret);
        JS_FreeValue(m_ctx, err);
        JS_FreeValue(m_ctx, group.resolve);
        JS_FreeValue(m_ctx, group.reject);
    }
    m_pending.clear();
}

void WaitAnyScheduler::addGroup(const QVector<stdiolink::Task>& tasks,
                                int timeoutMs,
                                JSValue resolve, JSValue reject) {
    // 检测冲突 1：同一 tasks 数组内部不应有重复 Task
    for (int i = 0; i < tasks.size(); ++i) {
        if (!tasks[i].isValid()) continue;
        for (int j = i + 1; j < tasks.size(); ++j) {
            if (tasks[j].isValid()
                && tasks[j].stateId() == tasks[i].stateId()) {
                JSValue errMsg = JS_NewString(m_ctx,
                    "waitAny: duplicate Task in tasks array");
                JSValue args[1] = {errMsg};
                JSValue ret = JS_Call(m_ctx, reject,
                                      JS_UNDEFINED, 1, args);
                JS_FreeValue(m_ctx, ret);
                JS_FreeValue(m_ctx, errMsg);
                JS_FreeValue(m_ctx, resolve);
                JS_FreeValue(m_ctx, reject);
                return;
            }
        }
    }

    // 检测冲突 2：同一个 Task 不应同时出现在多个 pending group 中。
    // 使用 stateId()（TaskState 原始指针）做精确身份比较，
    // 而非 owner()（Driver 指针），避免误判来自同一 Driver 的不同请求。
    for (const auto& task : tasks) {
        if (!task.isValid()) continue;
        for (const auto& existing : m_pending) {
            for (const auto& et : existing.tasks) {
                if (et.isValid()
                    && et.stateId() == task.stateId()) {
                    // 不使用 JS_ThrowInternalError（addGroup 非 JS 回调入口），
                    // 直接 reject Promise 并释放回调
                    JSValue errMsg = JS_NewString(m_ctx,
                        "waitAny conflict: the same Task "
                        "is already in a pending waitAny group");
                    JSValue args[1] = {errMsg};
                    JSValue ret = JS_Call(m_ctx, reject,
                                          JS_UNDEFINED, 1, args);
                    JS_FreeValue(m_ctx, ret);
                    JS_FreeValue(m_ctx, errMsg);
                    JS_FreeValue(m_ctx, resolve);
                    JS_FreeValue(m_ctx, reject);
                    return;
                }
            }
        }
    }

    PendingGroup group;
    group.tasks = tasks;
    group.timeoutMs = timeoutMs;
    group.resolve = resolve;
    group.reject = reject;
    group.elapsed.start();
    m_pending.push_back(group);
}
```

**`poll()` 核心逻辑**：

将所有 pending group 的 tasks 合并为一个数组，只调用一次 `waitAnyNext`，
再通过索引映射反查属于哪个 group。避免多 group 场景下延迟线性放大。

```cpp
bool WaitAnyScheduler::poll(int timeoutMs) {
    if (!m_ctx || m_pending.isEmpty()) {
        return false;
    }

    // 1. 合并所有 group 的 tasks，建立索引映射
    QVector<stdiolink::Task> allTasks;
    QVector<QPair<int,int>> indexMap; // (groupIdx, taskIdxInGroup)
    for (int g = 0; g < m_pending.size(); ++g) {
        for (int t = 0; t < m_pending[g].tasks.size(); ++t) {
            allTasks.push_back(m_pending[g].tasks[t]);
            indexMap.push_back({g, t});
        }
    }

    // 2. 统一调用一次 waitAnyNext
    stdiolink::AnyItem anyItem;
    const bool gotMessage =
        stdiolink::waitAnyNext(allTasks, anyItem, timeoutMs);

    if (gotMessage && anyItem.taskIndex >= 0
        && anyItem.taskIndex < indexMap.size()) {
        auto [groupIdx, taskIdx] = indexMap[anyItem.taskIndex];
        JSValue result = JS_NewObject(m_ctx);
        JS_SetPropertyStr(m_ctx, result, "taskIndex",
                          JS_NewInt32(m_ctx, taskIdx));
        JS_SetPropertyStr(m_ctx, result, "msg",
                          messageToJs(m_ctx, anyItem.msg));
        settleGroup(groupIdx, result, false);
    }

    // 3. 检查超时和全部完成的 group（倒序遍历以安全删除）
    for (int g = m_pending.size() - 1; g >= 0; --g) {
        PendingGroup& group = m_pending[g];

        // 用户指定的超时已到
        if (group.timeoutMs >= 0
            && group.elapsed.elapsed() >= group.timeoutMs) {
            settleGroup(g, JS_NULL, false);
            continue;
        }

        // 检测进程已退出但 Task 未收到终结消息的情况
        // Driver::terminate() 不会设置 TaskState::terminal，
        // 需要通过 Task::forceTerminal() 标记终态，确保 JS 侧
        // task.done 为 true，避免 while (!task.done) 死循环
        bool hasOrphan = false;
        int orphanIdx = -1;
        for (int t = 0; t < group.tasks.size(); ++t) {
            auto& task = group.tasks[t];
            if (task.isValid() && !task.isDone() && task.owner()
                && !task.owner()->isRunning()) {
                // 标记 TaskState 为终态（共享 shared_ptr，
                // JS 侧的 Task 对象同步生效）
                task.forceTerminal(1001,
                    "driver process exited unexpectedly");
                hasOrphan = true;
                orphanIdx = t;
                break;
            }
        }
        if (hasOrphan) {
            JSValue msg = JS_NewObject(m_ctx);
            JS_SetPropertyStr(m_ctx, msg, "status",
                              JS_NewString(m_ctx, "error"));
            JS_SetPropertyStr(m_ctx, msg, "code",
                              JS_NewInt32(m_ctx, 1001));
            JSValue errData = JS_NewObject(m_ctx);
            JS_SetPropertyStr(m_ctx, errData, "message",
                              JS_NewString(m_ctx,
                                  "driver process exited unexpectedly"));
            JS_SetPropertyStr(m_ctx, msg, "data", errData);

            JSValue result = JS_NewObject(m_ctx);
            JS_SetPropertyStr(m_ctx, result, "taskIndex",
                              JS_NewInt32(m_ctx, orphanIdx));
            JS_SetPropertyStr(m_ctx, result, "msg", msg);
            settleGroup(g, result, false);
            continue;
        }

        // 所有 Task 已完成且无排队消息
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
        "    if (!Array.isArray(tasks)) {\n"
        "      throw new TypeError('waitAny(tasks, timeoutMs?): tasks must be an array');\n"
        "    }\n"
        "    if (tasks.length === 0) {\n"
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

// 创建 WaitAnyScheduler 并注册全局函数（插入在 JsTaskScheduler 之后、evalFile 之前）
WaitAnyScheduler waitAnyScheduler(engine.context());
WaitAnyScheduler::installGlobal(engine.context(), &waitAnyScheduler);

// 执行脚本（与现有代码一致，使用 svcDir.entryPath()）
int ret = engine.evalFile(svcDir.entryPath());

// 扩展后的主循环（新增 waitAnyScheduler 条件和 poll 调用）
while (scheduler.hasPending()
       || waitAnyScheduler.hasPending()
       || engine.hasPendingJobs()) {
    if (scheduler.hasPending()) {
        scheduler.poll(50);
    }
    if (waitAnyScheduler.hasPending()) {
        waitAnyScheduler.poll(50);    // 新增
    }
    while (engine.hasPendingJobs()) {
        engine.executePendingJobs();
    }
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
| `src/stdiolink_service/CMakeLists.txt` | 更新：新增 js_wait_any_scheduler.* 和 wait_any_wrapper.* |
| `src/stdiolink/host/task.h` | 更新：新增 `stateId()` 和 `forceTerminal()` 方法声明 |
| `src/stdiolink/host/task.cpp` | 更新：新增 `forceTerminal()` 实现 |
| `src/demo/js_runtime_demo/services/wait_any/manifest.json` | 新增：waitAny 演示服务元信息 |
| `src/demo/js_runtime_demo/services/wait_any/index.js` | 新增：waitAny 演示脚本 |
| `doc/manual/10-js-service/README.md` | 更新：架构图、导出列表、目录索引 |
| `doc/manual/10-js-service/driver-binding.md` | 更新：Task API 表格补充 waitAny 引用 |
| `doc/manual/10-js-service/proxy-and-scheduler.md` | 更新：API 对照表新增 waitAny 行 |
| `doc/manual/10-js-service/wait-any-binding.md` | 新增：waitAny API 完整用户手册 |

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

### 6.8 中间 event 消息验证（需要真实 Driver）

```cpp
TEST_F(JsWaitAnyTest, IntermediateEventMessages) {
    QString path = createScript(QString(
        "import { Driver, waitAny } from 'stdiolink';\n"
        "const d = new Driver();\n"
        "d.start('%1');\n"
        "// batch 命令会产生 progress event 消息\n"
        "const task = d.request('batch', {\n"
        "  operations: [\n"
        "    { op: 'add', a: 1, b: 2 },\n"
        "    { op: 'add', a: 3, b: 4 }\n"
        "  ]\n"
        "});\n"
        "let eventCount = 0;\n"
        "let doneCount = 0;\n"
        "while (true) {\n"
        "  const r = await waitAny([task]);\n"
        "  if (!r) break;\n"
        "  if (r.msg.status === 'event') eventCount++;\n"
        "  if (r.msg.status === 'done') doneCount++;\n"
        "}\n"
        "globalThis.eventCount = eventCount;\n"
        "globalThis.doneCount = doneCount;\n"
        "d.terminate();\n"
    ).arg(driverPath));
    int ret = runScript(path);
    EXPECT_EQ(ret, 0);
    EXPECT_GE(getGlobalInt("eventCount"), 1);  // 至少收到 1 条 event
    EXPECT_EQ(getGlobalInt("doneCount"), 1);    // 恰好 1 条 done
}
```

## 7. 依赖关系

### 7.1 前置依赖

| 依赖项 | 说明 |
|--------|------|
| 里程碑 23（Driver/Task JS 绑定） | `JsTaskBinding::toTask()` 用于从 JS 对象提取 C++ Task |
| 里程碑 25（Proxy 与 Scheduler） | `JsTaskScheduler` 的设计模式、`__scheduleTask` 全局函数注册方式、主循环结构 |
| stdiolink 核心库 `waitAnyNext` | `src/stdiolink/host/wait_any.h` 提供的多 Task 多路监听 C++ 实现 |
| stdiolink 核心库 `Task` 扩展 | 需要在 `Task` 类上新增两个公开方法（参见 3.0 节） |

### 7.2 后置影响

本里程碑为独立的 JS API 扩展，不修改现有 API 的行为，无后置依赖。后续里程碑可基于 `waitAny` 构建更高层的抽象（如事件流迭代器、响应式管道等）。

## 8. Demo 演示更新

### 8.1 新增 Demo：`wait_any` 服务

在 `src/demo/js_runtime_demo/services/` 下新增 `wait_any/` 目录，演示 `waitAny` 的核心使用场景。

**目录结构**：

```
src/demo/js_runtime_demo/services/wait_any/
├── manifest.json
└── index.js
```

**manifest.json**：

```json
{
  "manifestVersion": "1",
  "id": "wait_any",
  "name": "waitAny 多路事件监听演示",
  "version": "1.0.0",
  "description": "演示 waitAny API 的多 Task 多路监听、中间事件消费、超时控制等场景"
}
```

**index.js**：

```javascript
import { Driver, waitAny } from "stdiolink";
import { startDriverAuto } from "../../shared/lib/runtime_utils.js";

// ============================================================
// 场景 1：单 Task 事件流消费
// 对比 driver_task demo 中的 task.waitNext() 同步阻塞方式，
// waitAny 以异步 Promise 方式逐条消费中间 event 消息
// ============================================================

console.log("=== 场景 1：单 Task 事件流消费 ===");

const d1 = new Driver();
startDriverAuto(d1, "calculator_driver", ["--profile=keepalive"]);

const batchTask = d1.request("batch", {
    operations: [
        { type: "add", a: 1, b: 2 },
        { type: "mul", a: 3, b: 4 },
        { type: "sub", a: 10, b: 5 }
    ]
});

let eventCount = 0;
while (!batchTask.done) {
    const result = await waitAny([batchTask]);
    if (!result) break;
    if (result.msg.status === "event") {
        eventCount++;
        console.log(`  event #${eventCount}:`, JSON.stringify(result.msg.data));
    } else if (result.msg.status === "done") {
        console.log("  done:", JSON.stringify(result.msg.data));
    } else {
        console.error("  error:", JSON.stringify(result.msg.data));
    }
}
console.log(`  共收到 ${eventCount} 条中间事件\n`);

// ============================================================
// 场景 2：多 Driver 多路监听
// 同时监听两个 Driver 的请求，任意一个有消息即返回
// ============================================================

console.log("=== 场景 2：多 Driver 多路监听 ===");

const d2 = new Driver();
startDriverAuto(d2, "calculator_driver", ["--profile=keepalive"]);

const taskA = d1.request("add", { a: 100, b: 200 });
const taskB = d2.request("multiply", { a: 6, b: 7 });

while (!taskA.done || !taskB.done) {
    const result = await waitAny([taskA, taskB]);
    if (!result) break;
    const label = result.taskIndex === 0 ? "Driver1/add" : "Driver2/multiply";
    console.log(`  ${label} [${result.msg.status}]:`,
                JSON.stringify(result.msg.data));
}
console.log("");

// ============================================================
// 场景 3：超时控制
// 使用 timeoutMs 参数避免无限等待
// ============================================================

console.log("=== 场景 3：超时控制 ===");

const taskC = d1.request("add", { a: 1, b: 1 });

// 先消费完所有消息
while (true) {
    const r = await waitAny([taskC]);
    if (!r) break;
    console.log("  consumed:", r.msg.status);
}

// Task 已完成，再次 waitAny 带超时 — 立即返回 null
const timeout_result = await waitAny([taskC], 1000);
console.log("  已完成 Task + 超时: result =", timeout_result);
console.log("");

// ============================================================
// 场景 4：空数组快速路径
// ============================================================

console.log("=== 场景 4：空数组快速路径 ===");
const empty_result = await waitAny([]);
console.log("  空数组: result =", empty_result);

d1.terminate();
d2.terminate();
console.log("\n所有场景执行完毕");
```

### 8.2 与现有 Demo 的关系

| 现有 Demo | 演示内容 | 与 wait_any 的对比 |
|-----------|---------|-------------------|
| `driver_task` | `task.waitNext()` 同步阻塞消费事件 | `wait_any` 场景 1 用 `waitAny` 异步消费同类事件流 |
| `proxy_scheduler` | `openDriver()` + Promise 异步调用 | `wait_any` 场景 2 展示保留中间消息的多路监听 |

**不修改现有 Demo**。`driver_task` 和 `proxy_scheduler` 保持原样，作为同步阻塞和 Proxy 异步两种模式的参照。`wait_any` 作为第三种模式的独立演示。

## 9. 用户手册更新

本里程碑涉及 3 个手册文件的修改和 1 个新增文件。

### 9.1 修改文件总览

| 文件 | 变更类型 | 说明 |
|------|---------|------|
| `doc/manual/10-js-service/README.md` | 修改 | 架构图新增 `waitAny`，模块导出列表新增 `waitAny`，目录索引新增链接 |
| `doc/manual/10-js-service/driver-binding.md` | 修改 | Task API 表格补充 `waitAny` 交叉引用 |
| `doc/manual/10-js-service/proxy-and-scheduler.md` | 修改 | 同步 vs 异步 API 对照表新增 `waitAny` 行 |
| `doc/manual/10-js-service/wait-any-binding.md` | **新增** | waitAny API 完整文档 |

### 9.2 修改：`doc/manual/10-js-service/README.md`

**变更 1**：架构图中 `stdiolink 内置模块` 层新增 `waitAny`：

```diff
 │            stdiolink 内置模块                 │
-│  Driver / Task / openDriver / exec          │
+│  Driver / Task / openDriver / exec / waitAny│
 │  getConfig                                  │
```

**变更 2**：模块导出代码块新增 `waitAny`：

```diff
 import {
     Driver,         // Driver 类（底层 API）
     openDriver,     // Proxy 工厂函数（推荐）
+    waitAny,        // 多路事件监听（异步 Promise）
     exec,           // 外部进程执行
     getConfig       // 读取配置值
 } from 'stdiolink';
```

**变更 3**：本章内容索引新增链接：

```diff
 - [Proxy 代理与并发调度](proxy-and-scheduler.md) - openDriver() 与异步调用
+- [多路事件监听](wait-any-binding.md) - waitAny() 异步多 Task 监听
 - [配置系统](config-schema.md) - config.schema.json 与 getConfig 配置管理
```

### 9.3 修改：`doc/manual/10-js-service/driver-binding.md`

在 Task 类的 API 参考表格末尾新增提示行，引导用户了解 `waitAny`：

```diff
 | `finalPayload` | `any` | 只读，最终响应数据 |
+
+> **多路监听**：如需同时等待多个 Task 的消息（含中间 event），参见 [waitAny()](wait-any-binding.md)。
```

### 9.4 修改：`doc/manual/10-js-service/proxy-and-scheduler.md`

在末尾的「同步 vs 异步 API」对照表中新增 `waitAny` 行：

```diff
 | API | 调用方式 | 说明 |
 |-----|---------|------|
 | `task.waitNext()` | 同步阻塞 | 底层 API，阻塞 JS 线程 |
 | `calc.add(params)` | 异步 Promise | Proxy 层，通过调度器非阻塞 |
 | `$rawRequest(cmd, data)` | 返回 Task | 底层 API，用户自行决定同步/异步 |
+| `waitAny(tasks, timeoutMs?)` | 异步 Promise | 多路监听，保留中间 event，详见 [waitAny](wait-any-binding.md) |
```

### 9.5 新增：`doc/manual/10-js-service/wait-any-binding.md`

完整文件内容如下：

````markdown
# 多路事件监听 (waitAny)

`waitAny` 是异步多 Task 多路监听 API，填补 `task.waitNext()`（同步阻塞）和 `openDriver()` Proxy（丢弃中间 event）之间的空白。

## API 层级对照

| API | 调用方式 | 中间消息 | 多路监听 | 适用场景 |
|-----|---------|---------|---------|---------|
| `task.waitNext()` | 同步阻塞 | 保留 | 不支持 | 简单单 Task |
| `proxy.command()` | 异步 Promise | 丢弃 | 跨 Driver | 只关心最终结果 |
| **`waitAny(tasks)`** | **异步 Promise** | **保留** | **支持** | **进度监听、事件流、多路复用** |

## 函数签名

```js
import { waitAny } from 'stdiolink';

const result = await waitAny(tasks, timeoutMs?);
```

### 参数

| 参数 | 类型 | 说明 |
|------|------|------|
| `tasks` | `Task[]` | 待监听的 Task 数组 |
| `timeoutMs` | `number` (可选) | 超时毫秒数，省略或 -1 表示无超时 |

### 返回值

resolve 为 `WaitAnyResult | null`：

```js
// 有消息时
{
    taskIndex: number,   // 产生消息的 Task 在 tasks 数组中的索引
    msg: {
        status: "event" | "done" | "error",
        code: number,
        data: any
    }
}

// 无消息时返回 null
```

### resolve 规则

| 情况 | 返回值 |
|------|--------|
| 任意 Task 有消息（含 event） | `{ taskIndex, msg }` |
| 所有 Task 已完成且无排队消息 | `null` |
| 超时 | `null` |
| 空数组 | `null` |

## 基本用法

### 单 Task 事件流消费

```js
import { Driver, waitAny } from "stdiolink";

const d = new Driver();
d.start("./my_driver");
const task = d.request("scan", { fps: 30 });

while (!task.done) {
    const result = await waitAny([task]);
    if (!result) break;

    if (result.msg.status === "event") {
        console.log("progress:", result.msg.data);
    } else if (result.msg.status === "done") {
        console.log("completed:", result.msg.data);
    } else {
        console.error("error:", result.msg.data);
    }
}

d.terminate();
```

### 多 Driver 多路监听

```js
import { Driver, waitAny } from "stdiolink";

const drvA = new Driver();
drvA.start("./camera_driver");
const drvB = new Driver();
drvB.start("./sensor_driver");

const taskA = drvA.request("scan", { fps: 30 });
const taskB = drvB.request("read", { interval: 100 });

while (!taskA.done || !taskB.done) {
    const result = await waitAny([taskA, taskB]);
    if (!result) break;

    const { taskIndex, msg } = result;
    const label = taskIndex === 0 ? "camera" : "sensor";
    console.log(`[${label}] ${msg.status}:`, msg.data);
}

drvA.terminate();
drvB.terminate();
```

### 超时控制

```js
const task = drv.request("stream", { duration: 10 });

while (!task.done) {
    const result = await waitAny([task], 3000);
    if (!result) {
        console.log("3 秒内无数据，继续等待...");
        continue;
    }
    console.log(result.msg.status, result.msg.data);
}
```

## 使用约束

### 消息消费互斥

`waitAny` 与 `task.waitNext()` / `proxy.command()` 共享同一个消息队列，消息被消费后不可重复获取。同一个 Task **不应同时**被多种 API 监听：

```js
// ❌ 错误：同一 Task 同时被 waitAny 和 proxy.command() 监听
const task = drv.request("scan", data);
const promise = proxy.scan(data);       // 内部也监听同一 Task
const result = await waitAny([task]);    // 消息可能被 promise 消费

// ✅ 正确：分别发起独立请求
const task1 = drv.request("scan", data);     // 用于 waitAny
const task2 = drv2.request("scan", data);    // 用于 proxy
```

### 不可重叠监听

同一个 Task 不应同时出现在多个 pending 的 `waitAny` 调用中，否则会被 reject：

```js
// ❌ 错误：同一 Task 在两个并发 waitAny 中
const p1 = waitAny([task]);
const p2 = waitAny([task]);  // reject: conflict

// ✅ 正确：串行调用
const r1 = await waitAny([task]);
const r2 = await waitAny([task]);
```

## 与 C++ waitAnyNext 的关系

JS 层 `waitAny` 底层通过 C++ `waitAnyNext`（参见 [Host 端多任务并行等待](../06-host/wait-any.md)）实现。区别在于：

| | C++ `waitAnyNext` | JS `waitAny` |
|-|-------------------|--------------|
| 调用方式 | 同步阻塞循环 | 异步 Promise |
| 返回语义 | `bool` + `AnyItem&` | `Promise<WaitAnyResult \| null>` |
| 每次调用 | 消费一条消息 | 消费一条消息 |
| 多路监听 | 支持 | 支持 |
````
