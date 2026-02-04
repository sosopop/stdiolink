#include "help_generator.h"
#include <QStringList>

namespace stdiolink {

QString HelpGenerator::generateVersion(const meta::DriverMeta& meta) {
    QString result;
    result += meta.info.name + " v" + meta.info.version + "\n";
    if (!meta.info.vendor.isEmpty()) {
        result += meta.info.vendor + "\n";
    }
    return result;
}

QString HelpGenerator::generateHelp(const meta::DriverMeta& meta) {
    QString result;

    // 标题
    result += meta.info.name + " v" + meta.info.version + "\n";
    if (!meta.info.description.isEmpty()) {
        result += meta.info.description + "\n";
    }
    result += "\n";

    // 用法
    result += "Usage:\n";
    result += "  <program> [options]\n";
    result += "  <program> --cmd=<command> [params...]\n\n";

    // 选项
    result += "Options:\n";
    result += "  -h, --help              Show help\n";
    result += "  -v, --version           Show version\n";
    result += "  -m, --mode=<mode>       Run mode (stdio|console)\n";
    result += "  -c, --cmd=<command>     Execute command\n";
    result += "  -E, --export-meta[=path] Export metadata as JSON\n";
    result += "  -D, --export-doc=<fmt>  Export documentation (markdown|openapi|html)\n";
    result += "\n";

    // 命令列表
    if (!meta.commands.isEmpty()) {
        result += "Commands:\n";
        for (const auto& cmd : meta.commands) {
            QString name = cmd.name.leftJustified(20);
            QString desc = cmd.description.isEmpty() ? cmd.title : cmd.description;
            result += "  " + name + desc.left(50) + "\n";
        }
        result += "\n";
        result += "Use '--cmd=<command> --help' for command details.\n";
    }

    return result;
}

QString HelpGenerator::generateCommandHelp(const meta::CommandMeta& cmd) {
    QString result;

    // 命令标题
    result += "Command: " + cmd.name + "\n";
    if (!cmd.title.isEmpty()) {
        result += "  " + cmd.title + "\n";
    }
    if (!cmd.description.isEmpty()) {
        result += "  " + cmd.description + "\n";
    }
    result += "\n";

    // 参数列表
    if (!cmd.params.isEmpty()) {
        result += "Parameters:\n";
        for (const auto& param : cmd.params) {
            result += formatParam(param);
        }
        result += "\n";
    }

    // 返回值
    if (!cmd.returns.fields.isEmpty()) {
        result += "Returns:\n";
        for (const auto& field : cmd.returns.fields) {
            result += "  " + field.name + " (" + fieldTypeToString(field.type) + ")";
            if (!field.description.isEmpty()) {
                result += " - " + field.description;
            }
            result += "\n";
        }
    }

    return result;
}

QString HelpGenerator::formatParam(const meta::FieldMeta& field) {
    QString result = "  --" + field.name;

    // 类型
    result += " <" + fieldTypeToString(field.type) + ">";

    // 必填标记
    if (field.required) {
        result += " [required]";
    }

    result += "\n";

    // 描述
    if (!field.description.isEmpty()) {
        result += "      " + field.description + "\n";
    }

    // 约束
    QString constraints = formatConstraints(field.constraints);
    if (!constraints.isEmpty()) {
        result += "      " + constraints + "\n";
    }

    // 默认值
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

QString HelpGenerator::formatConstraints(const meta::Constraints& c) {
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

QString HelpGenerator::fieldTypeToString(meta::FieldType type) {
    return meta::fieldTypeToString(type);
}

} // namespace stdiolink
