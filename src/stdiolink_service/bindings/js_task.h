/// @file js_task.h
/// @brief Task 类的 JS 绑定，实现 stdiolink::Task 与 JSValue 的双向转换

#pragma once

#include <quickjs.h>
#include "stdiolink/host/task.h"

/// @brief Task 的 JS 绑定类
///
/// 将 C++ 端的 stdiolink::Task 注册为 JS 类，支持在 JS 和 C++ 之间
/// 双向转换 Task 对象，用于异步命令执行的结果传递。
class JsTaskBinding {
public:
    /// @brief 在 JSContext 中注册 Task 类原型和方法
    /// @param ctx QuickJS 上下文
    static void registerClass(JSContext* ctx);

    /// @brief 从 C++ Task 对象创建对应的 JS Task 实例
    /// @param ctx QuickJS 上下文
    /// @param task 源 C++ Task 对象
    /// @return 新创建的 JS Task 对象
    static JSValue createFromTask(JSContext* ctx, const stdiolink::Task& task);

    /// @brief 将 JS Task 对象转换回 C++ Task
    /// @param ctx QuickJS 上下文
    /// @param value JS 端的 Task 对象
    /// @param outTask 输出参数，转换后的 C++ Task
    /// @return 转换成功返回 true，失败返回 false
    static bool toTask(JSContext* ctx, JSValueConst value, stdiolink::Task& outTask);

    /// @brief 分离运行时，清理类 ID 关联的资源
    /// @param rt QuickJS 运行时
    /// @note 应在 JSRuntime 销毁前调用
    static void detachRuntime(JSRuntime* rt);
};
