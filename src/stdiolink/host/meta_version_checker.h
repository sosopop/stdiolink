#pragma once

#include "stdiolink/stdiolink_export.h"

#include <QString>
#include <QStringList>

namespace stdiolink {

/**
 * 元数据版本检查器 (M18)
 * 用于检查 Host 与 Driver 的版本兼容性
 */
class STDIOLINK_API MetaVersionChecker {
public:
    /**
     * 检查版本兼容性
     * @param hostVersion Host 支持的最高版本
     * @param driverVersion Driver 的 schemaVersion
     * @return 是否兼容
     */
    static bool isCompatible(const QString& hostVersion, const QString& driverVersion);

    /**
     * 获取支持的版本列表
     */
    static QStringList getSupportedVersions();

    /**
     * 获取当前 Host 版本
     */
    static QString getCurrentVersion();

    /**
     * 解析版本号
     * @param version 版本字符串 (如 "1.0")
     * @param major 主版本号输出
     * @param minor 次版本号输出
     * @return 是否解析成功
     */
    static bool parseVersion(const QString& version, int& major, int& minor);
};

} // namespace stdiolink
