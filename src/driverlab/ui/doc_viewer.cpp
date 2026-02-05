#include "doc_viewer.h"

#include <QVBoxLayout>

DocViewer::DocViewer(QWidget *parent)
    : QWidget(parent)
    , m_browser(new QTextBrowser(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_browser);

    m_browser->setOpenExternalLinks(true);
}

void DocViewer::setCommand(const stdiolink::meta::CommandMeta *cmd)
{
    if (!cmd) {
        clear();
        return;
    }
    m_browser->setMarkdown(generateMarkdown(cmd));
}

void DocViewer::clear()
{
    m_browser->clear();
}

QString DocViewer::generateMarkdown(const stdiolink::meta::CommandMeta *cmd)
{
    QString md;
    md += "## " + cmd->name + "\n\n";

    if (!cmd->description.isEmpty()) {
        md += cmd->description + "\n\n";
    }

    if (!cmd->params.isEmpty()) {
        md += "### Parameters\n\n";
        md += "| Name | Type | Required | Description |\n";
        md += "|------|------|----------|-------------|\n";

        for (const auto &p : cmd->params) {
            QString type;
            switch (p.type) {
            case stdiolink::meta::FieldType::String: type = "string"; break;
            case stdiolink::meta::FieldType::Int: type = "int"; break;
            case stdiolink::meta::FieldType::Double: type = "double"; break;
            case stdiolink::meta::FieldType::Bool: type = "bool"; break;
            case stdiolink::meta::FieldType::Array: type = "array"; break;
            case stdiolink::meta::FieldType::Object: type = "object"; break;
            default: type = "any"; break;
            }
            md += QString("| %1 | %2 | %3 | %4 |\n")
                .arg(p.name, type, p.required ? "Yes" : "No", p.description);
        }
        md += "\n";
    }

    return md;
}
