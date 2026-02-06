/// @file js_process.h
/// @brief exec() 函数的 JS 绑定，用于在 JS 中执行外部进程

#pragma once

#include <quickjs.h>

/// @brief 进程执行的 JS 绑定类
///
/// 提供 exec() 函数供 JS 脚本调用，用于同步执行外部进程并获取输出结果。
class JsProcessBinding {
public:
    /// @brief 获取 exec() 函数的 JSValue
    /// @param ctx QuickJS 上下文
    /// @return exec 函数对象，可直接导出到 JS 模块
    static JSValue getExecFunction(JSContext* ctx);
};
