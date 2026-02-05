#pragma once

#include "stdiolink/protocol/meta_types.h"
#include "stdiolink/stdiolink_export.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace stdiolink {

/**
 * 配置注入器 (M17)
 * 将配置转换为不同的注入格式
 */
class STDIOLINK_API ConfigInjector {
public:
    /**
     * 转换为环境变量
     * @param config 配置对象
     * @param apply 注入策略
     * @return 环境变量映射 (key -> value)
     */
    static QHash<QString, QString> toEnvVars(const QJsonObject& config,
                                              const meta::ConfigApply& apply);

    /**
     * 转换为启动参数
     * @param config 配置对象
     * @param apply 注入策略
     * @return 参数列表 (--key=value)
     */
    static QStringList toArgs(const QJsonObject& config,
                              const meta::ConfigApply& apply);

    /**
     * 写入配置文件
     * @param config 配置对象
     * @param path 文件路径
     * @return 是否成功
     */
    static bool toFile(const QJsonObject& config, const QString& path);

    /**
     * 从文件读取配置
     * @param path 文件路径
     * @param config 输出配置对象
     * @return 是否成功
     */
    static bool fromFile(const QString& path, QJsonObject& config);

private:
    static QString valueToString(const QJsonValue& value);
    static QString keyToEnvName(const QString& key, const QString& prefix);
};

} // namespace stdiolink
