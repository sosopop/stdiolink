#pragma once

#include <QString>
#include "service_config_schema.h"

namespace stdiolink_service {

class ServiceConfigHelp {
public:
    /// 从 schema 生成配置项帮助文本
    static QString generate(const ServiceConfigSchema& schema);

private:
    /// 格式化单个字段（支持 object 类型递归展示）
    static QString formatField(const stdiolink::meta::FieldMeta& field,
                               const QString& prefix);
    /// 格式化约束信息
    static QString formatConstraints(const stdiolink::meta::Constraints& c);
    /// 类型名转字符串
    static QString fieldTypeToString(stdiolink::meta::FieldType type);
};

} // namespace stdiolink_service
