#include "js_driver_resolve_binding.h"
#include "js_driver_resolve.h"
#include "js_constants.h"

#include <QString>
#include <cstring>

namespace stdiolink_service {

namespace {

JSValue js_resolveDriver(JSContext* ctx, JSValueConst /*this_val*/,
                         int argc, JSValueConst* argv)
{
    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx,
            "resolveDriver: driverName must be a non-empty string");

    const char* cname = JS_ToCString(ctx, argv[0]);
    if (!cname || strlen(cname) == 0) {
        JS_FreeCString(ctx, cname);
        return JS_ThrowTypeError(ctx,
            "resolveDriver: driverName must be a non-empty string");
    }

    QString driverName = QString::fromUtf8(cname);
    JS_FreeCString(ctx, cname);

    if (driverName.contains('/') || driverName.contains('\\'))
        return JS_ThrowTypeError(ctx,
            "resolveDriver: driverName must not contain path separators");
    if (driverName.endsWith(".exe", Qt::CaseInsensitive))
        return JS_ThrowTypeError(ctx,
            "resolveDriver: driverName must not end with .exe");

    auto& paths = JsConstantsBinding::getPathContext(ctx);
    auto result = resolveDriverPath(driverName, paths.dataRoot, paths.appDir);

    if (result.path.isEmpty()) {
        QString msg = QString("driver not found: \"%1\"\n  searched:")
            .arg(driverName);
        for (const auto& p : result.searchedPaths)
            msg += "\n    - " + p;
        return JS_ThrowInternalError(ctx, "%s",
            msg.toUtf8().constData());
    }

    return JS_NewString(ctx, result.path.toUtf8().constData());
}

int driverModuleInit(JSContext* ctx, JSModuleDef* module) {
    JSValue fn = JS_NewCFunction(ctx, js_resolveDriver, "resolveDriver", 1);
    return JS_SetModuleExport(ctx, module, "resolveDriver", fn);
}

} // namespace

void JsDriverResolveBinding::attachRuntime(JSRuntime* /*rt*/) {
    // No per-runtime state needed; uses JsConstantsBinding state
}

void JsDriverResolveBinding::detachRuntime(JSRuntime* /*rt*/) {
}

JSModuleDef* JsDriverResolveBinding::initModule(JSContext* ctx,
                                                 const char* name) {
    JSModuleDef* module = JS_NewCModule(ctx, name, driverModuleInit);
    if (!module) return nullptr;
    JS_AddModuleExport(ctx, module, "resolveDriver");
    return module;
}

} // namespace stdiolink_service
