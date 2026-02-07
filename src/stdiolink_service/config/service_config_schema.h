#pragma once

#include <QJsonObject>
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

    /// 导出为 JSON（用于 --dump-config-schema）
    QJsonObject toJson() const;

    /// 按名称查找字段，未找到返回 nullptr
    const stdiolink::meta::FieldMeta* findField(const QString& name) const;
};

} // namespace stdiolink_service
