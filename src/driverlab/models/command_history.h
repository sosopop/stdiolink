#ifndef COMMAND_HISTORY_H
#define COMMAND_HISTORY_H

#include <QObject>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QVector>

struct HistoryEntry {
    QString command;
    QJsonObject params;
    QDateTime timestamp;
    int exitCode = 0;
    QJsonValue result;
    QString errorText;
    qint64 durationMs = 0;
};

class CommandHistory : public QObject
{
    Q_OBJECT

public:
    explicit CommandHistory(QObject *parent = nullptr);

    void addEntry(const HistoryEntry &entry);
    void clear();

    int count() const { return m_entries.size(); }
    const HistoryEntry &at(int index) const { return m_entries.at(index); }
    const QVector<HistoryEntry> &entries() const { return m_entries; }

signals:
    void entryAdded(const HistoryEntry &entry);
    void cleared();

private:
    QVector<HistoryEntry> m_entries;
    int m_maxEntries = 100;
};

#endif // COMMAND_HISTORY_H
