#pragma once

#include <quickjs.h>

namespace stdiolink_service {

/// @brief 递归深冻结 JS 对象（含嵌套对象和数组）
/// @param ctx QuickJS 上下文
/// @param obj 待冻结的对象（就地冻结，返回同一引用）
/// @return 冻结后的对象（与输入为同一引用）
JSValue deepFreezeObject(JSContext* ctx, JSValue obj);

} // namespace stdiolink_service
