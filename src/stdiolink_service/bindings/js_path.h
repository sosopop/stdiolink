#pragma once

#include <quickjs.h>

namespace stdiolink_service {

/// @brief stdiolink/path 内置模块绑定
///
/// 提供纯函数式路径操作 API，无状态，无需 runtime 隔离。
/// 底层使用 QDir/QFileInfo 实现，输出路径统一使用 '/' 分隔。
class JsPathBinding {
public:
    /// 模块初始化回调（注册给 ModuleLoader）
    static JSModuleDef* initModule(JSContext* ctx, const char* name);
};

} // namespace stdiolink_service
