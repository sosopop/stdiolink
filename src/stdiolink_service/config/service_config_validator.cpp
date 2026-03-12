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

ValidationResult ServiceConfigValidator::mergeAndValidate(
    const ServiceConfigSchema& schema,
    const QJsonObject& fileConfig,
    const QJsonObject& cliConfig,
    UnknownFieldPolicy unknownFieldPolicy,
    QJsonObject& mergedOut) {

    // Merge: cli > file > defaults
    QJsonObject merged = deepMerge(fileConfig, cliConfig);

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

        if (field->type == FieldType::Array &&
            field->items &&
            !field->items->fields.isEmpty() &&
            it.value().isArray()) {
            ServiceConfigSchema itemSchema;
            itemSchema.fields = field->items->fields;
            const QJsonArray arr = it.value().toArray();
            for (int i = 0; i < arr.size(); ++i) {
                if (!arr.at(i).isObject()) {
                    continue;
                }
                const QString itemPath = QString("%1[%2]").arg(fullPath).arg(i);
                auto nestedResult = rejectUnknownFields(itemSchema,
                                                        arr.at(i).toObject(),
                                                        itemPath);
                if (!nestedResult.valid) {
                    return nestedResult;
                }
            }
        }
    }
    return ValidationResult::ok();
}

} // namespace stdiolink_service
