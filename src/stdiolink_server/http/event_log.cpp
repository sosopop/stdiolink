#include "event_log.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <spdlog/sinks/rotating_file_sink.h>

namespace stdiolink_server {

EventLog::EventLog(const QString& logPath, EventBus* bus,
                   qint64 maxBytes, int maxFiles, QObject* parent)
    : QObject(parent), m_logPath(logPath)
{
    try {
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logPath.toStdString(),
            static_cast<size_t>(maxBytes),
            static_cast<size_t>(maxFiles));

        m_logger = std::make_shared<spdlog::logger>("event_log", sink);
        m_logger->set_pattern("%v");
        m_logger->set_level(spdlog::level::info);
        m_logger->flush_on(spdlog::level::info);
    } catch (const spdlog::spdlog_ex& ex) {
        qWarning("EventLog: failed to create logger: %s", ex.what());
    }

    connect(bus, &EventBus::eventPublished,
            this, &EventLog::onEventPublished);
}

EventLog::~EventLog() {
    if (m_logger) {
        spdlog::drop(m_logger->name());
    }
}

void EventLog::onEventPublished(const ServerEvent& event) {
    if (!m_logger) return;
    QJsonObject record;
    record["type"] = event.type;
    record["data"] = event.data;
    record["ts"] = event.timestamp.toString(Qt::ISODateWithMs);
    const QByteArray json = QJsonDocument(record).toJson(QJsonDocument::Compact);
    m_logger->info("{}", json.constData());
}

QJsonArray EventLog::query(int limit,
                           const QString& typePrefix,
                           const QString& projectId) const {
    QFile file(m_logPath);
    if (!file.open(QIODevice::ReadOnly)) return {};

    constexpr qint64 kMaxReadBytes = 4 * 1024 * 1024;
    const qint64 fileSize = file.size();
    const qint64 startPos = qMax(qint64(0), fileSize - kMaxReadBytes);
    file.seek(startPos);
    const QByteArray data = file.readAll();

    QJsonArray results;
    int pos = data.size();
    while (pos > 0 && results.size() < limit) {
        int lineEnd = pos;
        pos--;
        while (pos > 0 && data[pos - 1] != '\n') pos--;
        const QByteArray line = data.mid(pos, lineEnd - pos).trimmed();
        if (line.isEmpty()) continue;

        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) continue;
        const QJsonObject obj = doc.object();

        if (!typePrefix.isEmpty()) {
            if (!obj.value("type").toString().startsWith(typePrefix)) continue;
        }
        if (!projectId.isEmpty()) {
            if (obj.value("data").toObject().value("projectId").toString()
                != projectId) continue;
        }
        results.append(obj);
    }
    return results;
}

} // namespace stdiolink_server
