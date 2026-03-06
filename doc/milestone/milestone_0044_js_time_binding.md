# 里程碑 44：JS Time 基础库绑定

> **前置条件**: 里程碑 43 已完成
> **目标**: 实现 `stdiolink/time`，提供统一时间获取与非阻塞 sleep 能力

---

## 1. 目标

- 新增 `stdiolink/time` 模块
- 提供 `nowMs()`、`monotonicMs()`、`sleep(ms)`
- `sleep(ms)` 必须返回 Promise 且不阻塞 JS 事件循环

---

## 2. 设计原则（强约束）

- **简约**: 仅提供时间基础能力，不引入 Cron/时区调度器
- **可靠**: `sleep` 生命周期安全，不出现悬挂 Promise
- **稳定**: 时间语义固定，跨平台一致
- **避免过度设计**: 不实现高精度定时、间隔器族 API

---

## 3. 范围与非目标

### 3.1 范围（M44 内）

- `stdiolink/time` 三个函数
- Promise 与 Qt 事件循环桥接
- 单元测试与文档

### 3.2 非目标（M44 外）

- 不提供 `setInterval/clearInterval`
- 不提供时区转换 API

---

## 4. 技术方案

### 4.1 模块接口

```js
import { nowMs, monotonicMs, sleep } from "stdiolink/time";
```

- `nowMs()`: 返回当前 Unix epoch 毫秒数（`number`）
- `monotonicMs()`: 返回单调递增毫秒数（`number`，进程启动后归零）
- `sleep(ms)`: 返回 `Promise<void>`，非阻塞延迟

### 4.2 创建 bindings/js_time.h

```cpp
#pragma once

#include <quickjs.h>

namespace stdiolink_service {

/// @brief stdiolink/time 内置模块绑定
///
/// 提供时间获取与非阻塞 sleep。sleep 通过 QTimer::singleShot
/// 桥接到 QuickJS Promise，不阻塞 JS 事件循环。
/// 绑定状态按 JSRuntime 维度隔离，析构时安全清理 pending sleep。
class JsTimeBinding {
public:
    /// 绑定到指定 runtime
    static void attachRuntime(JSRuntime* rt);

    /// 解绑并清理所有 pending sleep
    static void detachRuntime(JSRuntime* rt);

    /// 模块初始化回调（注册给 ModuleLoader）
    static JSModuleDef* initModule(JSContext* ctx, const char* name);

    /// 重置状态（测试用）
    static void reset(JSContext* ctx);
};

} // namespace stdiolink_service
```

### 4.3 创建 bindings/js_time.cpp

```cpp
#include "js_time.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QTimer>
#include <QList>
#include <cmath>
#include <quickjs.h>

namespace stdiolink_service {

namespace {

struct PendingSleep {
    JSValue resolve = JS_UNDEFINED;
    JSValue reject = JS_UNDEFINED;
    QTimer* timer = nullptr;
};

struct TimeState {
    QElapsedTimer monotonic;
    QList<PendingSleep> pendingSleeps;
    JSContext* ctx = nullptr;
    bool monotonicStarted = false;
};

QHash<quintptr, TimeState> s_states;

quintptr runtimeKey(JSContext* ctx) {
    return reinterpret_cast<quintptr>(JS_GetRuntime(ctx));
}

TimeState& stateFor(JSContext* ctx) {
    return s_states[runtimeKey(ctx)];
}

JSValue jsNowMs(JSContext* ctx, JSValueConst,
                int, JSValueConst*) {
    return JS_NewFloat64(ctx,
        static_cast<double>(
            QDateTime::currentMSecsSinceEpoch()));
}

JSValue jsMonotonicMs(JSContext* ctx, JSValueConst,
                      int, JSValueConst*) {
    auto& state = stateFor(ctx);
    if (!state.monotonicStarted) {
        state.monotonic.start();
        state.monotonicStarted = true;
    }
    return JS_NewFloat64(ctx,
        static_cast<double>(state.monotonic.elapsed()));
}

JSValue jsSleep(JSContext* ctx, JSValueConst,
                int argc, JSValueConst* argv) {
    // 参数校验
    if (argc < 1 || !JS_IsNumber(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "sleep: argument must be a number");
    }
    double ms;
    JS_ToFloat64(ctx, &ms, argv[0]);
    if (!std::isfinite(ms) || ms < 0) {
        return JS_ThrowRangeError(ctx,
            "sleep: ms must be a finite number >= 0, got %f", ms);
    }

    // 创建 Promise
    JSValue resolvingFuncs[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolvingFuncs);
    if (JS_IsException(promise)) {
        return promise;
    }

    auto& state = stateFor(ctx);
    int sleepIdx = state.pendingSleeps.size();

    PendingSleep pending;
    pending.resolve = resolvingFuncs[0];
    pending.reject = resolvingFuncs[1];

    // QTimer::singleShot 桥接
    QTimer* timer = new QTimer();
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, [ctx, sleepIdx]() {
        auto& st = stateFor(ctx);
        if (sleepIdx < st.pendingSleeps.size()) {
            auto& p = st.pendingSleeps[sleepIdx];
            if (!JS_IsUndefined(p.resolve)) {
                JSValue ret = JS_Call(ctx, p.resolve,
                                      JS_UNDEFINED, 0, nullptr);
                JS_FreeValue(ctx, ret);
                JS_FreeValue(ctx, p.resolve);
                JS_FreeValue(ctx, p.reject);
                p.resolve = JS_UNDEFINED;
                p.reject = JS_UNDEFINED;
            }
            if (p.timer) {
                p.timer->deleteLater();
                p.timer = nullptr;
            }
        }
    });
    pending.timer = timer;
    state.pendingSleeps.append(pending);
    timer->start(static_cast<int>(ms));

    return promise;
}

int timeModuleInit(JSContext* ctx, JSModuleDef* module) {
    JS_SetModuleExport(ctx, module, "nowMs",
        JS_NewCFunction(ctx, jsNowMs, "nowMs", 0));
    JS_SetModuleExport(ctx, module, "monotonicMs",
        JS_NewCFunction(ctx, jsMonotonicMs, "monotonicMs", 0));
    JS_SetModuleExport(ctx, module, "sleep",
        JS_NewCFunction(ctx, jsSleep, "sleep", 1));
    return 0;
}

} // namespace

void JsTimeBinding::attachRuntime(JSRuntime* rt) {
    if (!rt) return;
    const quintptr key = reinterpret_cast<quintptr>(rt);
    if (!s_states.contains(key)) {
        s_states.insert(key, TimeState{});
    }
}

void JsTimeBinding::detachRuntime(JSRuntime* rt) {
    if (!rt) return;
    const quintptr key = reinterpret_cast<quintptr>(rt);
    if (!s_states.contains(key)) return;
    auto& state = s_states[key];
    // 清理所有 pending sleep
    for (auto& p : state.pendingSleeps) {
        if (p.timer) {
            p.timer->stop();
            p.timer->deleteLater();
            p.timer = nullptr;
        }
        if (!JS_IsUndefined(p.resolve) && state.ctx) {
            JS_FreeValue(state.ctx, p.resolve);
            JS_FreeValue(state.ctx, p.reject);
        }
    }
    state.pendingSleeps.clear();
    s_states.remove(key);
}

JSModuleDef* JsTimeBinding::initModule(JSContext* ctx,
                                        const char* name) {
    stateFor(ctx).ctx = ctx;
    JSModuleDef* module = JS_NewCModule(ctx, name, timeModuleInit);
    if (!module) return nullptr;
    JS_AddModuleExport(ctx, module, "nowMs");
    JS_AddModuleExport(ctx, module, "monotonicMs");
    JS_AddModuleExport(ctx, module, "sleep");
    return module;
}

void JsTimeBinding::reset(JSContext* ctx) {
    auto& state = stateFor(ctx);
    for (auto& p : state.pendingSleeps) {
        if (p.timer) {
            p.timer->stop();
            p.timer->deleteLater();
        }
        if (!JS_IsUndefined(p.resolve) && state.ctx) {
            JS_FreeValue(state.ctx, p.resolve);
            JS_FreeValue(state.ctx, p.reject);
        }
    }
    state.pendingSleeps.clear();
}

} // namespace stdiolink_service
```

### 4.4 main.cpp 集成

```cpp
#include "bindings/js_time.h"

// 在 engine 创建后：
JsTimeBinding::attachRuntime(engine.runtime());

// 模块注册（在 stdiolink/fs 之后）：
engine.registerModule("stdiolink/time", JsTimeBinding::initModule);
```

`JsTimeBinding::detachRuntime(oldRt)` 统一放在 `JsEngine::~JsEngine()` 中执行，避免重复清理。

### 4.5 与主循环的协作

`sleep` 的 `QTimer::singleShot` 依赖 Qt 事件循环。现有主循环中 `scheduler.poll(50)` 内部使用 `QEventLoop` 处理事件，`QTimer` 的 timeout 信号会在此期间被分发。因此 `sleep` 无需额外的事件循环集成。

### 4.6 JS 使用示例

```js
import { nowMs, monotonicMs, sleep } from "stdiolink/time";

console.log("当前时间:", nowMs());          // Unix epoch ms
console.log("单调时间:", monotonicMs());    // 进程启动后 ms

await sleep(1000);  // 非阻塞等待 1 秒
console.log("1 秒后:", monotonicMs());

// sleep(0) 立即 yield
await sleep(0);
```

---

## 5. 实现步骤

1. 新增 `js_time` 绑定并注册模块
2. 实现 `nowMs/monotonicMs/sleep`
3. 完成运行时销毁保护
4. 编写单测
5. 更新 manual 文档

---

## 6. 文件改动清单

### 6.1 新增文件

- `src/stdiolink_service/bindings/js_time.h`
- `src/stdiolink_service/bindings/js_time.cpp`
- `src/tests/test_time_binding.cpp`
- `doc/manual/10-js-service/time-binding.md`

### 6.2 修改文件

- `src/stdiolink_service/main.cpp`
- `src/stdiolink_service/engine/js_engine.cpp`
- `src/stdiolink_service/CMakeLists.txt`
- `src/tests/CMakeLists.txt`
- `doc/manual/10-js-service/module-system.md`
- `doc/manual/10-js-service/README.md`

---

## 7. 单元测试计划（全面覆盖）

### 7.1 测试 Fixture

```cpp
// test_time_binding.cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include "engine/js_engine.h"
#include "engine/module_loader.h"
#include "engine/console_bridge.h"
#include "bindings/js_time.h"

using namespace stdiolink_service;

class JsTimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        ModuleLoader::install(engine->context());
        ConsoleBridge::install(engine->context());
        JsTimeBinding::attachRuntime(engine->runtime());
        ModuleLoader::addBuiltin("stdiolink/time",
                                 JsTimeBinding::initModule);
    }
    void TearDown() override {
        JsTimeBinding::reset(engine->context());
        JsTimeBinding::detachRuntime(engine->runtime());
        engine.reset();
    }

    int runScript(const QString& code) {
        QTemporaryDir dir;
        QString path = dir.path() + "/test.mjs";
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(code.toUtf8());
        f.close();
        int ret = engine->evalFile(path);
        // 驱动 pending jobs（sleep Promise 回调）
        while (engine->hasPendingJobs()) {
            QCoreApplication::processEvents();
            engine->executePendingJobs();
        }
        return ret;
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

### 7.2 功能正确性

```cpp
TEST_F(JsTimeTest, NowMsReturnsNumber) {
    int ret = runScript(
        "import { nowMs } from 'stdiolink/time';\n"
        "const t = nowMs();\n"
        "globalThis.ok = (typeof t === 'number' && t > 1e12) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsTimeTest, MonotonicMsIsNonDecreasing) {
    int ret = runScript(
        "import { monotonicMs } from 'stdiolink/time';\n"
        "const a = monotonicMs();\n"
        "const b = monotonicMs();\n"
        "globalThis.ok = (typeof a === 'number'"
        " && a >= 0 && b >= a) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsTimeTest, SleepResolves) {
    int ret = runScript(
        "import { sleep } from 'stdiolink/time';\n"
        "await sleep(10);\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.3 异常与边界

```cpp
TEST_F(JsTimeTest, SleepNegativeThrowsRangeError) {
    int ret = runScript(
        "import { sleep } from 'stdiolink/time';\n"
        "try { await sleep(-1); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof RangeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsTimeTest, SleepNaNThrowsRangeError) {
    int ret = runScript(
        "import { sleep } from 'stdiolink/time';\n"
        "try { await sleep(NaN); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof RangeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsTimeTest, SleepStringThrowsTypeError) {
    int ret = runScript(
        "import { sleep } from 'stdiolink/time';\n"
        "try { await sleep('100'); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof TypeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsTimeTest, SleepNoArgThrowsTypeError) {
    int ret = runScript(
        "import { sleep } from 'stdiolink/time';\n"
        "try { await sleep(); globalThis.ok = 0; }\n"
        "catch (e) { globalThis.ok = (e instanceof TypeError) ? 1 : 0; }\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsTimeTest, SleepZeroResolvesQuickly) {
    int ret = runScript(
        "import { sleep } from 'stdiolink/time';\n"
        "await sleep(0);\n"
        "globalThis.ok = 1;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.4 回归测试

- `test_proxy_and_scheduler.cpp`
- `test_js_stress.cpp`

---

## 8. 验收标准（DoD）

- `stdiolink/time` API 可用且语义稳定
- `sleep` 非阻塞，异常输入可控
- 单测与回归全部通过

---

## 9. 风险与控制

- **风险 1**：Promise 句柄释放不当
  - 控制：统一管理 pending 项，析构时清理
- **风险 2**：时间断言偶发抖动
  - 控制：测试使用容差窗口而非绝对值
