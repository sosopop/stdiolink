#pragma once

#include <quickjs.h>

class JsDriverBinding {
public:
    static void registerClass(JSContext* ctx);
    static JSValue getConstructor(JSContext* ctx);
    static void detachRuntime(JSRuntime* rt);
};
