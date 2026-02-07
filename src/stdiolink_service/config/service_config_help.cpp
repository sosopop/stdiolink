#include "service_config_help.h"

#include <QStringList>
#include "stdiolink/protocol/meta_types.h"

namespace stdiolink_service {

QString ServiceConfigHelp::generate(const ServiceConfigSchema& schema) {
    if (schema.fields.isEmpty()) {
        return {};
    }

    QString result = "Config:\n";
    for (const auto& field : schema.fields) {
        result += formatField(field, "config");
    }
    return result;
}

QString ServiceConfigHelp::formatField(const stdiolink::meta::FieldMeta& field,
                                       const QString& prefix) {
    // For object type: recurse into sub-fields instead of printing the object itself
    if (field.type == stdiolink::meta::FieldType::Object && !field.fields.isEmpty()) {
        QString result;
        const QString childPrefix = prefix + "." + field.name;
        for (const auto& sub : field.fields) {
            result += formatField(sub, childPrefix);
        }
        return result;
    }

    QString result = "  --" + prefix + "." + field.name;

    // Type
    result += " <" + fieldTypeToString(field.type) + ">";

    // Required marker
    if (field.required) {
        result += " [required]";
    }

    result += "\n";

    // Description
    if (!field.description.isEmpty()) {
        result += "      " + field.description + "\n";
    }

    // Constraints
    QString cons = formatConstraints(field.constraints);
    if (!cons.isEmpty()) {
        result += "      " + cons + "\n";
    }

    // Default value
    if (!field.defaultValue.isNull() && !field.defaultValue.isUndefined()) {
        QString defVal;
        if (field.defaultValue.isBool()) {
            defVal = field.defaultValue.toBool() ? "true" : "false";
        } else if (field.defaultValue.isDouble()) {
            defVal = QString::number(field.defaultValue.toDouble());
        } else if (field.defaultValue.isString()) {
            defVal = "\"" + field.defaultValue.toString() + "\"";
        }
        if (!defVal.isEmpty()) {
            result += "      Default: " + defVal + "\n";
        }
    }

    return result;
}

QString ServiceConfigHelp::formatConstraints(const stdiolink::meta::Constraints& c) {
    QStringList parts;

    if (c.min.has_value() && c.max.has_value()) {
        parts << QString("Range: %1-%2").arg(*c.min).arg(*c.max);
    } else if (c.min.has_value()) {
        parts << QString("Min: %1").arg(*c.min);
    } else if (c.max.has_value()) {
        parts << QString("Max: %1").arg(*c.max);
    }

    if (c.minLength.has_value() && c.maxLength.has_value()) {
        parts << QString("Length: %1-%2").arg(*c.minLength).arg(*c.maxLength);
    } else if (c.minLength.has_value()) {
        parts << QString("MinLength: %1").arg(*c.minLength);
    } else if (c.maxLength.has_value()) {
        parts << QString("MaxLength: %1").arg(*c.maxLength);
    }

    if (!c.pattern.isEmpty()) {
        parts << QString("Pattern: %1").arg(c.pattern);
    }

    if (!c.enumValues.isEmpty()) {
        QStringList vals;
        for (const auto& v : c.enumValues) {
            vals << v.toString();
        }
        parts << QString("Values: [%1]").arg(vals.join(", "));
    }

    return parts.join(", ");
}

QString ServiceConfigHelp::fieldTypeToString(stdiolink::meta::FieldType type) {
    return stdiolink::meta::fieldTypeToString(type);
}

} // namespace stdiolink_service
