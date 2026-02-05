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
            QString def = field.defaultValue.isNull()
                              ? QString("-")
                              : QString(QJsonDocument(QJsonArray{field.defaultValue})
                                    .toJson(QJsonDocument::Compact)
                                    .mid(1));
            if (def.endsWith("]"))
                def.chop(1);
            md +=
                "| " + field.name + " | " + type + " | " + def + " | " + field.description + " |\n";
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

    // HTML Head
    html += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    html += "  <meta charset=\"UTF-8\">\n";
    html += "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    html += "  <title>" + meta.info.name + " Documentation</title>\n";
    html += "  <style>\n";
    html += generateHtmlStyle();
    html += "  </style>\n";
    html += "</head>\n<body>\n";

    // Mobile Header / Hamburger
    html += "<header id=\"mobile-header\">\n";
    html += "  <button id=\"menu-toggle\" aria-label=\"Toggle navigation\">☰</button>\n";
    html += "  <span class=\"mobile-title\">" + meta.info.name + "</span>\n";
    html += "</header>\n";

    // Layout Wrapper
    html += "<div class=\"layout\">\n";

    // Sidebar
    html += "  <nav id=\"sidebar\">\n";
    html += "    <div class=\"sidebar-header\">\n";
    html += "      <h2>" + meta.info.name + "</h2>\n";
    if (!meta.info.version.isEmpty()) {
        html += "      <span class=\"badge version\">v" + meta.info.version + "</span>\n";
    }
    html += "    </div>\n";

    html += "    <ul class=\"nav-links\">\n";
    html += "      <li><a href=\"#overview\" class=\"nav-link active\">Overview</a></li>\n";

    if (!meta.commands.isEmpty()) {
        html += "      <li class=\"nav-group\">Commands</li>\n";
        for (const auto& cmd : meta.commands) {
            html += "      <li><a href=\"#cmd-" + cmd.name + "\" class=\"nav-link\">" + cmd.name +
                    "</a></li>\n";
        }
    }

    if (!meta.config.fields.isEmpty()) {
        html += "      <li class=\"nav-group\">Configuration</li>\n";
        html += "      <li><a href=\"#configuration\" class=\"nav-link\">Global Config</a></li>\n";
    }
    html += "    </ul>\n";
    html += "  </nav>\n";

    // Main Content
    html += "  <main id=\"content\">\n";

    // Overview Section
    html += "    <section id=\"overview\" class=\"content-section\">\n";
    html += "      <h1 class=\"page-title\">" + meta.info.name + "</h1>\n";

    if (!meta.info.description.isEmpty()) {
        html += "      <div class=\"description-box\">" + meta.info.description + "</div>\n";
    }

    html += "      <div class=\"meta-info\">\n";
    if (!meta.info.vendor.isEmpty()) {
        html += "        <div class=\"meta-item\"><strong>Vendor:</strong> " + meta.info.vendor +
                "</div>\n";
    }
    if (!meta.info.version.isEmpty()) {
        html += "        <div class=\"meta-item\"><strong>Version:</strong> " + meta.info.version +
                "</div>\n";
    }
    html += "      </div>\n";
    html += "    </section>\n";

    // Commands Section
    if (!meta.commands.isEmpty()) {
        html += "    <section id=\"commands\">\n";
        html += "      <h2>Commands</h2>\n";
        for (const auto& cmd : meta.commands) {
            html += "      <div id=\"cmd-" + cmd.name + "\" class=\"card command-card\">\n";
            html += "        <div class=\"card-header\">\n";
            html += "          <h3>" + cmd.name + "</h3>\n";
            if (!cmd.title.isEmpty()) {
                html += "          <span class=\"command-title\">" + cmd.title + "</span>\n";
            }
            html += "        </div>\n";

            html += "        <div class=\"card-body\">\n";
            if (!cmd.description.isEmpty()) {
                html += "          <p class=\"command-desc\">" + cmd.description + "</p>\n";
            }

            // Parameters
            if (!cmd.params.isEmpty()) {
                html += "          <h4>Parameters</h4>\n";
                html += "          <div class=\"table-wrapper\">\n";
                html += "            <table>\n";
                html += "              "
                        "<thead><tr><th>Name</th><th>Type</th><th>Required</th><th>Description</"
                        "th></tr></thead>\n";
                html += "              <tbody>\n";
                for (const auto& p : cmd.params) {
                    QString type = meta::fieldTypeToString(p.type);
                    QString req = p.required ? "<span class=\"badge req-yes\">Yes</span>"
                                             : "<span class=\"badge req-no\">No</span>";

                    // Format constraints
                    QString desc = p.description;
                    QString constraints = formatConstraintsMarkdown(
                        p.constraints); // Reusing markdown helper for simplicity or create HTML
                                        // specific one
                    if (!constraints.isEmpty()) {
                        desc += " <br><small class=\"constraints\">" + constraints + "</small>";
                    }

                    html += "              <tr>\n";
                    html += "                <td><code>" + p.name + "</code></td>\n";
                    html += "                <td><span class=\"type-badge " + type.toLower() +
                            "\">" + type + "</span></td>\n";
                    html += "                <td>" + req + "</td>\n";
                    html += "                <td>" + desc + "</td>\n";
                    html += "              </tr>\n";
                }
                html += "              </tbody>\n";
                html += "            </table>\n";
                html += "          </div>\n";
            } else {
                html += "          <p class=\"no-params\">No parameters required.</p>\n";
            }

            // Returns
            if (!cmd.returns.fields.isEmpty()) {
                html += "          <h4>Returns</h4>\n";
                html += "          <div class=\"table-wrapper\">\n";
                html += "            <table>\n";
                html += "              "
                        "<thead><tr><th>Name</th><th>Type</th><th>Description</th></tr></thead>\n";
                html += "              <tbody>\n";
                for (const auto& field : cmd.returns.fields) {
                    QString type = meta::fieldTypeToString(field.type);
                    html += "              <tr>\n";
                    html += "                <td><code>" + field.name + "</code></td>\n";
                    html += "                <td><span class=\"type-badge " + type.toLower() +
                            "\">" + type + "</span></td>\n";
                    html += "                <td>" + field.description + "</td>\n";
                    html += "              </tr>\n";
                }
                html += "              </tbody>\n";
                html += "            </table>\n";
                html += "          </div>\n";
            }

            html += "        </div>\n"; // End card-body
            html += "      </div>\n";   // End card
        }
        html += "    </section>\n";
    }

    // Configuration Section
    if (!meta.config.fields.isEmpty()) {
        html += "    <section id=\"configuration\" class=\"content-section\">\n";
        html += "      <h2>Configuration</h2>\n";
        html += "      <div class=\"card\">\n";
        html += "        <div class=\"card-body\">\n";
        html += "          <div class=\"table-wrapper\">\n";
        html += "            <table>\n";
        html += "              "
                "<thead><tr><th>Name</th><th>Type</th><th>Default</th><th>Description</th></tr></"
                "thead>\n";
        html += "              <tbody>\n";
        for (const auto& field : meta.config.fields) {
            QString type = meta::fieldTypeToString(field.type);
            QString def = field.defaultValue.isNull()
                              ? QString("-")
                              : QString(QJsonDocument(QJsonArray{field.defaultValue})
                                    .toJson(QJsonDocument::Compact)
                                    .mid(1));
            if (def.endsWith("]"))
                def.chop(1);

            html += "              <tr>\n";
            html += "                <td><code>" + field.name + "</code></td>\n";
            html += "                <td><span class=\"type-badge " + type.toLower() + "\">" +
                    type + "</span></td>\n";
            html += "                <td><code>" + def + "</code></td>\n";
            html += "                <td>" + field.description + "</td>\n";
            html += "              </tr>\n";
        }
        html += "              </tbody>\n";
        html += "            </table>\n";
        html += "          </div>\n";
        html += "        </div>\n";
        html += "      </div>\n";
        html += "    </section>\n";
    }

    html += "  </main>\n";
    html += "</div>\n"; // End layout

    // Script
    html += "<script>\n";
    html += generateHtmlScript();
    html += "</script>\n";

    html += "</body>\n</html>\n";
    return html;
}

QString DocGenerator::generateHtmlStyle() {
    return R"(
    :root {
        --primary-color: #0066cc;
        --sidebar-width: 280px;
        --bg-color: #f8f9fa;
        --text-color: #333;
        --border-color: #e9ecef;
        --code-bg: #f1f3f5;
        --nav-hover: #e7f5ff;
        --white: #ffffff;
    }

    * { box-sizing: border-box; }
    
    body { 
        font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
        margin: 0; 
        padding: 0; 
        background-color: var(--bg-color);
        color: var(--text-color);
        line-height: 1.6;
    }

    /* Layout */
    .layout { display: flex; min-height: 100vh; }
    
    /* Sidebar */
    #sidebar {
        width: var(--sidebar-width);
        background: var(--white);
        border-right: 1px solid var(--border-color);
        position: fixed;
        height: 100vh;
        overflow-y: auto;
        padding: 20px 0;
        transition: transform 0.3s ease;
        z-index: 1000;
    }
    
    .sidebar-header { padding: 0 24px 20px; border-bottom: 1px solid var(--border-color); }
    .sidebar-header h2 { margin: 0; font-size: 1.25rem; color: var(--primary-color); }
    
    .nav-links { list-style: none; padding: 0; margin: 20px 0; }
    .nav-group { 
        padding: 10px 24px 5px; 
        font-weight: 600; 
        font-size: 0.85rem; 
        text-transform: uppercase; 
        color: #868e96; 
        letter-spacing: 0.5px;
    }
    .nav-link { 
        display: block; 
        padding: 8px 24px; 
        color: var(--text-color); 
        text-decoration: none; 
        font-size: 0.95rem;
        border-left: 3px solid transparent;
    }
    .nav-link:hover { background-color: var(--nav-hover); color: var(--primary-color); }
    .nav-link.active { background-color: var(--nav-hover); color: var(--primary-color); border-left-color: var(--primary-color); }

    /* Main Content */
    #content {
        margin-left: var(--sidebar-width);
        flex: 1;
        padding: 40px;
        max-width: 1000px;
    }

    /* Mobile Header */
    #mobile-header { display: none; background: var(--white); padding: 15px; border-bottom: 1px solid var(--border-color); align-items: center; }
    #menu-toggle { background: none; border: none; font-size: 1.5rem; cursor: pointer; margin-right: 15px; }
    .mobile-title { font-weight: bold; font-size: 1.2rem; }

    /* Typography & Utilities */
    h1, h2, h3, h4 { margin-top: 0; color: #212529; }
    h1 { font-size: 2.5rem; margin-bottom: 20px; border-bottom: 2px solid var(--border-color); padding-bottom: 15px; }
    h2 { font-size: 1.75rem; margin-top: 40px; margin-bottom: 20px; }
    h3 { font-size: 1.25rem; margin-bottom: 10px; }
    
    code { font-family: "SFMono-Regular", Consolas, "Liberation Mono", Menlo, Courier, monospace; background: var(--code-bg); padding: 2px 5px; border-radius: 4px; font-size: 0.9em; color: #d63384; }
    
    .description-box { font-size: 1.1rem; color: #495057; margin-bottom: 30px; }
    .meta-info { display: flex; gap: 20px; font-size: 0.9rem; color: #6c757d; margin-bottom: 40px; }

    /* Cards */
    .card { background: var(--white); border: 1px solid var(--border-color); border-radius: 8px; margin-bottom: 24px; box-shadow: 0 2px 4px rgba(0,0,0,0.02); overflow: hidden; }
    .card-header { padding: 16px 24px; background-color: #f8f9fa; border-bottom: 1px solid var(--border-color); display: flex; align-items: center; justify-content: space-between; }
    .card-body { padding: 24px; }
    
    .command-title { color: #6c757d; font-size: 0.9rem; }
    .command-desc { margin-bottom: 20px; }
    
    /* Tables */
    .table-wrapper { overflow-x: auto; margin-top: 15px; }
    table { width: 100%; border-collapse: collapse; font-size: 0.95rem; }
    th { text-align: left; padding: 12px; background: #f8f9fa; border-bottom: 2px solid var(--border-color); color: #495057; font-weight: 600; }
    td { padding: 12px; border-bottom: 1px solid var(--border-color); vertical-align: top; }
    tr:last-child td { border-bottom: none; }

    /* Badges */
    .badge { display: inline-block; padding: 3px 8px; border-radius: 12px; font-size: 0.75rem; font-weight: 600; text-transform: uppercase; }
    .version { background: #e7f5ff; color: #0066cc; }
    .req-yes { background: #ffe3e3; color: #e03131; }
    .req-no { background: #e9ecef; color: #495057; }
    
    .type-badge { display: inline-block; padding: 2px 6px; border-radius: 4px; font-size: 0.8rem; font-weight: 500; font-family: monospace; background: #e9ecef; color: #495057; }
    .type-badge.string { background: #e3fafc; color: #0c8599; }
    .type-badge.int, .type-badge.double { background: #fff3bf; color: #f08c00; }
    .type-badge.bool { background: #d3f9d8; color: #2b8a3e; }

    .constraints { color: #868e96; display: block; margin-top: 4px; }
    .no-params { color: #868e96; font-style: italic; }

    /* Responsive */
    @media (max-width: 768px) {
        :root { --sidebar-width: 0px; }
        #sidebar { transform: translateX(-100%); width: 260px; box-shadow: 2px 0 8px rgba(0,0,0,0.1); }
        #sidebar.open { transform: translateX(0); }
        #content { margin-left: 0; padding: 20px; }
        #mobile-header { display: flex; }
    }
)";
}

QString DocGenerator::generateHtmlScript() {
    return R"(
    document.addEventListener('DOMContentLoaded', () => {
        const toggle = document.getElementById('menu-toggle');
        const sidebar = document.getElementById('sidebar');
        const content = document.getElementById('content');
        
        // Mobile Toggle
        toggle.addEventListener('click', () => {
            sidebar.classList.toggle('open');
        });

        // Close sidebar when clicking outside on mobile
        document.addEventListener('click', (e) => {
            if (window.innerWidth <= 768 && 
                sidebar.classList.contains('open') && 
                !sidebar.contains(e.target) && 
                e.target !== toggle) {
                sidebar.classList.remove('open');
            }
        });

        // Active Link Highlight on Scroll
        const sections = document.querySelectorAll('section');
        const navLinks = document.querySelectorAll('.nav-link');
        
        window.addEventListener('scroll', () => {
            let current = '';
            sections.forEach(section => {
                const sectionTop = section.offsetTop;
                if (scrollY >= sectionTop - 100) {
                    current = section.getAttribute('id');
                }
            });

            navLinks.forEach(link => {
                link.classList.remove('active');
                if (link.getAttribute('href').includes(current)) {
                    link.classList.add('active');
                }
            });
        });
    });
)";
}

} // namespace stdiolink
