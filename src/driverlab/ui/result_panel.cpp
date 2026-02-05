#include "result_panel.h"

#include <QVBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTime>
#include <QHeaderView>

ResultPanel::ResultPanel(QWidget *parent)
    : QWidget(parent)
    , m_tabs(new QTabWidget(this))
    , m_eventTable(new QTableWidget(this))
    , m_resultTree(new JsonTreeWidget(this))
    , m_rawJson(new QPlainTextEdit(this))
    , m_historyList(new QListWidget(this))
    , m_flushTimer(new QTimer(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tabs);

    // Setup Event Table
    m_eventTable->setColumnCount(3);
    m_eventTable->setHorizontalHeaderLabels({tr("时间"), tr("状态"), tr("内容")});
    m_eventTable->horizontalHeader()->setStretchLastSection(true);
    m_eventTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_eventTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_eventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_eventTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_eventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_eventTable->setAlternatingRowColors(true);

    connect(m_eventTable, &QTableWidget::itemSelectionChanged, this, &ResultPanel::onEventSelected);

    m_tabs->addTab(m_eventTable, tr("事件流"));
    m_tabs->addTab(m_resultTree, tr("结果树"));
    m_tabs->addTab(m_rawJson, tr("原始 JSON"));
    m_tabs->addTab(m_historyList, tr("执行历史"));

    m_rawJson->setReadOnly(true);
    m_rawJson->setMaximumBlockCount(10000); // 限制最大行数，防止内存爆炸

    // 设置等宽字体给 Raw JSON
#ifdef Q_OS_WIN
    m_rawJson->setFont(QFont("Consolas", 9));
#else
    m_rawJson->setFont(QFont("Monospace", 9));
#endif

    // 批量刷新定时器
    m_flushTimer->setSingleShot(true);
    connect(m_flushTimer, &QTimer::timeout, this, &ResultPanel::flushPendingMessages);
}

void ResultPanel::addMessage(const stdiolink::Message &msg)
{
    // 将消息加入待处理队列
    m_pendingMessages.append(msg);

    // 记录最后的结果 payload（用于结果树）
    if (msg.status == "done" || msg.status == "event") {
        m_lastResultPayload = msg.payload;
    }

    // 启动延迟刷新定时器
    if (!m_flushTimer->isActive()) {
        m_flushTimer->start(kFlushIntervalMs);
    }

    // 如果是 done 状态，立即刷新
    if (msg.status == "done" || msg.status == "error") {
        m_flushTimer->stop();
        flushPendingMessages();
    }
}

void ResultPanel::addHistoryEntry(const HistoryEntry &entry)
{
    QString text = QString("[%1] %2 (%3ms) - Exit: %4")
        .arg(entry.timestamp.toString("HH:mm:ss"))
        .arg(entry.command)
        .arg(entry.durationMs)
        .arg(entry.exitCode);
    
    auto *item = new QListWidgetItem(text);
    if (entry.exitCode != 0) {
        item->setForeground(Qt::red);
    }
    m_historyList->addItem(item);
    m_historyList->scrollToBottom();
}

void ResultPanel::clear()
{
    m_flushTimer->stop();
    m_pendingMessages.clear();
    m_lastResultPayload = QJsonValue();
    m_eventTable->setRowCount(0);
    m_resultTree->clear();
    m_rawJson->clear();
}

void ResultPanel::onEventSelected()
{
    // 如果需要，可以在这里实现点击表格行高亮对应的 Raw JSON
    // 目前保持简单
}

void ResultPanel::flushPendingMessages()
{
    if (m_pendingMessages.isEmpty()) return;

    // 禁用更新以提高性能
    m_eventTable->setUpdatesEnabled(false);

    // 限制最大行数，删除旧行
    int rowsToAdd = m_pendingMessages.size();
    int currentRows = m_eventTable->rowCount();
    if (currentRows + rowsToAdd > kMaxEventRows) {
        int rowsToRemove = currentRows + rowsToAdd - kMaxEventRows;
        for (int i = 0; i < rowsToRemove && m_eventTable->rowCount() > 0; ++i) {
            m_eventTable->removeRow(0);
        }
    }

    // 批量插入行
    for (const auto &msg : m_pendingMessages) {
        QString time = QTime::currentTime().toString("HH:mm:ss.zzz");
        QString status = msg.status;
        QString content;

        if (!msg.payload.isNull()) {
            QJsonDocument doc;
            if (msg.payload.isObject()) {
                doc = QJsonDocument(msg.payload.toObject());
            } else if (msg.payload.isArray()) {
                doc = QJsonDocument(msg.payload.toArray());
            }
            content = doc.toJson(QJsonDocument::Compact);
        }

        int row = m_eventTable->rowCount();
        m_eventTable->insertRow(row);

        auto *timeItem = new QTableWidgetItem(time);
        auto *statusItem = new QTableWidgetItem(status);
        auto *contentItem = new QTableWidgetItem(content);

        // 设置状态颜色
        QColor statusColor = Qt::black;
        QColor bgColor = Qt::white;
        if (status == "error") {
            statusColor = Qt::red;
            bgColor = QColor(255, 240, 240);
        } else if (status == "done") {
            statusColor = QColor(0, 128, 0);
            bgColor = QColor(240, 255, 240);
        } else if (status == "event") {
            statusColor = QColor(0, 0, 255);
        }

        statusItem->setForeground(statusColor);
        timeItem->setBackground(bgColor);
        statusItem->setBackground(bgColor);
        contentItem->setBackground(bgColor);
        contentItem->setData(Qt::UserRole, content);

        m_eventTable->setItem(row, 0, timeItem);
        m_eventTable->setItem(row, 1, statusItem);
        m_eventTable->setItem(row, 2, contentItem);

        // 追加到 Raw JSON
        appendToRawJson(msg);
    }

    // 重新启用更新
    m_eventTable->setUpdatesEnabled(true);
    m_eventTable->scrollToBottom();

    // 更新结果树（只更新最后一个结果）
    if (!m_lastResultPayload.isNull()) {
        m_resultTree->setJson(m_lastResultPayload);
    }

    m_pendingMessages.clear();
}

void ResultPanel::appendToRawJson(const stdiolink::Message &msg)
{
    QJsonObject fullMsg;
    fullMsg["status"] = msg.status;
    fullMsg["code"] = msg.code;
    fullMsg["data"] = msg.payload;

    QString json = QJsonDocument(fullMsg).toJson(QJsonDocument::Indented);
    m_rawJson->appendPlainText(json);
}