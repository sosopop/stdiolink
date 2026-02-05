#include "command_list.h"

#include <QVBoxLayout>

CommandList::CommandList(QWidget *parent)
    : QWidget(parent)
    , m_list(new QListWidget(this))
    , m_searchEdit(new QLineEdit(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5); // 增加一点间距

    m_searchEdit->setPlaceholderText(tr("搜索命令..."));
    m_searchEdit->setClearButtonEnabled(true);
    layout->addWidget(m_searchEdit);
    layout->addWidget(m_list);

    connect(m_list, &QListWidget::itemClicked,
            this, &CommandList::onItemClicked);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &CommandList::onFilterChanged);
}

void CommandList::setCommands(const QVector<stdiolink::meta::CommandMeta> &commands)
{
    m_commands = commands;
    updateList();
}

void CommandList::clear()
{
    m_list->clear();
    m_commands.clear();
}

const stdiolink::meta::CommandMeta *CommandList::currentCommand() const
{
    auto *item = m_list->currentItem();
    if (!item) return nullptr;

    QString cmdName = item->data(Qt::UserRole).toString();
    for (const auto &cmd : m_commands) {
        if (cmd.name == cmdName) {
            return &cmd;
        }
    }
    return nullptr;
}

void CommandList::onItemClicked(QListWidgetItem *item)
{
    Q_UNUSED(item)
    emit commandSelected(currentCommand());
}

void CommandList::onFilterChanged(const QString &text)
{
    Q_UNUSED(text)
    updateList();
}

void CommandList::updateList()
{
    m_list->clear();
    QString filter = m_searchEdit->text().toLower();

    for (const auto &cmd : m_commands) {
        QString text = cmd.name;
        if (!cmd.title.isEmpty()) {
            text += " - " + cmd.title;
        }

        if (filter.isEmpty() || text.toLower().contains(filter)) {
            auto *item = new QListWidgetItem(text);
            item->setData(Qt::UserRole, cmd.name);
            item->setToolTip(cmd.description);
            m_list->addItem(item);
        }
    }
}