#include "js_config.h"

#include <QHash>
#include <QJsonDocument>
#include "config/service_config_validator.h"
#include "utils/js_convert.h"

namespace stdiolink_service {

namespace {

struct ConfigState {
    bool schemaDefined = false;
    bool dumpSchemaMode = false;
    bool blockedSideEffect = false;
    ServiceConfigSchema schema;
    QJsonObject rawCliConfig;
    QJsonObject fileConfig;
    QJsonObject mergedConfig;
};

QHash<quintptr, ConfigState> s_configStates;

quintptr runtimeKey(JSContext* ctx) {
    return reinterpret_cast<quintptr>(JS_GetRuntime(ctx));
}

ConfigState& stateFor(JSContext* ctx) {
    return s_configStates[runtimeKey(ctx)];
}

JSValue freezeObject(JSContext* ctx, JSValue obj) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue objectCtor = JS_GetPropertyStr(ctx, global, "Object");
    JSValue freezeFn = JS_GetPropertyStr(ctx, objectCtor, "freeze");
    JSValue result = JS_Call(ctx, freezeFn, objectCtor, 1, &obj);
    JS_FreeValue(ctx, freezeFn);
    JS_FreeValue(ctx, objectCtor);
    JS_FreeValue(ctx, global);
    if (JS_IsException(result)) {
        JS_FreeValue(ctx, result);
        return obj;
    }
    JS_FreeValue(ctx, result);
    return obj;
}

JSValue jsDefineConfig(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto& state = stateFor(ctx);

    if (state.schemaDefined) {
        return JS_ThrowInternalError(ctx, "defineConfig() can only be called once");
    }

    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "defineConfig(schema): schema must be an object");
    }

    QJsonObject schemaObj = jsValueToQJsonObject(ctx, argv[0]);
    state.schema = ServiceConfigSchema::fromJsObject(schemaObj);
    state.schemaDefined = true;

    // In dump-schema mode, skip merge/validate and return undefined
    if (state.dumpSchemaMode) {
        return JS_UNDEFINED;
    }

    // Merge and validate
    QJsonObject merged;
    auto vr = ServiceConfigValidator::mergeAndValidate(
        state.schema,
        state.fileConfig,
        state.rawCliConfig,
        UnknownFieldPolicy::Reject,
        merged);

    if (!vr.valid) {
        return JS_ThrowInternalError(ctx, "defineConfig() validation failed: %s",
                                     vr.toString().toUtf8().constData());
    }

    state.mergedConfig = merged;

    // Return frozen config object
    JSValue configJs = qjsonObjectToJsValue(ctx, merged);
    return freezeObject(ctx, configJs);
}

JSValue jsGetConfig(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto& state = stateFor(ctx);

    if (!state.schemaDefined) {
        JSValue empty = JS_NewObject(ctx);
        return freezeObject(ctx, empty);
    }

    JSValue configJs = qjsonObjectToJsValue(ctx, state.mergedConfig);
    return freezeObject(ctx, configJs);
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
    s_configStates.remove(reinterpret_cast<quintptr>(rt));
}

JSValue JsConfigBinding::getDefineConfigFunction(JSContext* ctx) {
    return JS_NewCFunction(ctx, jsDefineConfig, "defineConfig", 1);
}

JSValue JsConfigBinding::getGetConfigFunction(JSContext* ctx) {
    return JS_NewCFunction(ctx, jsGetConfig, "getConfig", 0);
}

bool JsConfigBinding::hasSchema(JSContext* ctx) {
    return stateFor(ctx).schemaDefined;
}

ServiceConfigSchema JsConfigBinding::getSchema(JSContext* ctx) {
    return stateFor(ctx).schema;
}

void JsConfigBinding::setRawConfig(JSContext* ctx,
                                   const QJsonObject& rawCli,
                                   const QJsonObject& file,
                                   bool dumpSchemaMode) {
    auto& state = stateFor(ctx);
    state.rawCliConfig = rawCli;
    state.fileConfig = file;
    state.dumpSchemaMode = dumpSchemaMode;
}

bool JsConfigBinding::isDumpSchemaMode(JSContext* ctx) {
    return stateFor(ctx).dumpSchemaMode;
}

void JsConfigBinding::markBlockedSideEffect(JSContext* ctx) {
    stateFor(ctx).blockedSideEffect = true;
}

bool JsConfigBinding::takeBlockedSideEffectFlag(JSContext* ctx) {
    auto& state = stateFor(ctx);
    const bool blocked = state.blockedSideEffect;
    state.blockedSideEffect = false;
    return blocked;
}

void JsConfigBinding::reset(JSContext* ctx) {
    s_configStates[runtimeKey(ctx)] = ConfigState{};
}

} // namespace stdiolink_service
