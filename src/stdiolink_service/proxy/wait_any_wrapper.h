/// @file wait_any_wrapper.h
/// @brief waitAny() JS 包装函数工厂

#pragma once

#include <quickjs.h>

/// @brief 创建 waitAny() 包装函数
///
/// 返回一个 JS 函数，对入参做基础校验后调用全局 __waitAny。
JSValue createWaitAnyFunction(JSContext* ctx);
