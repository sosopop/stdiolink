#pragma once

#include <quickjs.h>

namespace stdiolink_service {

/// @brief stdiolink/fs 内置模块绑定
///
/// 提供同步文件系统操作 API。所有函数均为同步调用，
/// IO 错误抛出 InternalError（含路径信息），类型错误抛出 TypeError。
/// 底层使用 QFile/QDir/QFileInfo/QJsonDocument 实现。
class JsFsBinding {
public:
    /// 模块初始化回调（注册给 ModuleLoader）
    static JSModuleDef* initModule(JSContext* ctx, const char* name);
};

} // namespace stdiolink_service
