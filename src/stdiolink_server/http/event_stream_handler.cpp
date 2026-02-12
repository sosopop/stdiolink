#include "event_stream_handler.h"

#include <QHttpHeaders>
#include <QJsonDocument>

#include "cors_middleware.h"

namespace stdiolink_server {

// ---- EventStreamConnection ----

EventStreamConnection::EventStreamConnection(QHttpServerResponder&& responder,
                                             const QSet<QString>& filters,
                                             const QString& allowedOrigin,
                                             QObject* parent)
    : QObject(parent)
    , m_responder(std::move(responder))
    , m_filters(filters)
    , m_allowedOrigin(allowedOrigin) {
}

void EventStreamConnection::beginStream() {
    QHttpHeaders headers = CorsMiddleware::buildCorsHeaders(m_allowedOrigin);
    headers.append(QHttpHeaders::WellKnownHeader::ContentType, "text/event-stream");
    headers.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-cache");
    headers.append("X-Accel-Buffering", "no");
    m_responder.writeBeginChunked(headers);
    m_streamOpen = true;
}

void EventStreamConnection::sendEvent(const ServerEvent& event) {
    QByteArray chunk;
    chunk.append("event: ");
    chunk.append(event.type.toUtf8());
    chunk.append('\n');
    chunk.append("data: ");
    chunk.append(QJsonDocument(event.data).toJson(QJsonDocument::Compact));
    chunk.append("\n\n");
    m_responder.writeChunk(chunk);
}

void EventStreamConnection::sendHeartbeat() {
    m_responder.writeChunk(": heartbeat\n\n");
}

void EventStreamConnection::close() {
    if (!m_streamOpen) {
        return;
    }
    m_responder.writeEndChunked(QByteArray());
    m_streamOpen = false;
}

bool EventStreamConnection::matchesFilter(const QString& eventType) const {
    return matchesFilter(m_filters, eventType);
}

bool EventStreamConnection::matchesFilter(const QSet<QString>& filters,
                                           const QString& eventType) {
    if (filters.isEmpty()) {
        return true;
    }
    for (const QString& prefix : filters) {
        if (eventType.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

// ---- EventStreamHandler ----

EventStreamHandler::EventStreamHandler(EventBus* bus,
                                       const QString& allowedOrigin,
                                       QObject* parent)
    : QObject(parent)
    , m_bus(bus)
    , m_allowedOrigin(allowedOrigin) {
    connect(m_bus, &EventBus::eventPublished,
            this, &EventStreamHandler::onEventPublished);

    m_heartbeatTimer.setInterval(kHeartbeatIntervalMs);
    connect(&m_heartbeatTimer, &QTimer::timeout,
            this, &EventStreamHandler::onHeartbeat);
    m_heartbeatTimer.start();
}

void EventStreamHandler::addConnection(QHttpServerResponder&& responder,
                                        const QSet<QString>& filters) {
    if (m_connections.size() >= kMaxSseConnections) {
        evictOldestConnection();
    }

    auto* conn = new EventStreamConnection(std::move(responder),
                                           filters,
                                           m_allowedOrigin,
                                           this);
    m_connections.append(conn);
    conn->beginStream();
}

int EventStreamHandler::activeConnectionCount() const {
    return m_connections.size();
}

void EventStreamHandler::onEventPublished(const ServerEvent& event) {
    for (auto* conn : m_connections) {
        if (conn->matchesFilter(event.type)) {
            conn->sendEvent(event);
        }
    }
}

void EventStreamHandler::onHeartbeat() {
    for (auto* conn : m_connections) {
        conn->sendHeartbeat();
    }
}

void EventStreamHandler::removeConnection(EventStreamConnection* conn) {
    if (!conn) {
        return;
    }
    m_connections.removeOne(conn);
    conn->deleteLater();
}

void EventStreamHandler::evictOldestConnection() {
    if (m_connections.isEmpty()) {
        return;
    }

    EventStreamConnection* oldest = m_connections.front();
    oldest->close();
    removeConnection(oldest);
}

} // namespace stdiolink_server
