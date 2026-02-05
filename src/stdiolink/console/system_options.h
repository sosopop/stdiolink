#pragma once

#include "stdiolink/stdiolink_export.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace stdiolink {

/**
 * 系统命令元数据 (M20)
 */
struct STDIOLINK_API SystemOptionMeta {
    QString longName;      // e.g. "export-doc"
    QString shortName;     // e.g. "D"
    QString valueHint;     // e.g. "<fmt>", "[=path]"
    QString description;   // 人类可读说明
    QStringList choices;   // 可选值（如 format）
    QString defaultValue;  // 默认值（若有）
    bool requiresValue;    // 是否需要值
};

/**
 * 系统命令注册中心 (M20)
 */
class STDIOLINK_API SystemOptionRegistry {
public:
    /**
     * 获取所有系统选项
     */
    static QVector<SystemOptionMeta> list();

    /**
     * 根据长名称查找选项
     */
    static const SystemOptionMeta* findLong(const QString& name);

    /**
     * 根据短名称查找选项
     */
    static const SystemOptionMeta* findShort(const QString& name);

    /**
     * 检查是否为框架参数（长名称）
     */
    static bool isFrameworkArg(const QString& longName);

    /**
     * 检查是否为框架参数（短名称）
     */
    static bool isFrameworkShortArg(const QString& shortName);

private:
    static const QVector<SystemOptionMeta>& options();
};

} // namespace stdiolink
