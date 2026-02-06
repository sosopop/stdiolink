#pragma once

#include <quickjs.h>
#include <QJsonObject>
#include "config/service_config_schema.h"

namespace stdiolink_service {

class JsConfigBinding {
public:
    static void attachRuntime(JSRuntime* rt);
    static void detachRuntime(JSRuntime* rt);

    static JSValue getDefineConfigFunction(JSContext* ctx);
    static JSValue getGetConfigFunction(JSContext* ctx);

    static bool hasSchema(JSContext* ctx);
    static ServiceConfigSchema getSchema(JSContext* ctx);

    static void setRawConfig(JSContext* ctx,
                             const QJsonObject& rawCli,
                             const QJsonObject& file,
                             bool dumpSchemaMode);

    static bool isDumpSchemaMode(JSContext* ctx);
    static void markBlockedSideEffect(JSContext* ctx);
    static bool takeBlockedSideEffectFlag(JSContext* ctx);

    static void reset(JSContext* ctx);
};

} // namespace stdiolink_service
