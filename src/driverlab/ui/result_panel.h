#ifndef RESULT_PANEL_H
#define RESULT_PANEL_H

#include <QWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QTimer>
#include <stdiolink/protocol/jsonl_types.h>
#include "models/command_history.h"
#include "widgets/json_tree_widget.h"

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
    void flushPendingMessages();

private:
    void appendToRawJson(const stdiolink::Message &msg);

    QTabWidget *m_tabs;
    QTableWidget *m_eventTable;
    JsonTreeWidget *m_resultTree;
    QPlainTextEdit *m_rawJson;
    QListWidget *m_historyList;

    // 批量更新优化
    QTimer *m_flushTimer;
    QVector<stdiolink::Message> m_pendingMessages;
    QJsonValue m_lastResultPayload;
    static constexpr int kFlushIntervalMs = 50;
    static constexpr int kMaxEventRows = 1000;
};

#endif // RESULT_PANEL_H
