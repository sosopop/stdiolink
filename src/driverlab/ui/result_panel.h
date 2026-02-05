#ifndef RESULT_PANEL_H
#define RESULT_PANEL_H

#include <QWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QListWidget>
#include <stdiolink/protocol/jsonl_types.h>
#include "models/command_history.h"

class ResultPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ResultPanel(QWidget *parent = nullptr);

    void addMessage(const stdiolink::Message &msg);
    void addHistoryEntry(const HistoryEntry &entry);
    void clear();

private slots:
    void onEventSelected();

private:
    QTabWidget *m_tabs;
    QTableWidget *m_eventTable;
    QPlainTextEdit *m_rawJson;
    QListWidget *m_historyList;
};

#endif // RESULT_PANEL_H
