#pragma once

#include <quickjs.h>

namespace stdiolink_service {

/// @brief stdiolink/http 内置模块绑定
///
/// 提供异步 HTTP 客户端 API（request/get/post）。
/// 底层使用 QNetworkAccessManager，Promise 通过 QNetworkReply::finished
/// 信号桥接。绑定状态按 JSRuntime 维度隔离。
class JsHttpBinding {
public:
    static void attachRuntime(JSRuntime* rt);
    static void detachRuntime(JSRuntime* rt);
    static JSModuleDef* initModule(JSContext* ctx, const char* name);
    static void reset(JSContext* ctx);
    static bool hasPending(JSContext* ctx);
};

} // namespace stdiolink_service
