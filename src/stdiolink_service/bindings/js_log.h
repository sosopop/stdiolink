#pragma once

#include <quickjs.h>

namespace stdiolink_service {

/// @brief stdiolink/log 内置模块绑定
///
/// 提供结构化日志 API（createLogger → Logger 实例）。
/// Logger 对象通过 QuickJS class 机制实现，支持 child 继承链。
/// 日志输出为 JSON line 格式，映射到 Qt 日志通道。
/// 无需 runtime 级状态隔离（Logger 自身持有 baseFields）。
class JsLogBinding {
public:
    /// 模块初始化回调（注册给 ModuleLoader）
    static JSModuleDef* initModule(JSContext* ctx, const char* name);

    /// 注册 Logger class（在 initModule 内部调用）
    static void registerLoggerClass(JSContext* ctx);
};

} // namespace stdiolink_service
