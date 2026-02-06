/// @file driver_proxy.h
/// @brief openDriver() 工厂函数，返回 Proxy 包装的 Driver 实例

#pragma once

#include <quickjs.h>

/// @brief 创建 openDriver() 工厂函数
///
/// 返回一个 JS 函数，调用时会创建 Driver 实例并用 ES6 Proxy 包装，
/// 使得 JS 端可以通过 `driver.commandName(params)` 的语法糖直接调用命令，
/// Proxy 的 get 拦截器会自动将属性访问转换为命令调用。
///
/// @param ctx QuickJS 上下文
/// @param driverCtor Driver 类的构造函数对象
/// @return openDriver 函数的 JSValue
JSValue createOpenDriverFunction(JSContext* ctx, JSValueConst driverCtor);
