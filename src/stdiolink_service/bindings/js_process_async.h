#pragma once

#include <quickjs.h>

namespace stdiolink_service {

/// @brief stdiolink/process 内置模块绑定
///
/// 提供异步进程执行 API（execAsync/spawn）。
/// 底层使用 QProcess，Promise/回调通过 Qt 信号桥接。
/// 绑定状态按 JSRuntime 隔离，支持在 runtime 销毁时统一清理。
class JsProcessAsyncBinding {
public:
    static void attachRuntime(JSRuntime* rt);
    static void detachRuntime(JSRuntime* rt);
    static JSModuleDef* initModule(JSContext* ctx, const char* name);
    static void reset(JSContext* ctx);
    static bool hasPending(JSContext* ctx);
};

} // namespace stdiolink_service
