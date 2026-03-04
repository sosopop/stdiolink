#include "example_auto_fill.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace stdiolink::meta {

namespace {

bool hasDefault(const FieldMeta& field) {
    return !field.defaultValue.isNull() && !field.defaultValue.isUndefined();
}

int intFromField(const FieldMeta& field) {
    if (hasDefault(field) && field.defaultValue.isDouble()) {
        return field.defaultValue.toInt();
    }
    if (field.constraints.min.has_value()) {
        return static_cast<int>(*field.constraints.min);
    }

    const QString n = field.name.toLower();
    if (n.contains("port")) return 502;
    if (n.contains("unit")) return 1;
    if (n.contains("address")) return 1;
    if (n.contains("count")) return 1;
    if (n.contains("timeout")) return 3000;
    if (n == "baud_rate") return 9600;
    if (n == "data_bits") return 8;
    if (n.contains("size")) return 100;
    if (n == "id") return 1;
    return 1;
}

QJsonValue buildExampleValue(const FieldMeta& field, int depth = 0) {
    if (depth > 4) {
        return QJsonValue();
    }
    if (hasDefault(field)) {
        return field.defaultValue;
    }

    switch (field.type) {
    case FieldType::String: {
        const QString n = field.name.toLower();
        if (n.contains("host") || n.contains("address")) return "127.0.0.1";
        if (n.contains("port_name")) return "COM1";
        if (n.contains("path")) return "demo";
        return "demo";
    }
    case FieldType::Enum:
        if (!field.constraints.enumValues.isEmpty()) {
            return field.constraints.enumValues.first();
        }
        return "demo";
    case FieldType::Int:
    case FieldType::Int64:
        return intFromField(field);
    case FieldType::Double:
        return 1.0;
    case FieldType::Bool:
        return false;
    case FieldType::Array: {
        QJsonArray arr;
        if (field.items) {
            const QJsonValue item = buildExampleValue(*field.items, depth + 1);
            if (!item.isUndefined() && !item.isNull()) {
                arr.append(item);
            }
        } else {
            arr.append(1);
        }
        return arr;
    }
    case FieldType::Object: {
        QJsonObject obj;
        for (const auto& sub : field.fields) {
            obj[sub.name] = buildExampleValue(sub, depth + 1);
        }
        return obj;
    }
    case FieldType::Any:
    default:
        return "demo";
    }
}

QJsonObject buildExampleParams(const QVector<FieldMeta>& params) {
    QJsonObject obj;
    for (const auto& field : params) {
        obj[field.name] = buildExampleValue(field);
    }
    return obj;
}

QJsonObject buildExample(const CommandMeta& cmd, const QString& mode) {
    return QJsonObject{
        {"description", QString("自动示例（%1）").arg(mode)},
        {"mode", mode},
        {"params", buildExampleParams(cmd.params)},
    };
}

bool hasModeExample(const CommandMeta& cmd, const QString& mode) {
    for (const auto& ex : cmd.examples) {
        if (ex.value("mode").toString() == mode) {
            return true;
        }
    }
    return false;
}

} // namespace

void ensureCommandExamples(DriverMeta& meta, bool addConsoleExamples) {
    for (auto& cmd : meta.commands) {
        if (!hasModeExample(cmd, "stdio")) {
            cmd.examples.append(buildExample(cmd, "stdio"));
        }
        if (addConsoleExamples && !hasModeExample(cmd, "console")) {
            cmd.examples.append(buildExample(cmd, "console"));
        }
    }
}

} // namespace stdiolink::meta

