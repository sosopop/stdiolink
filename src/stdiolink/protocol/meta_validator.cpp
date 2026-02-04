#include "meta_validator.h"

namespace stdiolink::meta {

// MetaValidator 实现

ValidationResult MetaValidator::checkType(const QJsonValue& value, FieldType type) {
    switch (type) {
    case FieldType::String:
        if (!value.isString())
            return ValidationResult::fail("", "expected string");
        break;
    case FieldType::Int:
        if (!value.isDouble())
            return ValidationResult::fail("", "expected integer");
        {
            double d = value.toDouble();
            if (d != static_cast<int>(d))
                return ValidationResult::fail("", "expected integer, got decimal");
        }
        break;
    case FieldType::Int64:
        if (!value.isDouble())
            return ValidationResult::fail("", "expected integer");
        {
            double d = value.toDouble();
            // 检查安全整数范围 (2^53)
            if (d < -9007199254740992.0 || d > 9007199254740992.0)
                return ValidationResult::fail("", "integer out of safe range");
        }
        break;
    case FieldType::Double:
        if (!value.isDouble())
            return ValidationResult::fail("", "expected number");
        break;
    case FieldType::Bool:
        if (!value.isBool())
            return ValidationResult::fail("", "expected boolean");
        break;
    case FieldType::Object:
        if (!value.isObject())
            return ValidationResult::fail("", "expected object");
        break;
    case FieldType::Array:
        if (!value.isArray())
            return ValidationResult::fail("", "expected array");
        break;
    case FieldType::Enum:
        if (!value.isString())
            return ValidationResult::fail("", "expected string for enum");
        break;
    case FieldType::Any:
        break;
    }
    return ValidationResult::ok();
}

ValidationResult MetaValidator::checkConstraints(const QJsonValue& value, const FieldMeta& field) {
    const auto& c = field.constraints;

    // 数值范围检查
    if (c.min.has_value() && value.isDouble()) {
        if (value.toDouble() < c.min.value())
            return ValidationResult::fail(
                field.name, QString("value %1 < min %2").arg(value.toDouble()).arg(*c.min));
    }
    if (c.max.has_value() && value.isDouble()) {
        if (value.toDouble() > c.max.value())
            return ValidationResult::fail(
                field.name, QString("value %1 > max %2").arg(value.toDouble()).arg(*c.max));
    }

    // 字符串长度检查
    if (value.isString()) {
        int len = value.toString().length();
        if (c.minLength.has_value() && len < *c.minLength)
            return ValidationResult::fail(field.name, "string too short");
        if (c.maxLength.has_value() && len > *c.maxLength)
            return ValidationResult::fail(field.name, "string too long");
        if (!c.pattern.isEmpty()) {
            QRegularExpression re(c.pattern);
            if (!re.match(value.toString()).hasMatch())
                return ValidationResult::fail(field.name, "pattern mismatch");
        }
    }

    // 枚举值检查
    if (field.type == FieldType::Enum && !c.enumValues.isEmpty()) {
        if (!c.enumValues.contains(value))
            return ValidationResult::fail(field.name, "invalid enum value");
    }

    // 数组长度检查
    if (value.isArray()) {
        int size = value.toArray().size();
        if (c.minItems.has_value() && size < *c.minItems)
            return ValidationResult::fail(field.name, "array too short");
        if (c.maxItems.has_value() && size > *c.maxItems)
            return ValidationResult::fail(field.name, "array too long");
    }

    return ValidationResult::ok();
}

ValidationResult MetaValidator::validateField(const QJsonValue& value, const FieldMeta& field) {
    // 类型检查
    auto typeResult = checkType(value, field.type);
    if (!typeResult.valid) {
        typeResult.errorField = field.name;
        return typeResult;
    }

    // 约束检查
    auto constraintResult = checkConstraints(value, field);
    if (!constraintResult.valid) {
        return constraintResult;
    }

    // 嵌套 Object 验证
    if (field.type == FieldType::Object && !field.fields.isEmpty()) {
        auto objResult = validateObject(value.toObject(), field.fields, field.requiredKeys, true);
        if (!objResult.valid) {
            objResult.errorField = field.name + "." + objResult.errorField;
            return objResult;
        }
    }

    // Array 元素验证
    if (field.type == FieldType::Array && field.items) {
        auto arrResult = validateArray(value.toArray(), field);
        if (!arrResult.valid) {
            return arrResult;
        }
    }

    return ValidationResult::ok();
}

ValidationResult MetaValidator::validateObject(const QJsonObject& obj,
                                               const QVector<FieldMeta>& fields,
                                               const QStringList& requiredKeys,
                                               bool allowUnknown) {
    // 检查必填字段
    for (const auto& field : fields) {
        if (field.required && !obj.contains(field.name)) {
            return ValidationResult::fail(field.name, "required field missing");
        }
    }

    // 检查 requiredKeys
    for (const auto& key : requiredKeys) {
        if (!obj.contains(key)) {
            return ValidationResult::fail(key, "required key missing");
        }
    }

    // 验证每个字段
    for (const auto& field : fields) {
        if (obj.contains(field.name)) {
            auto result = validateField(obj[field.name], field);
            if (!result.valid) {
                return result;
            }
        }
    }

    // 检查未知字段
    if (!allowUnknown) {
        QSet<QString> knownFields;
        for (const auto& f : fields) {
            knownFields.insert(f.name);
        }
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (!knownFields.contains(it.key())) {
                return ValidationResult::fail(it.key(), "unknown field");
            }
        }
    }

    return ValidationResult::ok();
}

ValidationResult MetaValidator::validateArray(const QJsonArray& arr, const FieldMeta& field) {
    if (!field.items) {
        return ValidationResult::ok();
    }

    for (int i = 0; i < arr.size(); ++i) {
        auto result = validateField(arr[i], *field.items);
        if (!result.valid) {
            result.errorField = QString("%1[%2]").arg(field.name).arg(i);
            return result;
        }
    }

    return ValidationResult::ok();
}

ValidationResult MetaValidator::validateParams(const QJsonValue& data,
                                               const CommandMeta& cmd,
                                               bool allowUnknown) {
    if (!data.isObject() && !data.isNull() && !data.isUndefined()) {
        return ValidationResult::fail("", "params must be an object");
    }

    QJsonObject obj = data.toObject();
    return validateObject(obj, cmd.params, {}, allowUnknown);
}

ValidationResult MetaValidator::validateConfig(const QJsonObject& config,
                                               const ConfigSchema& schema) {
    return validateObject(config, schema.fields, {}, true);
}

// DefaultFiller 实现

QJsonObject DefaultFiller::fillDefaults(const QJsonObject& data,
                                        const QVector<FieldMeta>& fields) {
    QJsonObject result = data;

    for (const auto& field : fields) {
        if (!result.contains(field.name) && !field.defaultValue.isNull() &&
            !field.defaultValue.isUndefined()) {
            result[field.name] = field.defaultValue;
        }
    }

    return result;
}

QJsonObject DefaultFiller::fillDefaults(const QJsonObject& data, const CommandMeta& cmd) {
    return fillDefaults(data, cmd.params);
}

} // namespace stdiolink::meta
