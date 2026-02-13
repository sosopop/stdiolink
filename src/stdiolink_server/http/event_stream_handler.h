#pragma once

#include <QHttpServerResponder>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVector>

#include "event_bus.h"

namespace stdiolink_server {

class EventStreamConnection : public QObject {
    Q_OBJECT
public:
    EventStreamConnection(QHttpServerResponder&& responder, const QSet<QString>& filters,
                          QObject* parent = nullptr);
    ~EventStreamConnection();

    void beginStream();
    void sendEvent(const ServerEvent& event);
    bool matchesFilter(const QString& eventType) const;
    bool isOpen() const { return m_open; }

private:
    void sendHeartbeat();

    QHttpServerResponder m_responder;
    QSet<QString> m_filters;
    QTimer m_heartbeatTimer;
    bool m_open = false;
};

class EventStreamHandler : public QObject {
    Q_OBJECT
public:
    explicit EventStreamHandler(EventBus* bus, QObject* parent = nullptr);
    ~EventStreamHandler() override;

    /// Handle an incoming SSE request — takes ownership of the responder
    void handleRequest(const QSet<QString>& filters, QHttpServerResponder&& responder);
    void closeAllConnections();

    int activeConnectionCount() const { return m_connections.size(); }

    static constexpr int kMaxSseConnections = 32;

private slots:
    void onEventPublished(const ServerEvent& event);

private:
    void evictOldestConnection();

    EventBus* m_bus;
    QVector<EventStreamConnection*> m_connections;
};

} // namespace stdiolink_server
