#include "js_config.h"

#include <QHash>
#include "utils/js_convert.h"
#include "utils/js_freeze.h"

namespace stdiolink_service {

namespace {

struct ConfigState {
    QJsonObject mergedConfig;
    JSValue cachedConfigJs = JS_UNDEFINED;  // cached frozen object
    JSContext* ownerCtx = nullptr;
};

QHash<quintptr, ConfigState> s_configStates;

quintptr runtimeKey(JSContext* ctx) {
    return reinterpret_cast<quintptr>(JS_GetRuntime(ctx));
}

ConfigState& stateFor(JSContext* ctx) {
    return s_configStates[runtimeKey(ctx)];
}

void clearCachedConfig(ConfigState& state) {
    if (!JS_IsUndefined(state.cachedConfigJs) && state.ownerCtx) {
        JS_FreeValue(state.ownerCtx, state.cachedConfigJs);
    }
    state.cachedConfigJs = JS_UNDEFINED;
    state.ownerCtx = nullptr;
}

JSValue jsGetConfig(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto& state = stateFor(ctx);
    if (JS_IsUndefined(state.cachedConfigJs)) {
        JSValue configJs = qjsonObjectToJsValue(ctx, state.mergedConfig);
        state.cachedConfigJs = deepFreezeObject(ctx, configJs);
        state.ownerCtx = ctx;
    }
    return JS_DupValue(ctx, state.cachedConfigJs);
}

} // namespace

void JsConfigBinding::attachRuntime(JSRuntime* rt) {
    if (!rt) return;
    const quintptr key = reinterpret_cast<quintptr>(rt);
    if (!s_configStates.contains(key)) {
        s_configStates.insert(key, ConfigState{});
    }
}

void JsConfigBinding::detachRuntime(JSRuntime* rt) {
    if (!rt) return;
    const quintptr key = reinterpret_cast<quintptr>(rt);
    if (s_configStates.contains(key)) {
        clearCachedConfig(s_configStates[key]);
        s_configStates.remove(key);
    }
}

JSValue JsConfigBinding::getGetConfigFunction(JSContext* ctx) {
    return JS_NewCFunction(ctx, jsGetConfig, "getConfig", 0);
}

void JsConfigBinding::setMergedConfig(JSContext* ctx, const QJsonObject& mergedConfig) {
    auto& state = stateFor(ctx);
    clearCachedConfig(state);
    state.mergedConfig = mergedConfig;
}

void JsConfigBinding::reset(JSContext* ctx) {
    const quintptr key = runtimeKey(ctx);
    if (s_configStates.contains(key)) {
        clearCachedConfig(s_configStates[key]);
    }
    s_configStates[key] = ConfigState{};
}

} // namespace stdiolink_service
