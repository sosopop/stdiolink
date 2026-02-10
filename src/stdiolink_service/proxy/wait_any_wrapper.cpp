#include "wait_any_wrapper.h"

#include <cstring>

JSValue createWaitAnyFunction(JSContext* ctx) {
    static const char kFactorySource[] =
        "(function(){\n"
        "  return async function waitAny(tasks, timeoutMs) {\n"
        "    if (!Array.isArray(tasks)) {\n"
        "      throw new TypeError('waitAny(tasks, timeoutMs?): tasks must be an array');\n"
        "    }\n"
        "    if (timeoutMs !== undefined && timeoutMs !== null) {\n"
        "      if (!Number.isFinite(timeoutMs) || timeoutMs < 0) {\n"
        "        throw new TypeError('waitAny(tasks, timeoutMs?): timeoutMs must be >= 0');\n"
        "      }\n"
        "    }\n"
        "    const ms = (timeoutMs === undefined || timeoutMs === null) ? -1 : Math.floor(timeoutMs);\n"
        "    return globalThis.__waitAny(tasks, ms);\n"
        "  };\n"
        "})";

    JSValue factory = JS_Eval(ctx, kFactorySource, std::strlen(kFactorySource),
                              "<stdiolink/wait_any_factory>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(factory)) {
        return factory;
    }

    JSValue waitAnyFn = JS_Call(ctx, factory, JS_UNDEFINED, 0, nullptr);
    JS_FreeValue(ctx, factory);
    return waitAnyFn;
}
