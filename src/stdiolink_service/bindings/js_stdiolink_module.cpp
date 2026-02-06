#include "js_stdiolink_module.h"

#include <quickjs.h>
#include "js_config.h"
#include "js_driver.h"
#include "js_process.h"
#include "js_task.h"
#include "proxy/driver_proxy.h"

namespace {

JSValue jsOpenDriverBlocked(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    stdiolink_service::JsConfigBinding::markBlockedSideEffect(ctx);
    return JS_ThrowInternalError(ctx, "openDriver() is blocked in --dump-config-schema mode");
}

int jsStdiolinkModuleInit(JSContext* ctx, JSModuleDef* module) {
    JSValue driverCtor = JsDriverBinding::getConstructor(ctx);
    if (JS_IsException(driverCtor)) {
        return -1;
    }
    JSValue execFn = JsProcessBinding::getExecFunction(ctx);
    if (JS_IsException(execFn)) {
        JS_FreeValue(ctx, driverCtor);
        return -1;
    }
    JSValue openDriverFn = JS_UNDEFINED;
    if (stdiolink_service::JsConfigBinding::isDumpSchemaMode(ctx)) {
        openDriverFn = JS_NewCFunction(ctx, jsOpenDriverBlocked, "openDriver", 2);
    } else {
        openDriverFn = createOpenDriverFunction(ctx, driverCtor);
    }
    if (JS_IsException(openDriverFn)) {
        JS_FreeValue(ctx, driverCtor);
        JS_FreeValue(ctx, execFn);
        return -1;
    }

    JSValue defineConfigFn = stdiolink_service::JsConfigBinding::getDefineConfigFunction(ctx);
    JSValue getConfigFn = stdiolink_service::JsConfigBinding::getGetConfigFunction(ctx);

    if (JS_SetModuleExport(ctx, module, "Driver", driverCtor) < 0) {
        JS_FreeValue(ctx, openDriverFn);
        JS_FreeValue(ctx, execFn);
        JS_FreeValue(ctx, defineConfigFn);
        JS_FreeValue(ctx, getConfigFn);
        return -1;
    }
    if (JS_SetModuleExport(ctx, module, "exec", execFn) < 0) {
        JS_FreeValue(ctx, openDriverFn);
        JS_FreeValue(ctx, defineConfigFn);
        JS_FreeValue(ctx, getConfigFn);
        return -1;
    }
    if (JS_SetModuleExport(ctx, module, "openDriver", openDriverFn) < 0) {
        JS_FreeValue(ctx, defineConfigFn);
        JS_FreeValue(ctx, getConfigFn);
        return -1;
    }
    if (JS_SetModuleExport(ctx, module, "defineConfig", defineConfigFn) < 0) {
        JS_FreeValue(ctx, getConfigFn);
        return -1;
    }
    return JS_SetModuleExport(ctx, module, "getConfig", getConfigFn);
}

} // namespace

JSModuleDef* jsInitStdiolinkModule(JSContext* ctx, const char* name) {
    JsTaskBinding::registerClass(ctx);
    JsDriverBinding::registerClass(ctx);

    JSModuleDef* module = JS_NewCModule(ctx, name, jsStdiolinkModuleInit);
    if (!module) {
        return nullptr;
    }
    if (JS_AddModuleExport(ctx, module, "Driver") < 0) {
        return nullptr;
    }
    if (JS_AddModuleExport(ctx, module, "exec") < 0) {
        return nullptr;
    }
    if (JS_AddModuleExport(ctx, module, "openDriver") < 0) {
        return nullptr;
    }
    if (JS_AddModuleExport(ctx, module, "defineConfig") < 0) {
        return nullptr;
    }
    if (JS_AddModuleExport(ctx, module, "getConfig") < 0) {
        return nullptr;
    }
    return module;
}
