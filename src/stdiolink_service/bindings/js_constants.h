#pragma once

#include <quickjs.h>
#include <QString>

namespace stdiolink_service {

/// @brief 路径上下文，由 main.cpp 在解析 ServiceDirectory 后注入
struct PathContext {
    QString appPath;
    QString appDir;
    QString cwd;
    QString serviceDir;
    QString serviceEntryPath;
    QString serviceEntryDir;
    QString tempDir;
    QString homeDir;
};

/// @brief stdiolink/constants 内置模块绑定
///
/// 提供 SYSTEM 和 APP_PATHS 两个只读常量对象。
/// 绑定状态按 JSRuntime 维度隔离，与 JsConfigBinding 模式一致。
class JsConstantsBinding {
public:
    static void attachRuntime(JSRuntime* rt);
    static void detachRuntime(JSRuntime* rt);

    /// 注入路径上下文（main.cpp 解析完 ServiceDirectory 后调用）
    static void setPathContext(JSContext* ctx, const PathContext& paths);

    /// 模块初始化回调（注册给 ModuleLoader）
    static JSModuleDef* initModule(JSContext* ctx, const char* name);

    /// 重置状态（测试用）
    static void reset(JSContext* ctx);
};

} // namespace stdiolink_service
