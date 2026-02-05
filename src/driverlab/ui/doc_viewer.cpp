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

    if (!cmd->title.isEmpty()) {
        md += "**" + cmd->title + "**\n\n";
    }

    if (!cmd->description.isEmpty()) {
        md += cmd->description + "\n\n";
    }

    // 参数部分
    if (!cmd->params.isEmpty()) {
        md += "### 参数\n\n";
        md += "| 名称 | 类型 | 必填 | 说明 |\n";
        md += "|------|------|------|------|\n";

        for (const auto &p : cmd->params) {
            QString type = stdiolink::meta::fieldTypeToString(p.type);
            md += QString("| %1 | %2 | %3 | %4 |\n")
                .arg(p.name, type, p.required ? "是" : "否", p.description);
        }
        md += "\n";
    }

    // 返回值部分
    if (!cmd->returns.fields.isEmpty()) {
        md += "### 返回值\n\n";
        if (!cmd->returns.description.isEmpty()) {
            md += cmd->returns.description + "\n\n";
        }
        md += "| 名称 | 类型 | 说明 |\n";
        md += "|------|------|------|\n";

        for (const auto &f : cmd->returns.fields) {
            QString type = stdiolink::meta::fieldTypeToString(f.type);
            md += QString("| %1 | %2 | %3 |\n")
                .arg(f.name, type, f.description);
        }
        md += "\n";
    }

    return md;
}
