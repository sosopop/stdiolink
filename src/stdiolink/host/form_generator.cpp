#include "form_generator.h"
#include <QRegularExpression>
#include <algorithm>

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
    if (field.ui.order != 0)
        widget["order"] = field.ui.order;
    if (field.ui.advanced)
        widget["advanced"] = true;
    if (field.ui.readonly)
        widget["readonly"] = true;
    if (!field.ui.visibleIf.isEmpty())
        widget["visibleIf"] = field.ui.visibleIf;

    // M16: 嵌套对象递归渲染
    if (field.type == meta::FieldType::Object && !field.fields.isEmpty()) {
        QJsonArray nestedWidgets;
        for (const auto& nested : field.fields) {
            nestedWidgets.append(fieldToWidget(nested));
        }
        widget["fields"] = nestedWidgets;
    }

    // M16: 数组元素类型
    if (field.type == meta::FieldType::Array && field.items) {
        widget["items"] = fieldToWidget(*field.items);
    }

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

// ============================================
// M16: 高级 UI 生成
// ============================================

QHash<QString, QVector<meta::FieldMeta>> UiGenerator::groupFields(
    const QVector<meta::FieldMeta>& fields) {
    QHash<QString, QVector<meta::FieldMeta>> groups;

    for (const auto& field : fields) {
        QString group = field.ui.group.isEmpty() ? "default" : field.ui.group;
        groups[group].append(field);
    }

    return groups;
}

QVector<meta::FieldMeta> UiGenerator::sortFields(const QVector<meta::FieldMeta>& fields) {
    QVector<meta::FieldMeta> sorted = fields;

    std::sort(sorted.begin(), sorted.end(),
              [](const meta::FieldMeta& a, const meta::FieldMeta& b) {
                  return a.ui.order < b.ui.order;
              });

    return sorted;
}

// ============================================
// M16: 条件求值器
// ============================================

bool ConditionEvaluator::evaluate(const QString& condition, const QJsonObject& context) {
    if (condition.isEmpty()) {
        return true;
    }

    // 支持简单的比较表达式: "field == 'value'" 或 "field != 'value'"
    static QRegularExpression re(R"((\w+)\s*(==|!=|>|<|>=|<=)\s*('[^']*'|"[^"]*"|\w+))");
    auto match = re.match(condition.trimmed());

    if (match.hasMatch()) {
        QString left = match.captured(1);
        QString op = match.captured(2);
        QString right = match.captured(3);
        return evaluateComparison(left, op, right, context);
    }

    // 简单布尔字段: "advanced" 或 "!advanced"
    QString trimmed = condition.trimmed();
    if (trimmed.startsWith('!')) {
        QString field = trimmed.mid(1);
        return !context.value(field).toBool();
    }
    return context.value(trimmed).toBool();
}

bool ConditionEvaluator::evaluateComparison(const QString& left, const QString& op,
                                            const QString& right, const QJsonObject& context) {
    QJsonValue leftVal = resolveValue(left, context);
    QJsonValue rightVal = resolveValue(right, context);

    if (op == "==") {
        return leftVal == rightVal;
    }
    if (op == "!=") {
        return leftVal != rightVal;
    }

    // 数值比较
    double leftNum = leftVal.toDouble();
    double rightNum = rightVal.toDouble();

    if (op == ">") return leftNum > rightNum;
    if (op == "<") return leftNum < rightNum;
    if (op == ">=") return leftNum >= rightNum;
    if (op == "<=") return leftNum <= rightNum;

    return false;
}

QJsonValue ConditionEvaluator::resolveValue(const QString& expr, const QJsonObject& context) {
    // 字符串字面量 'value' 或 "value"
    if ((expr.startsWith('\'') && expr.endsWith('\'')) ||
        (expr.startsWith('"') && expr.endsWith('"'))) {
        return expr.mid(1, expr.length() - 2);
    }

    // 布尔字面量
    if (expr == "true") return true;
    if (expr == "false") return false;

    // 数字字面量
    bool ok = false;
    double num = expr.toDouble(&ok);
    if (ok) return num;

    // 字段引用
    return context.value(expr);
}

} // namespace stdiolink
