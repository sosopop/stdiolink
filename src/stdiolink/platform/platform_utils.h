#pragma once

#include "stdiolink/stdiolink_export.h"

#include <QString>
#include <cstdio>

namespace stdiolink::PlatformUtils {

/**
 * Windows UTF-8 控制台初始化（SetConsoleOutputCP / SetConsoleCP）
 * 非 Windows 平台为空操作
 */
STDIOLINK_API void initConsoleEncoding();

/**
 * 检测文件流是否连接到交互式终端
 * 封装 isatty/fileno（Windows: _isatty/_fileno）
 */
STDIOLINK_API bool isInteractiveTerminal(FILE* stream);

/**
 * 返回当前平台的可执行文件后缀
 * Windows: ".exe"，其他: ""
 */
STDIOLINK_API QString executableSuffix();

/**
 * 拼接平台对应的可执行文件完整路径
 * @param dir 目录路径
 * @param baseName 可执行文件基础名（不含后缀）
 * @return dir/baseName[.exe]
 */
STDIOLINK_API QString executablePath(const QString& dir, const QString& baseName);

/**
 * 返回用于目录扫描的可执行文件过滤器
 * Windows: "*.exe"，其他: "*"
 */
STDIOLINK_API QString executableFilter();

/**
 * Driver 可执行文件名前缀常量
 */
STDIOLINK_API QString driverExecutablePrefix();

/**
 * 判断 basename（不含扩展名）是否符合 Driver 命名规范
 */
STDIOLINK_API bool isDriverExecutableName(const QString& baseName);

} // namespace stdiolink::PlatformUtils
