#pragma once

#include <QDateTime>
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
    EventStreamConnection(QHttpServerResponder&& responder,
                          const QSet<QString>& filters,
                          const QString& allowedOrigin,
                          QObject* parent = nullptr);

    void beginStream();
    void sendEvent(const ServerEvent& event);
    void sendHeartbeat();
    void close();
    bool matchesFilter(const QString& eventType) const;

    QDateTime createdAt() const;
    QDateTime lastSendAt() const;

    static bool matchesFilter(const QSet<QString>& filters, const QString& eventType);

signals:
    void disconnected();

private:
    QHttpServerResponder m_responder;
    QSet<QString> m_filters;
    QString m_allowedOrigin;
    bool m_streamOpen = false;
    QDateTime m_createdAt;
    QDateTime m_lastSendAt;
};

class EventStreamHandler : public QObject {
    Q_OBJECT
public:
    explicit EventStreamHandler(EventBus* bus,
                                const QString& allowedOrigin = QStringLiteral("*"),
                                QObject* parent = nullptr);
    ~EventStreamHandler() override;

    void addConnection(QHttpServerResponder&& responder,
                       const QSet<QString>& filters);
    void closeAllConnections();

    int activeConnectionCount() const;

    static constexpr int kMaxSseConnections = 32;
    static constexpr int kHeartbeatIntervalMs = 30000;
    static constexpr int kConnectionTimeoutMs = kHeartbeatIntervalMs * 2;

private slots:
    void onEventPublished(const ServerEvent& event);
    void onHeartbeat();
    void onConnectionDisconnected();

private:
    void removeConnection(EventStreamConnection* conn);
    void evictOldestConnection();
    void sweepStaleConnections();

    EventBus* m_bus;
    QString m_allowedOrigin;
    QVector<EventStreamConnection*> m_connections;
    QTimer m_heartbeatTimer;
};

} // namespace stdiolink_server
