#include "service_config_validator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include "stdiolink/protocol/meta_types.h"

using stdiolink::meta::DefaultFiller;
using stdiolink::meta::FieldMeta;
using stdiolink::meta::FieldType;
using stdiolink::meta::MetaValidator;
using stdiolink::meta::ValidationResult;

namespace stdiolink_service {

namespace {

QJsonValue parseAnyJsonLiteral(const QString& raw) {
    QJsonParseError err;
    QByteArray wrapped = raw.toUtf8();
    wrapped.prepend('[');
    wrapped.append(']');
    QJsonDocument doc = QJsonDocument::fromJson(wrapped, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        return QJsonValue(QJsonValue::Undefined);
    }
    QJsonArray arr = doc.array();
    if (arr.size() != 1) {
        return QJsonValue(QJsonValue::Undefined);
    }
    return arr.at(0);
}

QJsonValue convertSingleRawValue(const QString& raw, FieldType type) {
    switch (type) {
    case FieldType::Bool:
        if (raw == "true") return QJsonValue(true);
        if (raw == "false") return QJsonValue(false);
        return QJsonValue(QJsonValue::Undefined);

    case FieldType::Int: {
        bool ok = false;
        const int v = raw.toInt(&ok);
        if (ok) return QJsonValue(v);
        return QJsonValue(QJsonValue::Undefined);
    }

    case FieldType::Int64: {
        bool ok = false;
        const qint64 v = raw.toLongLong(&ok);
        if (ok) return QJsonValue(static_cast<double>(v));
        return QJsonValue(QJsonValue::Undefined);
    }

    case FieldType::Double: {
        bool ok = false;
        const double v = raw.toDouble(&ok);
        if (ok) return QJsonValue(v);
        return QJsonValue(QJsonValue::Undefined);
    }

    case FieldType::String:
    case FieldType::Enum:
        return QJsonValue(raw);

    case FieldType::Array:
    case FieldType::Object:
    case FieldType::Any: {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError) {
            if (doc.isArray()) return QJsonValue(doc.array());
            if (doc.isObject()) return QJsonValue(doc.object());
        }
        if (type == FieldType::Any) {
            QJsonValue literal = parseAnyJsonLiteral(raw);
            if (!literal.isUndefined()) {
                return literal;
            }
            return QJsonValue(raw);
        }
        return QJsonValue(QJsonValue::Undefined);
    }

    default:
        return QJsonValue(raw);
    }
}

} // namespace

QJsonObject ServiceConfigValidator::deepMerge(const QJsonObject& base,
                                               const QJsonObject& override) {
    QJsonObject result = base;
    for (auto it = override.begin(); it != override.end(); ++it) {
        if (it.value().isObject() && result.contains(it.key())
            && result.value(it.key()).isObject()) {
            result[it.key()] = deepMerge(result.value(it.key()).toObject(),
                                         it.value().toObject());
        } else {
            result[it.key()] = it.value();
        }
    }
    return result;
}

QJsonObject ServiceConfigValidator::convertRawValues(const ServiceConfigSchema& schema,
                                                      const QJsonObject& raw) {
    QJsonObject result;
    for (auto it = raw.begin(); it != raw.end(); ++it) {
        const FieldMeta* field = schema.findField(it.key());
        if (!field) {
            // Unknown field: pass through as-is
            result[it.key()] = it.value();
            continue;
        }

        if (it.value().isString()) {
            QJsonValue converted = convertSingleRawValue(it.value().toString(), field->type);
            if (converted.isUndefined()) {
                // Conversion failed, keep raw string (validation will catch it)
                result[it.key()] = it.value();
            } else {
                result[it.key()] = converted;
            }
        } else if (it.value().isObject() && field->type == FieldType::Object) {
            // Recurse into nested objects if schema has nested fields
            if (!field->fields.isEmpty()) {
                ServiceConfigSchema nested;
                nested.fields = field->fields;
                result[it.key()] = convertRawValues(nested, it.value().toObject());
            } else {
                result[it.key()] = it.value();
            }
        } else {
            result[it.key()] = it.value();
        }
    }
    return result;
}

ValidationResult ServiceConfigValidator::mergeAndValidate(
    const ServiceConfigSchema& schema,
    const QJsonObject& fileConfig,
    const QJsonObject& rawCliConfig,
    UnknownFieldPolicy unknownFieldPolicy,
    QJsonObject& mergedOut) {

    // Convert raw CLI string values to typed values based on schema
    QJsonObject typedCliConfig = convertRawValues(schema, rawCliConfig);

    // Merge: cli > file > defaults
    QJsonObject merged = deepMerge(fileConfig, typedCliConfig);

    // Fill defaults from schema
    merged = fillDefaults(schema, merged);

    // Check unknown fields
    if (unknownFieldPolicy == UnknownFieldPolicy::Reject) {
        auto unknownResult = rejectUnknownFields(schema, merged);
        if (!unknownResult.valid) {
            return unknownResult;
        }
    }

    // Validate against schema
    auto vr = validate(schema, merged);
    if (!vr.valid) {
        return vr;
    }

    mergedOut = merged;
    return ValidationResult::ok();
}

ValidationResult ServiceConfigValidator::validate(
    const ServiceConfigSchema& schema,
    const QJsonObject& config) {

    // Check required fields
    for (const auto& field : schema.fields) {
        if (field.required && !config.contains(field.name)) {
            return ValidationResult::fail(
                field.name,
                QString("required field '%1' is missing").arg(field.name));
        }
    }

    // Validate each present field
    for (const auto& field : schema.fields) {
        if (!config.contains(field.name)) {
            continue;
        }
        auto vr = MetaValidator::validateField(config.value(field.name), field);
        if (!vr.valid) {
            if (vr.errorField.isEmpty()) {
                vr.errorField = field.name;
            }
            return vr;
        }
    }

    return ValidationResult::ok();
}

QJsonObject ServiceConfigValidator::fillDefaults(
    const ServiceConfigSchema& schema,
    const QJsonObject& config) {
    return DefaultFiller::fillDefaults(config, schema.fields);
}

ValidationResult ServiceConfigValidator::rejectUnknownFields(
    const ServiceConfigSchema& schema,
    const QJsonObject& config,
    const QString& prefix) {
    for (auto it = config.begin(); it != config.end(); ++it) {
        const FieldMeta* field = schema.findField(it.key());
        const QString fullPath = prefix.isEmpty() ? it.key() : (prefix + "." + it.key());
        if (!field) {
            return ValidationResult::fail(fullPath, "unknown configuration field");
        }

        if (field->type == FieldType::Object &&
            !field->fields.isEmpty() &&
            it.value().isObject()) {
            ServiceConfigSchema nested;
            nested.fields = field->fields;
            auto nestedResult = rejectUnknownFields(nested, it.value().toObject(), fullPath);
            if (!nestedResult.valid) {
                return nestedResult;
            }
        }
    }
    return ValidationResult::ok();
}

} // namespace stdiolink_service
