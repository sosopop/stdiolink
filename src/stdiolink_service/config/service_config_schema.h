#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include "stdiolink/protocol/meta_types.h"

namespace stdiolink_service {

struct ServiceConfigSchema {
    QVector<stdiolink::meta::FieldMeta> fields;

    /// 从字段描述对象解析 schema
    /// key = 字段名, value = 描述对象 {type, required, default, description, constraints, items}
    static ServiceConfigSchema fromJsObject(const QJsonObject& obj);

    /// 从 config.schema.json 文件加载 schema
    static ServiceConfigSchema fromJsonFile(const QString& filePath, QString& error);

    /// 从 JSON 对象解析 schema，带错误检查
    static ServiceConfigSchema fromJsonObject(const QJsonObject& obj,
                                               QString& error);

    /// 导出为 JSON（用于 --dump-config-schema）
    QJsonObject toJson() const;

    /// 导出为 FieldMeta 数组格式（与 DriverMeta.params 统一）
    QJsonArray toFieldMetaArray() const;

    /// 根据 schema 默认值生成默认配置
    QJsonObject generateDefaults() const;

    /// 获取必填字段名列表
    QStringList requiredFieldNames() const;

    /// 获取可选字段名列表
    QStringList optionalFieldNames() const;

    /// 按名称查找字段，未找到返回 nullptr
    const stdiolink::meta::FieldMeta* findField(const QString& name) const;
};

} // namespace stdiolink_service
