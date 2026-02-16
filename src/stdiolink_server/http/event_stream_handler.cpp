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
    , m_allowedOrigin(allowedOrigin)
    , m_createdAt(QDateTime::currentDateTimeUtc())
    , m_lastSendAt(m_createdAt) {
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
    m_lastSendAt = QDateTime::currentDateTimeUtc();
}

void EventStreamConnection::sendHeartbeat() {
    m_responder.writeChunk(": heartbeat\n\n");
    // Do NOT update m_lastSendAt here. Only sendEvent() updates it,
    // so that sweepStaleConnections() can detect connections where no
    // real data has been delivered for longer than the timeout window.
    // If writeChunk() silently fails on a dead socket, the stale
    // lastSendAt will eventually trigger eviction.
}

void EventStreamConnection::close() {
    if (!m_streamOpen) {
        return;
    }
    // Do NOT call writeEndChunked() here. Passing an empty QByteArray
    // triggers Qt's "Chunk must have length > 0" warning, and for SSE
    // connections being forcefully closed the proper termination is a
    // TCP-level close — the client detects the disconnect and reconnects.
    // The QHttpServerResponder destructor handles the underlying cleanup.
    m_streamOpen = false;
}

QDateTime EventStreamConnection::createdAt() const {
    return m_createdAt;
}

QDateTime EventStreamConnection::lastSendAt() const {
    return m_lastSendAt;
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

EventStreamHandler::~EventStreamHandler() {
    closeAllConnections();
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
    connect(conn, &EventStreamConnection::disconnected,
            this, &EventStreamHandler::onConnectionDisconnected);
    m_connections.append(conn);
    conn->beginStream();
}

void EventStreamHandler::closeAllConnections() {
    m_heartbeatTimer.stop();
    const QVector<EventStreamConnection*> connections = m_connections;
    m_connections.clear();
    for (auto* conn : connections) {
        if (!conn) {
            continue;
        }
        conn->close();
        // Use direct delete here: during destruction the event loop may no longer
        // process deferred deletions, and the underlying TCP socket / QHttpServer
        // may already be torn down.
        delete conn;
    }
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
    // Sweep first: evict connections that haven't received any real
    // event data within the timeout window, then send heartbeats
    // only to the surviving (presumably healthy) connections.
    sweepStaleConnections();
    for (auto* conn : m_connections) {
        conn->sendHeartbeat();
    }
}

void EventStreamHandler::onConnectionDisconnected() {
    auto* conn = qobject_cast<EventStreamConnection*>(sender());
    if (conn) {
        removeConnection(conn);
    }
}

void EventStreamHandler::sweepStaleConnections() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVector<EventStreamConnection*> stale;
    for (auto* conn : m_connections) {
        const qint64 elapsed = conn->lastSendAt().msecsTo(now);
        if (elapsed > kConnectionTimeoutMs) {
            stale.append(conn);
        }
    }
    for (auto* conn : stale) {
        conn->close();
        emit conn->disconnected();
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
