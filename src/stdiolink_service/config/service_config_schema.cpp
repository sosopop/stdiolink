#include "service_config_schema.h"

#include <QFile>
#include <QJsonDocument>
#include <QSet>
#include "stdiolink/protocol/meta_types.h"

using stdiolink::meta::Constraints;
using stdiolink::meta::FieldMeta;
using stdiolink::meta::FieldType;
using stdiolink::meta::fieldTypeFromString;

namespace stdiolink_service {

namespace {

/// 已知的合法类型字符串集合
bool isKnownFieldType(const QString& typeStr) {
    static const QSet<QString> known = {"string", "int",     "integer", "int64", "double", "number",
                                        "bool",   "boolean", "object",  "array", "enum",   "any"};
    return known.contains(typeStr);
}

/// 带错误检查的递归解析（内部实现）
ServiceConfigSchema parseObject(const QJsonObject& obj, const QString& pathPrefix, QString& error) {
    ServiceConfigSchema schema;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString& fieldName = it.key();
        const QString fieldPath = pathPrefix.isEmpty() ? fieldName : (pathPrefix + "." + fieldName);
        if (!it.value().isObject()) {
            error = QString("field descriptor for \"%1\" must be a JSON object").arg(fieldPath);
            return {};
        }
        const QJsonObject desc = it.value().toObject();

        // Validate type string
        const QString typeStr = desc.value("type").toString("any");
        if (!isKnownFieldType(typeStr)) {
            error = QString("unknown field type \"%1\" for field \"%2\"").arg(typeStr, fieldPath);
            return {};
        }

        FieldMeta field;
        field.name = fieldName;
        field.type = fieldTypeFromString(typeStr);
        field.required = desc.value("required").toBool(false);
        field.description = desc.value("description").toString();

        if (desc.contains("default")) {
            field.defaultValue = desc.value("default");
        }

        if (desc.contains("constraints")) {
            QJsonObject cObj = desc.value("constraints").toObject();
            if (cObj.contains("enumValues")) {
                cObj["enum"] = cObj.take("enumValues");
            }
            field.constraints = Constraints::fromJson(cObj);
        }

        if (desc.contains("items")) {
            if (!desc.value("items").isObject()) {
                error = QString("\"items\" for field \"%1\" must be a JSON object").arg(fieldPath);
                return {};
            }
            auto itemMeta = std::make_shared<FieldMeta>();
            const QJsonObject itemObj = desc.value("items").toObject();
            const QString itemTypeStr = itemObj.value("type").toString("any");
            if (!isKnownFieldType(itemTypeStr)) {
                error = QString("unknown item type \"%1\" for field \"%2\"")
                            .arg(itemTypeStr, fieldPath);
                return {};
            }
            itemMeta->type = fieldTypeFromString(itemTypeStr);
            if (itemObj.contains("constraints")) {
                itemMeta->constraints =
                    Constraints::fromJson(itemObj.value("constraints").toObject());
            }
            field.items = itemMeta;
        }

        if (desc.contains("fields")) {
            if (!desc.value("fields").isObject()) {
                error = QString("\"fields\" for field \"%1\" must be a JSON object").arg(fieldPath);
                return {};
            }
            const QJsonObject fieldsObj = desc.value("fields").toObject();
            ServiceConfigSchema nested = parseObject(fieldsObj, fieldPath, error);
            if (!error.isEmpty()) {
                return {};
            }
            field.fields = nested.fields;
        }

        schema.fields.append(field);
    }
    return schema;
}

} // namespace

ServiceConfigSchema ServiceConfigSchema::fromJsObject(const QJsonObject& obj) {
    ServiceConfigSchema schema;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString& fieldName = it.key();
        const QJsonObject desc = it.value().toObject();

        FieldMeta field;
        field.name = fieldName;
        field.type = fieldTypeFromString(desc.value("type").toString("any"));
        field.required = desc.value("required").toBool(false);
        field.description = desc.value("description").toString();

        if (desc.contains("default")) {
            field.defaultValue = desc.value("default");
        }

        if (desc.contains("constraints")) {
            QJsonObject cObj = desc.value("constraints").toObject();
            // JS API uses "enumValues" but Constraints::fromJson expects "enum"
            if (cObj.contains("enumValues")) {
                cObj["enum"] = cObj.take("enumValues");
            }
            field.constraints = Constraints::fromJson(cObj);
        }

        if (desc.contains("items")) {
            auto itemMeta = std::make_shared<FieldMeta>();
            const QJsonObject itemObj = desc.value("items").toObject();
            itemMeta->type = fieldTypeFromString(itemObj.value("type").toString("any"));
            if (itemObj.contains("constraints")) {
                itemMeta->constraints =
                    Constraints::fromJson(itemObj.value("constraints").toObject());
            }
            field.items = itemMeta;
        }

        if (desc.contains("fields")) {
            const QJsonObject fieldsObj = desc.value("fields").toObject();
            ServiceConfigSchema nested = fromJsObject(fieldsObj);
            field.fields = nested.fields;
        }

        schema.fields.append(field);
    }
    return schema;
}

ServiceConfigSchema ServiceConfigSchema::fromJsonObject(const QJsonObject& obj, QString& error) {
    error.clear();
    return parseObject(obj, QString(), error);
}

ServiceConfigSchema ServiceConfigSchema::fromJsonFile(const QString& filePath, QString& error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString("cannot open config schema file: %1").arg(filePath);
        return {};
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        error = QString("config.schema.json parse error: %1").arg(parseErr.errorString());
        return {};
    }
    if (!doc.isObject()) {
        error = "config.schema.json must be a JSON object";
        return {};
    }

    error.clear();
    return parseObject(doc.object(), QString(), error);
}

QJsonObject ServiceConfigSchema::toJson() const {
    QJsonArray fieldsArray;
    for (const auto& field : fields) {
        fieldsArray.append(field.toJson());
    }
    QJsonObject result;
    result["fields"] = fieldsArray;
    return result;
}

const FieldMeta* ServiceConfigSchema::findField(const QString& name) const {
    for (const auto& field : fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

QJsonArray ServiceConfigSchema::toFieldMetaArray() const {
    QJsonArray arr;
    for (const auto& field : fields) {
        arr.append(field.toJson());
    }
    return arr;
}

QJsonObject ServiceConfigSchema::generateDefaults() const {
    QJsonObject config;
    for (const auto& field : fields) {
        if (!field.defaultValue.isNull() && !field.defaultValue.isUndefined()) {
            config[field.name] = field.defaultValue;
        }
    }
    return config;
}

QStringList ServiceConfigSchema::requiredFieldNames() const {
    QStringList names;
    for (const auto& field : fields) {
        if (field.required) {
            names.append(field.name);
        }
    }
    return names;
}

QStringList ServiceConfigSchema::optionalFieldNames() const {
    QStringList names;
    for (const auto& field : fields) {
        if (!field.required) {
            names.append(field.name);
        }
    }
    return names;
}

} // namespace stdiolink_service
