#include "command_history.h"

CommandHistory::CommandHistory(QObject *parent)
    : QObject(parent)
{
}

void CommandHistory::addEntry(const HistoryEntry &entry)
{
    if (m_entries.size() >= m_maxEntries) {
        m_entries.removeFirst();
    }
    m_entries.append(entry);
    emit entryAdded(entry);
}

void CommandHistory::clear()
{
    m_entries.clear();
    emit cleared();
}
