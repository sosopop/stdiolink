#include "js_time.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QTimer>
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
    QHash<int, PendingSleep> pendingSleeps;
    int nextSleepId = 0;
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

JSValue jsNowMs(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewFloat64(ctx,
        static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
}

JSValue jsMonotonicMs(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto& state = stateFor(ctx);
    if (!state.monotonicStarted) {
        state.monotonic.start();
        state.monotonicStarted = true;
    }
    return JS_NewFloat64(ctx,
        static_cast<double>(state.monotonic.elapsed()));
}

JSValue jsSleep(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsNumber(argv[0])) {
        return JS_ThrowTypeError(ctx, "sleep: argument must be a number");
    }
    double ms;
    JS_ToFloat64(ctx, &ms, argv[0]);
    if (!std::isfinite(ms) || ms < 0) {
        return JS_ThrowRangeError(ctx,
            "sleep: ms must be a finite number >= 0, got %f", ms);
    }

    JSValue resolvingFuncs[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolvingFuncs);
    if (JS_IsException(promise)) {
        return promise;
    }

    auto& state = stateFor(ctx);
    int sleepId = state.nextSleepId++;

    PendingSleep pending;
    pending.resolve = resolvingFuncs[0];
    pending.reject = resolvingFuncs[1];

    QTimer* timer = new QTimer();
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, [ctx, sleepId]() {
        auto& st = stateFor(ctx);
        if (!st.pendingSleeps.contains(sleepId)) return;
        auto& p = st.pendingSleeps[sleepId];
        if (!JS_IsUndefined(p.resolve)) {
            JSValue ret = JS_Call(ctx, p.resolve,
                                  JS_UNDEFINED, 0, nullptr);
            JS_FreeValue(ctx, ret);
            JS_FreeValue(ctx, p.resolve);
            JS_FreeValue(ctx, p.reject);
        }
        if (p.timer) {
            p.timer->deleteLater();
        }
        st.pendingSleeps.remove(sleepId);
    });
    pending.timer = timer;
    state.pendingSleeps.insert(sleepId, pending);
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

JSModuleDef* JsTimeBinding::initModule(JSContext* ctx, const char* name) {
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

bool JsTimeBinding::hasPending(JSContext* ctx) {
    auto& state = stateFor(ctx);
    return !state.pendingSleeps.isEmpty();
}

} // namespace stdiolink_service
