#pragma once

#include <quickjs.h>

namespace stdiolink_service {

/// @brief stdiolink/time 内置模块绑定
///
/// 提供时间获取与非阻塞 sleep。sleep 通过 QTimer::singleShot
/// 桥接到 QuickJS Promise，不阻塞 JS 事件循环。
/// 绑定状态按 JSRuntime 维度隔离，析构时安全清理 pending sleep。
class JsTimeBinding {
public:
    static void attachRuntime(JSRuntime* rt);
    static void detachRuntime(JSRuntime* rt);
    static JSModuleDef* initModule(JSContext* ctx, const char* name);
    static void reset(JSContext* ctx);
    static bool hasPending(JSContext* ctx);
};

} // namespace stdiolink_service
