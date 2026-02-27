#pragma once

#include <quickjs.h>

namespace stdiolink_service {

class JsDriverResolveBinding {
public:
    static void attachRuntime(JSRuntime* rt);
    static void detachRuntime(JSRuntime* rt);
    static JSModuleDef* initModule(JSContext* ctx, const char* name);
};

} // namespace stdiolink_service
