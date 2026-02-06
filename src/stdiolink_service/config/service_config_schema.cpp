#include "service_config_schema.h"

#include "stdiolink/protocol/meta_types.h"

using stdiolink::meta::Constraints;
using stdiolink::meta::FieldMeta;
using stdiolink::meta::FieldType;
using stdiolink::meta::fieldTypeFromString;

namespace stdiolink_service {

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
                itemMeta->constraints = Constraints::fromJson(itemObj.value("constraints").toObject());
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

} // namespace stdiolink_service
