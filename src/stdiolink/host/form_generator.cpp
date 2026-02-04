#include "form_generator.h"

namespace stdiolink {

FormDesc UiGenerator::generateCommandForm(const meta::CommandMeta& cmd) {
    FormDesc form;
    form.title = cmd.title.isEmpty() ? cmd.name : cmd.title;
    form.description = cmd.description;

    for (const auto& param : cmd.params) {
        form.widgets.append(fieldToWidget(param));
    }

    return form;
}

FormDesc UiGenerator::generateConfigForm(const meta::ConfigSchema& config) {
    FormDesc form;
    form.title = "Configuration";

    for (const auto& field : config.fields) {
        form.widgets.append(fieldToWidget(field));
    }

    return form;
}

QJsonObject UiGenerator::toJson(const FormDesc& form) {
    QJsonObject obj;
    obj["title"] = form.title;
    obj["description"] = form.description;
    obj["widgets"] = form.widgets;
    return obj;
}

QJsonObject UiGenerator::fieldToWidget(const meta::FieldMeta& field) {
    QJsonObject widget;
    widget["name"] = field.name;
    widget["type"] = meta::fieldTypeToString(field.type);
    widget["label"] = field.description;
    widget["required"] = field.required;

    // 设置控件类型
    QString w = field.ui.widget;
    if (w.isEmpty()) {
        w = defaultWidget(field.type);
    }
    widget["widget"] = w;

    // 默认值
    if (!field.defaultValue.isNull() && !field.defaultValue.isUndefined()) {
        widget["default"] = field.defaultValue;
    }

    // 约束条件
    if (field.constraints.min.has_value())
        widget["min"] = *field.constraints.min;
    if (field.constraints.max.has_value())
        widget["max"] = *field.constraints.max;
    if (!field.constraints.enumValues.isEmpty())
        widget["options"] = field.constraints.enumValues;

    // UI 提示
    if (!field.ui.unit.isEmpty())
        widget["unit"] = field.ui.unit;
    if (!field.ui.placeholder.isEmpty())
        widget["placeholder"] = field.ui.placeholder;
    if (!field.ui.group.isEmpty())
        widget["group"] = field.ui.group;
    if (field.ui.advanced)
        widget["advanced"] = true;
    if (field.ui.readonly)
        widget["readonly"] = true;

    return widget;
}

QString UiGenerator::defaultWidget(meta::FieldType type) {
    switch (type) {
    case meta::FieldType::String:
        return "text";
    case meta::FieldType::Int:
    case meta::FieldType::Int64:
    case meta::FieldType::Double:
        return "number";
    case meta::FieldType::Bool:
        return "checkbox";
    case meta::FieldType::Enum:
        return "select";
    case meta::FieldType::Object:
        return "object";
    case meta::FieldType::Array:
        return "array";
    case meta::FieldType::Any:
        return "json";
    }
    return "text";
}

} // namespace stdiolink
