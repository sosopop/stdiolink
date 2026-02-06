#pragma once

#include <quickjs.h>
#include "stdiolink/host/task.h"

class JsTaskBinding {
public:
    static void registerClass(JSContext* ctx);
    static JSValue createFromTask(JSContext* ctx, const stdiolink::Task& task);
    static bool toTask(JSContext* ctx, JSValueConst value, stdiolink::Task& outTask);
    static void detachRuntime(JSRuntime* rt);
};
