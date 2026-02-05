#include "meta_schema_validator.h"
#include <QRegularExpression>
#include <QSet>

namespace stdiolink {

bool MetaSchemaValidator::validate(const meta::DriverMeta& meta, QString* error) {
    if (!validateSchemaVersion(meta.schemaVersion, error)) {
        return false;
    }

    if (!validateDriverInfo(meta.info, error)) {
        return false;
    }

    if (!validateCommands(meta.commands, error)) {
        return false;
    }

    return true;
}

bool MetaSchemaValidator::validateSchemaVersion(const QString& version, QString* error) {
    // 版本格式: major.minor (如 1.0, 1.1, 2.0)
    static QRegularExpression re(R"(^\d+\.\d+$)");
    if (!re.match(version).hasMatch()) {
        if (error) {
            *error = QString("Invalid schemaVersion format: '%1'. Expected: major.minor").arg(version);
        }
        return false;
    }
    return true;
}

bool MetaSchemaValidator::validateDriverInfo(const meta::DriverInfo& info, QString* error) {
    if (info.id.isEmpty()) {
        if (error) {
            *error = "Missing required field: info.id";
        }
        return false;
    }

    if (info.name.isEmpty()) {
        if (error) {
            *error = "Missing required field: info.name";
        }
        return false;
    }

    return true;
}

bool MetaSchemaValidator::validateCommands(const QVector<meta::CommandMeta>& commands,
                                           QString* error) {
    QSet<QString> names;

    for (const auto& cmd : commands) {
        if (!validateCommand(cmd, error)) {
            return false;
        }

        if (names.contains(cmd.name)) {
            if (error) {
                *error = QString("Duplicate command name: '%1'").arg(cmd.name);
            }
            return false;
        }
        names.insert(cmd.name);
    }

    return true;
}

bool MetaSchemaValidator::validateCommand(const meta::CommandMeta& cmd, QString* error) {
    if (cmd.name.isEmpty()) {
        if (error) {
            *error = "Command name cannot be empty";
        }
        return false;
    }

    // 检查参数名唯一性
    QSet<QString> paramNames;
    for (const auto& param : cmd.params) {
        if (param.name.isEmpty()) {
            if (error) {
                *error = QString("Parameter name cannot be empty in command '%1'").arg(cmd.name);
            }
            return false;
        }
        if (paramNames.contains(param.name)) {
            if (error) {
                *error = QString("Duplicate parameter '%1' in command '%2'")
                             .arg(param.name, cmd.name);
            }
            return false;
        }
        paramNames.insert(param.name);
    }

    return true;
}

} // namespace stdiolink
