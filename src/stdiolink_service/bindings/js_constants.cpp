#include "js_constants.h"

#include <QHash>
#include <QSysInfo>
#include <quickjs.h>

#include "utils/js_freeze.h"

namespace stdiolink_service {

namespace {

struct ConstantsState {
    PathContext paths;
};

QHash<quintptr, ConstantsState> s_states;

quintptr runtimeKey(JSContext* ctx) {
    return reinterpret_cast<quintptr>(JS_GetRuntime(ctx));
}

ConstantsState& stateFor(JSContext* ctx) {
    return s_states[runtimeKey(ctx)];
}

JSValue buildSystemObject(JSContext* ctx) {
    JSValue sys = JS_NewObject(ctx);

#if defined(Q_OS_WIN)
    JS_SetPropertyStr(ctx, sys, "os", JS_NewString(ctx, "windows"));
    JS_SetPropertyStr(ctx, sys, "isWindows", JS_TRUE);
    JS_SetPropertyStr(ctx, sys, "isMac", JS_FALSE);
    JS_SetPropertyStr(ctx, sys, "isLinux", JS_FALSE);
#elif defined(Q_OS_MACOS)
    JS_SetPropertyStr(ctx, sys, "os", JS_NewString(ctx, "macos"));
    JS_SetPropertyStr(ctx, sys, "isWindows", JS_FALSE);
    JS_SetPropertyStr(ctx, sys, "isMac", JS_TRUE);
    JS_SetPropertyStr(ctx, sys, "isLinux", JS_FALSE);
#elif defined(Q_OS_LINUX)
    JS_SetPropertyStr(ctx, sys, "os", JS_NewString(ctx, "linux"));
    JS_SetPropertyStr(ctx, sys, "isWindows", JS_FALSE);
    JS_SetPropertyStr(ctx, sys, "isMac", JS_FALSE);
    JS_SetPropertyStr(ctx, sys, "isLinux", JS_TRUE);
#else
    JS_SetPropertyStr(ctx, sys, "os", JS_NewString(ctx, "unknown"));
    JS_SetPropertyStr(ctx, sys, "isWindows", JS_FALSE);
    JS_SetPropertyStr(ctx, sys, "isMac", JS_FALSE);
    JS_SetPropertyStr(ctx, sys, "isLinux", JS_FALSE);
#endif

    QByteArray arch = QSysInfo::currentCpuArchitecture().toUtf8();
    JS_SetPropertyStr(ctx, sys, "arch", JS_NewString(ctx, arch.constData()));

    return deepFreezeObject(ctx, sys);
}

JSValue buildAppPathsObject(JSContext* ctx) {
    auto& state = stateFor(ctx);
    JSValue paths = JS_NewObject(ctx);

    auto setStr = [&](const char* key, const QString& val) {
        JS_SetPropertyStr(ctx, paths, key,
                          JS_NewString(ctx, val.toUtf8().constData()));
    };

    setStr("appPath", state.paths.appPath);
    setStr("appDir", state.paths.appDir);
    setStr("cwd", state.paths.cwd);
    setStr("serviceDir", state.paths.serviceDir);
    setStr("serviceEntryPath", state.paths.serviceEntryPath);
    setStr("serviceEntryDir", state.paths.serviceEntryDir);
    setStr("tempDir", state.paths.tempDir);
    setStr("homeDir", state.paths.homeDir);
    setStr("dataRoot", state.paths.dataRoot);

    return deepFreezeObject(ctx, paths);
}

int constantsModuleInit(JSContext* ctx, JSModuleDef* module) {
    JSValue system = buildSystemObject(ctx);
    JSValue appPaths = buildAppPathsObject(ctx);

    if (JS_SetModuleExport(ctx, module, "SYSTEM", system) < 0) {
        JS_FreeValue(ctx, appPaths);
        return -1;
    }
    return JS_SetModuleExport(ctx, module, "APP_PATHS", appPaths);
}

} // namespace

void JsConstantsBinding::attachRuntime(JSRuntime* rt) {
    if (!rt) return;
    const quintptr key = reinterpret_cast<quintptr>(rt);
    if (!s_states.contains(key)) {
        s_states.insert(key, ConstantsState{});
    }
}

void JsConstantsBinding::detachRuntime(JSRuntime* rt) {
    if (!rt) return;
    s_states.remove(reinterpret_cast<quintptr>(rt));
}

void JsConstantsBinding::setPathContext(JSContext* ctx,
                                         const PathContext& paths) {
    stateFor(ctx).paths = paths;
}

const PathContext& JsConstantsBinding::getPathContext(JSContext* ctx) {
    return stateFor(ctx).paths;
}

JSModuleDef* JsConstantsBinding::initModule(JSContext* ctx,
                                             const char* name) {
    JSModuleDef* module = JS_NewCModule(ctx, name, constantsModuleInit);
    if (!module) return nullptr;
    JS_AddModuleExport(ctx, module, "SYSTEM");
    JS_AddModuleExport(ctx, module, "APP_PATHS");
    return module;
}

void JsConstantsBinding::reset(JSContext* ctx) {
    s_states[runtimeKey(ctx)] = ConstantsState{};
}

} // namespace stdiolink_service
