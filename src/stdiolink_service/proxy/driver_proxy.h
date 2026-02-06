#pragma once

#include <quickjs.h>

JSValue createOpenDriverFunction(JSContext* ctx, JSValueConst driverCtor);
