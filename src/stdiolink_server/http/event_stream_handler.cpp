#include "event_stream_handler.h"

#include <QHttpHeaders>
#include <QJsonDocument>

namespace stdiolink_server {

// ── EventStreamConnection ───────────────────────────────────────────

EventStreamConnection::EventStreamConnection(QHttpServerResponder&& responder,
                                             const QSet<QString>& filters, QObject* parent)
    : QObject(parent), m_responder(std::move(responder)), m_filters(filters) {
    // Heartbeat every 30 seconds
    m_heartbeatTimer.setInterval(30000);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &EventStreamConnection::sendHeartbeat);
}

EventStreamConnection::~EventStreamConnection() {
    if (m_open) {
        m_heartbeatTimer.stop();
        m_open = false;
    }
}

void EventStreamConnection::beginStream() {
    if (m_open) {
        return;
    }

    QHttpHeaders headers;
    headers.append(QHttpHeaders::WellKnownHeader::ContentType, "text/event-stream");
    headers.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-cache");
    headers.append(QHttpHeaders::WellKnownHeader::Connection, "keep-alive");

    m_responder.writeBeginChunked(headers, QHttpServerResponder::StatusCode::Ok);
    m_open = true;
    m_heartbeatTimer.start();
}

void EventStreamConnection::sendEvent(const ServerEvent& event) {
    if (!m_open)
        return;

    QByteArray chunk;
    chunk.append("event: ");
    chunk.append(event.type.toUtf8());
    chunk.append('\n');
    chunk.append("data: ");
    chunk.append(QJsonDocument(event.data).toJson(QJsonDocument::Compact));
    chunk.append("\n\n");

    m_responder.writeChunk(chunk);
}

bool EventStreamConnection::matchesFilter(const QString& eventType) const {
    if (m_filters.isEmpty())
        return true;

    for (const auto& prefix : m_filters) {
        if (eventType.startsWith(prefix))
            return true;
    }
    return false;
}

void EventStreamConnection::sendHeartbeat() {
    if (!m_open)
        return;

    m_responder.writeChunk(QByteArray(": heartbeat\n\n"));
}

// ── EventStreamHandler ──────────────────────────────────────────────

EventStreamHandler::EventStreamHandler(EventBus* bus, QObject* parent)
    : QObject(parent), m_bus(bus) {
    connect(m_bus, &EventBus::eventPublished, this, &EventStreamHandler::onEventPublished);
}

EventStreamHandler::~EventStreamHandler() {
    closeAllConnections();
}

void EventStreamHandler::handleRequest(const QSet<QString>& filters,
                                       QHttpServerResponder&& responder) {
    if (m_connections.size() >= kMaxSseConnections) {
        // SSE disconnect is not surfaced to this layer; cap by replacing oldest.
        evictOldestConnection();
    }

    auto* conn = new EventStreamConnection(std::move(responder), filters, this);
    m_connections.append(conn);
    conn->beginStream();
}

void EventStreamHandler::onEventPublished(const ServerEvent& event) {
    for (auto* conn : m_connections) {
        if (conn->isOpen() && conn->matchesFilter(event.type)) {
            conn->sendEvent(event);
        }
    }
}

void EventStreamHandler::closeAllConnections() {
    while (!m_connections.isEmpty()) {
        EventStreamConnection* conn = m_connections.takeLast();
        delete conn;
    }
}

void EventStreamHandler::evictOldestConnection() {
    if (m_connections.isEmpty()) {
        return;
    }
    EventStreamConnection* oldest = m_connections.takeFirst();
    delete oldest;
}

} // namespace stdiolink_server
