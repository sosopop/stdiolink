#pragma once

#include <quickjs.h>
#include <QJsonObject>

namespace stdiolink_service {

class JsConfigBinding {
public:
    static void attachRuntime(JSRuntime* rt);
    static void detachRuntime(JSRuntime* rt);

    /// 获取 getConfig() 函数对象
    static JSValue getGetConfigFunction(JSContext* ctx);

    /// 注入已合并校验的最终配置（C++ 侧调用）
    static void setMergedConfig(JSContext* ctx, const QJsonObject& mergedConfig);

    /// 重置状态（测试用）
    static void reset(JSContext* ctx);
};

} // namespace stdiolink_service
