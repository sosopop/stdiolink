/// @file console_bridge.h
/// @brief 将 JS console 对象桥接到 Qt 日志系统

#pragma once

struct JSContext;

/// @brief Console 桥接类
///
/// 在 JS 全局对象上注册 console.log / console.warn / console.error 等方法，
/// 将输出重定向到 Qt 的 qDebug / qWarning / qCritical，
/// 使 JS 脚本的日志能够统一通过 Qt 日志系统管理。
class ConsoleBridge {
public:
    /// @brief 在指定 JSContext 中安装 console 对象
    /// @param ctx QuickJS 上下文
    static void install(JSContext* ctx);
};

