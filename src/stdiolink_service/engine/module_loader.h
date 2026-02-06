/// @file module_loader.h
/// @brief ES Module 加载器，支持内置模块和文件模块的解析与加载

#pragma once

#include <QString>

struct JSContext;
struct JSModuleDef;

/// @brief ES Module 加载器
///
/// 为 QuickJS 提供模块解析（normalize）和加载（loader）回调。
/// 支持两种模块来源：通过 addBuiltin() 注册的内置模块，以及从文件系统加载的 .js/.mjs 文件。
class ModuleLoader {
public:
    /// @brief 安装模块加载器到指定 JSContext
    /// @param ctx QuickJS 上下文
    static void install(JSContext* ctx);

    /// @brief 注册内置模块
    /// @param name 模块名称（import 时使用的标识符）
    /// @param init 模块初始化函数指针
    static void addBuiltin(const QString& name, JSModuleDef* (*init)(JSContext*, const char*));

    // 模块解析规则（设计固定）：
    // 1) 内置模块按精确名称匹配。
    // 2) 非内置模块必须使用相对或绝对文件路径。
    // 3) 路径必须包含显式的 .js/.mjs 扩展名。
    // 4) 不进行扩展名探测，不支持目录 index 回退。

private:
    /// @brief 模块名称规范化回调
    /// @param ctx QuickJS 上下文
    /// @param baseName 当前模块的基础路径
    /// @param name import 语句中的模块标识符
    /// @param opaque 用户数据（未使用）
    /// @return 规范化后的模块名称（由 js_malloc 分配），失败返回 nullptr
    static char* normalize(JSContext* ctx,
                           const char* baseName,
                           const char* name,
                           void* opaque);

    /// @brief 模块加载回调
    /// @param ctx QuickJS 上下文
    /// @param moduleName 规范化后的模块名称
    /// @param opaque 用户数据（未使用）
    /// @return 加载成功的模块定义，失败返回 nullptr
    static JSModuleDef* loader(JSContext* ctx,
                               const char* moduleName,
                               void* opaque);
};
