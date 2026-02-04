#pragma once

#include "meta_types.h"

#include <QRegularExpression>

namespace stdiolink::meta {

/**
 * 验证结果
 */
struct ValidationResult {
    bool valid = true;
    QString errorField;
    QString errorMessage;
    int errorCode = 0;

    static ValidationResult ok() { return {true, {}, {}, 0}; }

    static ValidationResult fail(const QString& field, const QString& msg, int code = 400) {
        return {false, field, msg, code};
    }

    QString toString() const {
        if (valid)
            return "OK";
        if (errorField.isEmpty())
            return errorMessage;
        return QString("%1: %2").arg(errorField, errorMessage);
    }
};

/**
 * 元数据验证器
 */
class MetaValidator {
public:
    /**
     * 验证命令参数
     */
    static ValidationResult validateParams(const QJsonValue& data,
                                           const CommandMeta& cmd,
                                           bool allowUnknown = true);

    /**
     * 验证单个字段
     */
    static ValidationResult validateField(const QJsonValue& value, const FieldMeta& field);

    /**
     * 验证配置
     */
    static ValidationResult validateConfig(const QJsonObject& config, const ConfigSchema& schema);

private:
    static ValidationResult checkType(const QJsonValue& value, FieldType type);

    static ValidationResult checkConstraints(const QJsonValue& value, const FieldMeta& field);

    static ValidationResult validateObject(const QJsonObject& obj,
                                           const QVector<FieldMeta>& fields,
                                           const QStringList& requiredKeys,
                                           bool allowUnknown);

    static ValidationResult validateArray(const QJsonArray& arr, const FieldMeta& field);
};

/**
 * 默认值填充器
 */
class DefaultFiller {
public:
    static QJsonObject fillDefaults(const QJsonObject& data, const QVector<FieldMeta>& fields);

    static QJsonObject fillDefaults(const QJsonObject& data, const CommandMeta& cmd);
};

} // namespace stdiolink::meta
