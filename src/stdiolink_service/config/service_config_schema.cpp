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
    static const QSet<QString> known = {
        "string", "int", "integer", "int64", "double", "number",
        "bool", "boolean", "object", "array", "enum", "any"
    };
    return known.contains(typeStr);
}

ServiceConfigSchema parseObject(const QJsonObject& obj,
                                const QString& pathPrefix,
                                QString& error,
                                bool strictTypeCheck = true);

FieldMeta parseFieldMeta(const QString& name,
                         const QJsonObject& desc,
                         const QString& fieldPath,
                         QString& error,
                         bool strictTypeCheck) {
    FieldMeta field;
    field.name = name;

    const QString typeStr = desc.value("type").toString("any");
    if (strictTypeCheck && !isKnownFieldType(typeStr)) {
        error = QString("unknown field type \"%1\" for field \"%2\"").arg(typeStr, fieldPath);
        return {};
    }

    field.type = fieldTypeFromString(typeStr);
    field.required = desc.value("required").toBool(false);
    field.description = desc.value("description").toString();
    field.additionalProperties = desc.value("additionalProperties").toBool(true);

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

    if (desc.contains("requiredKeys")) {
        const QJsonArray requiredKeys = desc.value("requiredKeys").toArray();
        for (const auto& key : requiredKeys) {
            if (key.isString()) {
                field.requiredKeys.append(key.toString());
            }
        }
    }

    if (desc.contains("fields")) {
        if (!desc.value("fields").isObject()) {
            error = QString("\"fields\" for field \"%1\" must be a JSON object").arg(fieldPath);
            return {};
        }
        const QJsonObject fieldsObj = desc.value("fields").toObject();
        ServiceConfigSchema nested = parseObject(fieldsObj, fieldPath, error, strictTypeCheck);
        if (!error.isEmpty()) {
            return {};
        }
        field.fields = nested.fields;
    }

    if (desc.contains("items")) {
        if (!desc.value("items").isObject()) {
            error = QString("\"items\" for field \"%1\" must be a JSON object").arg(fieldPath);
            return {};
        }
        const QString itemPath = fieldPath + ".items";
        FieldMeta itemMeta = parseFieldMeta(name,
                                            desc.value("items").toObject(),
                                            itemPath,
                                            error,
                                            strictTypeCheck);
        if (!error.isEmpty()) {
            return {};
        }
        field.items = std::make_shared<FieldMeta>(itemMeta);
    }

    return field;
}

/// 带错误检查的递归解析（内部实现）
ServiceConfigSchema parseObject(const QJsonObject& obj,
                                const QString& pathPrefix,
                                QString& error,
                                bool strictTypeCheck) {
    ServiceConfigSchema schema;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString& fieldName = it.key();
        const QString fieldPath = pathPrefix.isEmpty() ? fieldName : (pathPrefix + "." + fieldName);
        if (!it.value().isObject()) {
            error = QString("field descriptor for \"%1\" must be a JSON object").arg(fieldPath);
            return {};
        }
        FieldMeta field = parseFieldMeta(fieldName,
                                         it.value().toObject(),
                                         fieldPath,
                                         error,
                                         strictTypeCheck);
        if (!error.isEmpty()) {
            return {};
        }

        schema.fields.append(field);
    }
    return schema;
}

} // namespace

ServiceConfigSchema ServiceConfigSchema::fromJsObject(const QJsonObject& obj) {
    QString error;
    return parseObject(obj, QString(), error, /*strictTypeCheck=*/false);
}

ServiceConfigSchema ServiceConfigSchema::fromJsonObject(const QJsonObject& obj,
                                                          QString& error) {
    error.clear();
    return parseObject(obj, QString(), error, /*strictTypeCheck=*/true);
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
    return parseObject(doc.object(), QString(), error, /*strictTypeCheck=*/true);
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

const FieldMeta* ServiceConfigSchema::findField(const QString& name) const {
    for (const auto& field : fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

} // namespace stdiolink_service
