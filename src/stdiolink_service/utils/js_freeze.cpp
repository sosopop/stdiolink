#include "js_freeze.h"

#include <quickjs.h>

namespace stdiolink_service {

JSValue deepFreezeObject(JSContext* ctx, JSValue obj) {
    if (!JS_IsObject(obj)) return obj;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue objectCtor = JS_GetPropertyStr(ctx, global, "Object");
    JSValue freezeFn = JS_GetPropertyStr(ctx, objectCtor, "freeze");
    JSValue keysFn = JS_GetPropertyStr(ctx, objectCtor, "keys");

    // Recursively freeze child properties first
    JSValue keys = JS_Call(ctx, keysFn, objectCtor, 1, &obj);
    if (!JS_IsException(keys)) {
        JSValue lenVal = JS_GetPropertyStr(ctx, keys, "length");
        uint32_t len = 0;
        JS_ToUint32(ctx, &len, lenVal);
        JS_FreeValue(ctx, lenVal);
        for (uint32_t i = 0; i < len; ++i) {
            JSValue key = JS_GetPropertyUint32(ctx, keys, i);
            const char* keyStr = JS_ToCString(ctx, key);
            if (keyStr) {
                JSValue child = JS_GetPropertyStr(ctx, obj, keyStr);
                if (JS_IsObject(child)) {
                    deepFreezeObject(ctx, child);
                }
                JS_FreeValue(ctx, child);
                JS_FreeCString(ctx, keyStr);
            }
            JS_FreeValue(ctx, key);
        }
    }
    JS_FreeValue(ctx, keys);

    // Freeze self
    JSValue result = JS_Call(ctx, freezeFn, objectCtor, 1, &obj);
    JS_FreeValue(ctx, result);

    JS_FreeValue(ctx, keysFn);
    JS_FreeValue(ctx, freezeFn);
    JS_FreeValue(ctx, objectCtor);
    JS_FreeValue(ctx, global);
    return obj;
}

} // namespace stdiolink_service
