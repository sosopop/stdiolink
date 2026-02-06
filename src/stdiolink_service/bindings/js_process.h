#pragma once

#include "quickjs.h"

class JsProcessBinding {
public:
    static JSValue getExecFunction(JSContext* ctx);
};

