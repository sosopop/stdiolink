#include "meta_types.h"

namespace stdiolink::meta {

QString fieldTypeToString(FieldType type) {
    switch (type) {
    case FieldType::String:
        return "string";
    case FieldType::Int:
        return "int";
    case FieldType::Int64:
        return "int64";
    case FieldType::Double:
        return "double";
    case FieldType::Bool:
        return "bool";
    case FieldType::Object:
        return "object";
    case FieldType::Array:
        return "array";
    case FieldType::Enum:
        return "enum";
    case FieldType::Any:
        return "any";
    }
    return "any";
}

FieldType fieldTypeFromString(const QString& str) {
    if (str == "string")
        return FieldType::String;
    if (str == "int" || str == "integer")
        return FieldType::Int;
    if (str == "int64")
        return FieldType::Int64;
    if (str == "double" || str == "number")
        return FieldType::Double;
    if (str == "bool" || str == "boolean")
        return FieldType::Bool;
    if (str == "object")
        return FieldType::Object;
    if (str == "array")
        return FieldType::Array;
    if (str == "enum")
        return FieldType::Enum;
    return FieldType::Any;
}

// UIHint 实现
QJsonObject UIHint::toJson() const {
    QJsonObject obj;
    if (!widget.isEmpty())
        obj["widget"] = widget;
    if (!group.isEmpty())
        obj["group"] = group;
    if (order != 0)
        obj["order"] = order;
    if (!placeholder.isEmpty())
        obj["placeholder"] = placeholder;
    if (advanced)
        obj["advanced"] = true;
    if (readonly)
        obj["readonly"] = true;
    if (!visibleIf.isEmpty())
        obj["visibleIf"] = visibleIf;
    if (!unit.isEmpty())
        obj["unit"] = unit;
    if (step != 0)
        obj["step"] = step;
    return obj;
}

UIHint UIHint::fromJson(const QJsonObject& obj) {
    UIHint hint;
    hint.widget = obj["widget"].toString();
    hint.group = obj["group"].toString();
    hint.order = obj["order"].toInt();
    hint.placeholder = obj["placeholder"].toString();
    hint.advanced = obj["advanced"].toBool();
    hint.readonly = obj["readonly"].toBool();
    hint.visibleIf = obj["visibleIf"].toString();
    hint.unit = obj["unit"].toString();
    hint.step = obj["step"].toDouble();
    return hint;
}

bool UIHint::isEmpty() const {
    return widget.isEmpty() && group.isEmpty() && order == 0 &&
           placeholder.isEmpty() && !advanced && !readonly &&
           visibleIf.isEmpty() && unit.isEmpty() && step == 0;
}

// Constraints 实现
QJsonObject Constraints::toJson() const {
    QJsonObject obj;
    if (min.has_value())
        obj["min"] = *min;
    if (max.has_value())
        obj["max"] = *max;
    if (minLength.has_value())
        obj["minLength"] = *minLength;
    if (maxLength.has_value())
        obj["maxLength"] = *maxLength;
    if (!pattern.isEmpty())
        obj["pattern"] = pattern;
    if (!enumValues.isEmpty())
        obj["enum"] = enumValues;
    if (!format.isEmpty())
        obj["format"] = format;
    if (minItems.has_value())
        obj["minItems"] = *minItems;
    if (maxItems.has_value())
        obj["maxItems"] = *maxItems;
    return obj;
}

Constraints Constraints::fromJson(const QJsonObject& obj) {
    Constraints c;
    if (obj.contains("min"))
        c.min = obj["min"].toDouble();
    if (obj.contains("max"))
        c.max = obj["max"].toDouble();
    if (obj.contains("minLength"))
        c.minLength = obj["minLength"].toInt();
    if (obj.contains("maxLength"))
        c.maxLength = obj["maxLength"].toInt();
    c.pattern = obj["pattern"].toString();
    c.enumValues = obj["enum"].toArray();
    c.format = obj["format"].toString();
    if (obj.contains("minItems"))
        c.minItems = obj["minItems"].toInt();
    if (obj.contains("maxItems"))
        c.maxItems = obj["maxItems"].toInt();
    return c;
}

bool Constraints::isEmpty() const {
    return !min.has_value() && !max.has_value() && !minLength.has_value() &&
           !maxLength.has_value() && pattern.isEmpty() && enumValues.isEmpty() &&
           format.isEmpty() && !minItems.has_value() && !maxItems.has_value();
}

// FieldMeta 实现
QJsonObject FieldMeta::toJson() const {
    QJsonObject obj;
    if (!name.isEmpty())
        obj["name"] = name;
    obj["type"] = fieldTypeToString(type);
    if (required)
        obj["required"] = true;
    if (!defaultValue.isNull())
        obj["default"] = defaultValue;
    if (!description.isEmpty())
        obj["description"] = description;

    // 合并约束条件
    QJsonObject c = constraints.toJson();
    for (auto it = c.begin(); it != c.end(); ++it) {
        obj[it.key()] = it.value();
    }

    // UI 提示
    if (!ui.isEmpty())
        obj["ui"] = ui.toJson();

    // 嵌套字段
    if (!fields.isEmpty()) {
        QJsonArray arr;
        for (const auto& f : fields) {
            arr.append(f.toJson());
        }
        obj["fields"] = arr;
    }

    // Array items
    if (items)
        obj["items"] = items->toJson();

    // Object 属性
    if (!requiredKeys.isEmpty())
        obj["requiredKeys"] = QJsonArray::fromStringList(requiredKeys);
    if (!additionalProperties)
        obj["additionalProperties"] = false;

    return obj;
}

FieldMeta FieldMeta::fromJson(const QJsonObject& obj) {
    FieldMeta f;
    f.name = obj["name"].toString();
    f.type = fieldTypeFromString(obj["type"].toString());
    f.required = obj["required"].toBool();
    f.defaultValue = obj["default"];
    f.description = obj["description"].toString();
    f.constraints = Constraints::fromJson(obj);
    if (obj.contains("ui"))
        f.ui = UIHint::fromJson(obj["ui"].toObject());

    // 嵌套字段
    if (obj.contains("fields")) {
        for (const auto& v : obj["fields"].toArray()) {
            f.fields.append(FieldMeta::fromJson(v.toObject()));
        }
    }

    // Array items
    if (obj.contains("items")) {
        f.items = std::make_shared<FieldMeta>(
            FieldMeta::fromJson(obj["items"].toObject()));
    }

    // Object 属性
    if (obj.contains("requiredKeys")) {
        for (const auto& v : obj["requiredKeys"].toArray()) {
            f.requiredKeys.append(v.toString());
        }
    }
    if (obj.contains("additionalProperties"))
        f.additionalProperties = obj["additionalProperties"].toBool();

    return f;
}

// EventMeta 实现
QJsonObject EventMeta::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    if (!description.isEmpty())
        obj["description"] = description;
    if (!fields.isEmpty()) {
        QJsonArray arr;
        for (const auto& f : fields) {
            arr.append(f.toJson());
        }
        obj["fields"] = arr;
    }
    return obj;
}

EventMeta EventMeta::fromJson(const QJsonObject& obj) {
    EventMeta e;
    e.name = obj["name"].toString();
    e.description = obj["description"].toString();
    for (const auto& v : obj["fields"].toArray()) {
        e.fields.append(FieldMeta::fromJson(v.toObject()));
    }
    return e;
}

// ReturnMeta 实现
QJsonObject ReturnMeta::toJson() const {
    QJsonObject obj;
    obj["type"] = fieldTypeToString(type);
    if (!description.isEmpty())
        obj["description"] = description;
    if (!fields.isEmpty()) {
        QJsonArray arr;
        for (const auto& f : fields) {
            arr.append(f.toJson());
        }
        obj["fields"] = arr;
    }
    return obj;
}

ReturnMeta ReturnMeta::fromJson(const QJsonObject& obj) {
    ReturnMeta r;
    r.type = fieldTypeFromString(obj["type"].toString());
    r.description = obj["description"].toString();
    for (const auto& v : obj["fields"].toArray()) {
        r.fields.append(FieldMeta::fromJson(v.toObject()));
    }
    return r;
}

// CommandMeta 实现
QJsonObject CommandMeta::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    if (!description.isEmpty())
        obj["description"] = description;
    if (!title.isEmpty())
        obj["title"] = title;
    if (!summary.isEmpty())
        obj["summary"] = summary;

    if (!params.isEmpty()) {
        QJsonArray arr;
        for (const auto& p : params) {
            arr.append(p.toJson());
        }
        obj["params"] = arr;
    }

    obj["returns"] = returns.toJson();

    if (!events.isEmpty()) {
        QJsonArray arr;
        for (const auto& e : events) {
            arr.append(e.toJson());
        }
        obj["events"] = arr;
    }

    if (!errors.isEmpty())
        obj["errors"] = QJsonArray::fromVariantList(
            QVariantList(errors.begin(), errors.end()));
    if (!examples.isEmpty())
        obj["examples"] = QJsonArray::fromVariantList(
            QVariantList(examples.begin(), examples.end()));
    if (!ui.isEmpty())
        obj["ui"] = ui.toJson();

    return obj;
}

CommandMeta CommandMeta::fromJson(const QJsonObject& obj) {
    CommandMeta c;
    c.name = obj["name"].toString();
    c.description = obj["description"].toString();
    c.title = obj["title"].toString();
    c.summary = obj["summary"].toString();

    for (const auto& v : obj["params"].toArray()) {
        c.params.append(FieldMeta::fromJson(v.toObject()));
    }

    if (obj.contains("returns"))
        c.returns = ReturnMeta::fromJson(obj["returns"].toObject());

    for (const auto& v : obj["events"].toArray()) {
        c.events.append(EventMeta::fromJson(v.toObject()));
    }

    for (const auto& v : obj["errors"].toArray()) {
        c.errors.append(v.toObject());
    }
    for (const auto& v : obj["examples"].toArray()) {
        c.examples.append(v.toObject());
    }

    if (obj.contains("ui"))
        c.ui = UIHint::fromJson(obj["ui"].toObject());

    return c;
}

// ConfigApply 实现
QJsonObject ConfigApply::toJson() const {
    QJsonObject obj;
    if (!method.isEmpty())
        obj["method"] = method;
    if (!envPrefix.isEmpty())
        obj["envPrefix"] = envPrefix;
    if (!command.isEmpty())
        obj["command"] = command;
    if (!fileName.isEmpty())
        obj["fileName"] = fileName;
    return obj;
}

ConfigApply ConfigApply::fromJson(const QJsonObject& obj) {
    ConfigApply a;
    a.method = obj["method"].toString();
    a.envPrefix = obj["envPrefix"].toString();
    a.command = obj["command"].toString();
    a.fileName = obj["fileName"].toString();
    return a;
}

// ConfigSchema 实现
QJsonObject ConfigSchema::toJson() const {
    QJsonObject obj;
    if (!fields.isEmpty()) {
        QJsonArray arr;
        for (const auto& f : fields) {
            arr.append(f.toJson());
        }
        obj["fields"] = arr;
    }
    obj["apply"] = apply.toJson();
    return obj;
}

ConfigSchema ConfigSchema::fromJson(const QJsonObject& obj) {
    ConfigSchema s;
    for (const auto& v : obj["fields"].toArray()) {
        s.fields.append(FieldMeta::fromJson(v.toObject()));
    }
    if (obj.contains("apply"))
        s.apply = ConfigApply::fromJson(obj["apply"].toObject());
    return s;
}

// DriverInfo 实现
QJsonObject DriverInfo::toJson() const {
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["version"] = version;
    if (!description.isEmpty())
        obj["description"] = description;
    if (!vendor.isEmpty())
        obj["vendor"] = vendor;
    if (!entry.isEmpty())
        obj["entry"] = entry;
    if (!capabilities.isEmpty())
        obj["capabilities"] = QJsonArray::fromStringList(capabilities);
    if (!profiles.isEmpty())
        obj["profiles"] = QJsonArray::fromStringList(profiles);
    return obj;
}

DriverInfo DriverInfo::fromJson(const QJsonObject& obj) {
    DriverInfo i;
    i.id = obj["id"].toString();
    i.name = obj["name"].toString();
    i.version = obj["version"].toString();
    i.description = obj["description"].toString();
    i.vendor = obj["vendor"].toString();
    i.entry = obj["entry"].toObject();
    for (const auto& v : obj["capabilities"].toArray()) {
        i.capabilities.append(v.toString());
    }
    for (const auto& v : obj["profiles"].toArray()) {
        i.profiles.append(v.toString());
    }
    return i;
}

// DriverMeta 实现
QJsonObject DriverMeta::toJson() const {
    QJsonObject obj;
    obj["schemaVersion"] = schemaVersion;
    obj["info"] = info.toJson();

    if (!config.fields.isEmpty() || !config.apply.method.isEmpty())
        obj["config"] = config.toJson();

    if (!commands.isEmpty()) {
        QJsonArray arr;
        for (const auto& c : commands) {
            arr.append(c.toJson());
        }
        obj["commands"] = arr;
    }

    if (!types.isEmpty()) {
        QJsonObject t;
        for (auto it = types.begin(); it != types.end(); ++it) {
            t[it.key()] = it.value().toJson();
        }
        obj["types"] = t;
    }

    return obj;
}

DriverMeta DriverMeta::fromJson(const QJsonObject& obj) {
    DriverMeta m;
    m.schemaVersion = obj["schemaVersion"].toString();

    // 兼容 info 和 driver 两种字段名
    if (obj.contains("info"))
        m.info = DriverInfo::fromJson(obj["info"].toObject());
    else if (obj.contains("driver"))
        m.info = DriverInfo::fromJson(obj["driver"].toObject());

    if (obj.contains("config"))
        m.config = ConfigSchema::fromJson(obj["config"].toObject());

    for (const auto& v : obj["commands"].toArray()) {
        m.commands.append(CommandMeta::fromJson(v.toObject()));
    }

    if (obj.contains("types")) {
        QJsonObject t = obj["types"].toObject();
        for (auto it = t.begin(); it != t.end(); ++it) {
            m.types[it.key()] = FieldMeta::fromJson(it.value().toObject());
        }
    }

    return m;
}

const CommandMeta* DriverMeta::findCommand(const QString& name) const {
    for (const auto& cmd : commands) {
        if (cmd.name == name)
            return &cmd;
    }
    return nullptr;
}

} // namespace stdiolink::meta
