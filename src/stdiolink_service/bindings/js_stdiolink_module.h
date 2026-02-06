/// @file js_stdiolink_module.h
/// @brief stdiolink 内置 JS 模块的初始化入口

#pragma once

struct JSContext;
struct JSModuleDef;

/// @brief 初始化 stdiolink 内置模块
///
/// 作为 ModuleLoader 的内置模块初始化回调，注册 stdiolink 模块的所有导出项
/// （Driver 构造函数、openDriver 工厂函数、exec 函数等）。
/// JS 脚本通过 `import { Driver, openDriver } from "stdiolink"` 使用。
///
/// @param ctx QuickJS 上下文
/// @param name 模块名称（通常为 "stdiolink"）
/// @return 创建的模块定义，失败返回 nullptr
JSModuleDef* jsInitStdiolinkModule(JSContext* ctx, const char* name);

