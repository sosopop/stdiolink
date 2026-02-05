#ifndef COMMAND_LIST_H
#define COMMAND_LIST_H

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <stdiolink/protocol/meta_types.h>

class CommandList : public QWidget
{
    Q_OBJECT

public:
    explicit CommandList(QWidget *parent = nullptr);

    void setCommands(const QVector<stdiolink::meta::CommandMeta> &commands);
    void clear();

    const stdiolink::meta::CommandMeta *currentCommand() const;

signals:
    void commandSelected(const stdiolink::meta::CommandMeta *cmd);

private slots:
    void onItemClicked(QListWidgetItem *item);
    void onFilterChanged(const QString &text);

private:
    void updateList();

    QListWidget *m_list;
    QLineEdit *m_searchEdit;
    QVector<stdiolink::meta::CommandMeta> m_commands;
};

#endif // COMMAND_LIST_H