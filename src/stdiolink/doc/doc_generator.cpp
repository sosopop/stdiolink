#include "doc_generator.h"
#include <QJsonArray>
#include <QJsonDocument>

namespace stdiolink {

// ============================================
// Markdown 生成
// ============================================

QString DocGenerator::toMarkdown(const meta::DriverMeta& meta) {
    QString md;

    // 标题
    md += "# " + meta.info.name + "\n\n";

    // 版本和描述
    if (!meta.info.version.isEmpty()) {
        md += "**Version:** " + meta.info.version + "\n\n";
    }
    if (!meta.info.description.isEmpty()) {
        md += meta.info.description + "\n\n";
    }
    if (!meta.info.vendor.isEmpty()) {
        md += "**Vendor:** " + meta.info.vendor + "\n\n";
    }

    // 命令列表
    if (!meta.commands.isEmpty()) {
        md += "## Commands\n\n";
        for (const auto& cmd : meta.commands) {
            md += "### " + cmd.name + "\n\n";
            if (!cmd.title.isEmpty()) {
                md += "**" + cmd.title + "**\n\n";
            }
            if (!cmd.description.isEmpty()) {
                md += cmd.description + "\n\n";
            }

            // 参数
            if (!cmd.params.isEmpty()) {
                md += "#### Parameters\n\n";
                md += "| Name | Type | Required | Description |\n";
                md += "|------|------|----------|-------------|\n";
                for (const auto& param : cmd.params) {
                    md += formatFieldMarkdown(param);
                }
                md += "\n";
            }

            // 返回值
            if (!cmd.returns.fields.isEmpty()) {
                md += "#### Returns\n\n";
                md += "| Name | Type | Description |\n";
                md += "|------|------|-------------|\n";
                for (const auto& field : cmd.returns.fields) {
                    QString type = meta::fieldTypeToString(field.type);
                    md += "| " + field.name + " | " + type + " | " + field.description + " |\n";
                }
                md += "\n";
            }
        }
    }

    // 配置
    if (!meta.config.fields.isEmpty()) {
        md += "## Configuration\n\n";
        md += "| Name | Type | Default | Description |\n";
        md += "|------|------|---------|-------------|\n";
        for (const auto& field : meta.config.fields) {
            QString type = meta::fieldTypeToString(field.type);
            QString def = field.defaultValue.isNull() ? "-" :
                QJsonDocument(QJsonArray{field.defaultValue}).toJson(QJsonDocument::Compact).mid(1);
            if (def.endsWith("]")) def.chop(1);
            md += "| " + field.name + " | " + type + " | " + def + " | " + field.description + " |\n";
        }
        md += "\n";
    }

    return md;
}

QString DocGenerator::formatFieldMarkdown(const meta::FieldMeta& field, int indent) {
    Q_UNUSED(indent)
    QString type = meta::fieldTypeToString(field.type);
    QString req = field.required ? "Yes" : "No";
    QString desc = field.description;

    // 添加约束信息
    QString constraints = formatConstraintsMarkdown(field.constraints);
    if (!constraints.isEmpty()) {
        desc += " " + constraints;
    }

    return "| " + field.name + " | " + type + " | " + req + " | " + desc + " |\n";
}

QString DocGenerator::formatConstraintsMarkdown(const meta::Constraints& c) {
    QStringList parts;
    if (c.min.has_value() && c.max.has_value()) {
        parts << QString("Range: %1-%2").arg(*c.min).arg(*c.max);
    }
    if (!c.enumValues.isEmpty()) {
        QStringList vals;
        for (const auto& v : c.enumValues) {
            vals << "`" + v.toString() + "`";
        }
        parts << "Values: " + vals.join(", ");
    }
    if (!parts.isEmpty()) {
        return "(" + parts.join(", ") + ")";
    }
    return QString();
}

// ============================================
// OpenAPI 生成
// ============================================

QJsonObject DocGenerator::toOpenAPI(const meta::DriverMeta& meta) {
    QJsonObject api;

    // OpenAPI 版本
    api["openapi"] = "3.0.3";

    // Info
    QJsonObject info;
    info["title"] = meta.info.name;
    info["version"] = meta.info.version;
    if (!meta.info.description.isEmpty()) {
        info["description"] = meta.info.description;
    }
    api["info"] = info;

    // Paths
    QJsonObject paths;
    for (const auto& cmd : meta.commands) {
        QString path = commandToPath(cmd.name);
        QJsonObject pathItem;
        QJsonObject post;

        post["summary"] = cmd.title.isEmpty() ? cmd.name : cmd.title;
        if (!cmd.description.isEmpty()) {
            post["description"] = cmd.description;
        }
        post["operationId"] = cmd.name;

        // Request body
        if (!cmd.params.isEmpty()) {
            QJsonObject requestBody;
            QJsonObject content;
            QJsonObject jsonContent;
            QJsonObject schema;
            schema["type"] = "object";

            QJsonObject properties;
            QJsonArray required;
            for (const auto& param : cmd.params) {
                properties[param.name] = fieldToSchema(param);
                if (param.required) {
                    required.append(param.name);
                }
            }
            schema["properties"] = properties;
            if (!required.isEmpty()) {
                schema["required"] = required;
            }

            jsonContent["schema"] = schema;
            content["application/json"] = jsonContent;
            requestBody["content"] = content;
            post["requestBody"] = requestBody;
        }

        // Responses
        QJsonObject responses;
        QJsonObject response200;
        response200["description"] = "Success";
        responses["200"] = response200;
        post["responses"] = responses;

        pathItem["post"] = post;
        paths[path] = pathItem;
    }
    api["paths"] = paths;

    return api;
}

QString DocGenerator::commandToPath(const QString& cmdName) {
    // scan -> /scan
    // mesh.union -> /mesh/union
    QString path = "/" + cmdName;
    path.replace('.', '/');
    return path;
}

QJsonObject DocGenerator::fieldToSchema(const meta::FieldMeta& field) {
    QJsonObject schema;
    schema["type"] = fieldTypeToOpenAPIType(field.type);

    if (!field.description.isEmpty()) {
        schema["description"] = field.description;
    }

    // 约束
    if (field.constraints.min.has_value()) {
        schema["minimum"] = *field.constraints.min;
    }
    if (field.constraints.max.has_value()) {
        schema["maximum"] = *field.constraints.max;
    }
    if (!field.constraints.enumValues.isEmpty()) {
        schema["enum"] = field.constraints.enumValues;
    }

    // 默认值
    if (!field.defaultValue.isNull() && !field.defaultValue.isUndefined()) {
        schema["default"] = field.defaultValue;
    }

    return schema;
}

QString DocGenerator::fieldTypeToOpenAPIType(meta::FieldType type) {
    switch (type) {
    case meta::FieldType::String:
    case meta::FieldType::Enum:
        return "string";
    case meta::FieldType::Int:
    case meta::FieldType::Int64:
        return "integer";
    case meta::FieldType::Double:
        return "number";
    case meta::FieldType::Bool:
        return "boolean";
    case meta::FieldType::Array:
        return "array";
    case meta::FieldType::Object:
    case meta::FieldType::Any:
    default:
        return "object";
    }
}

// ============================================
// HTML 生成
// ============================================

QString DocGenerator::toHtml(const meta::DriverMeta& meta) {
    QString html;

    // HTML 头部
    html += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    html += "  <meta charset=\"UTF-8\">\n";
    html += "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    html += "  <title>" + meta.info.name + "</title>\n";
    html += "  <style>\n";
    html += generateHtmlStyle();
    html += "  </style>\n";
    html += "</head>\n<body>\n";

    // 内容
    html += "<div class=\"container\">\n";
    html += "  <h1>" + meta.info.name + "</h1>\n";

    if (!meta.info.version.isEmpty()) {
        html += "  <p class=\"version\">Version: " + meta.info.version + "</p>\n";
    }
    if (!meta.info.description.isEmpty()) {
        html += "  <p class=\"description\">" + meta.info.description + "</p>\n";
    }

    // 命令列表
    if (!meta.commands.isEmpty()) {
        html += "  <h2>Commands</h2>\n";
        for (const auto& cmd : meta.commands) {
            html += "  <div class=\"command\">\n";
            html += "    <h3>" + cmd.name + "</h3>\n";
            if (!cmd.description.isEmpty()) {
                html += "    <p>" + cmd.description + "</p>\n";
            }

            // 参数表格
            if (!cmd.params.isEmpty()) {
                html += "    <h4>Parameters</h4>\n";
                html += "    <table>\n";
                html += "      <tr><th>Name</th><th>Type</th><th>Required</th><th>Description</th></tr>\n";
                for (const auto& p : cmd.params) {
                    QString type = meta::fieldTypeToString(p.type);
                    QString req = p.required ? "Yes" : "No";
                    html += "      <tr><td>" + p.name + "</td><td>" + type + "</td>";
                    html += "<td>" + req + "</td><td>" + p.description + "</td></tr>\n";
                }
                html += "    </table>\n";
            }
            html += "  </div>\n";
        }
    }

    html += "</div>\n</body>\n</html>\n";
    return html;
}

QString DocGenerator::generateHtmlStyle() {
    return R"(
    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }
    .container { max-width: 900px; margin: 0 auto; background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
    h1 { color: #333; border-bottom: 2px solid #007bff; padding-bottom: 10px; }
    h2 { color: #444; margin-top: 30px; }
    h3 { color: #007bff; }
    h4 { color: #666; margin-top: 15px; }
    .version { color: #666; font-size: 0.9em; }
    .description { color: #555; line-height: 1.6; }
    .command { background: #f8f9fa; padding: 15px; margin: 15px 0; border-radius: 5px; border-left: 4px solid #007bff; }
    table { width: 100%; border-collapse: collapse; margin: 10px 0; }
    th, td { padding: 10px; text-align: left; border-bottom: 1px solid #ddd; }
    th { background: #f0f0f0; font-weight: 600; }
    tr:hover { background: #f5f5f5; }
)";
}

} // namespace stdiolink
