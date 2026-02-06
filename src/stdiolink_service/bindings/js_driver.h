/// @file js_driver.h
/// @brief Driver 类的 JS 绑定，将 stdiolink::Driver 暴露给 JS 层

#pragma once

#include <quickjs.h>

/// @brief Driver 的 JS 绑定类
///
/// 将 C++ 端的 stdiolink::Driver 注册为 JS 可构造的类，
/// 使 JS 脚本能够创建和操作 Driver 实例（注册命令、发送响应等）。
class JsDriverBinding {
public:
    /// @brief 在 JSContext 中注册 Driver 类原型和方法
    /// @param ctx QuickJS 上下文
    static void registerClass(JSContext* ctx);

    /// @brief 获取 Driver 类的构造函数对象
    /// @param ctx QuickJS 上下文
    /// @return Driver 构造函数的 JSValue
    static JSValue getConstructor(JSContext* ctx);

    /// @brief 分离运行时，清理类 ID 关联的资源
    /// @param rt QuickJS 运行时
    /// @note 应在 JSRuntime 销毁前调用
    static void detachRuntime(JSRuntime* rt);
};
