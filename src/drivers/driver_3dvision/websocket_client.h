#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QWebSocket>

class WebSocketClient : public QObject {
    Q_OBJECT
public:
    explicit WebSocketClient(QObject* parent = nullptr);
    ~WebSocketClient() override;

    bool connectToServer(const QString& url);
    void disconnect();
    bool isConnected() const;

    void subscribe(const QString& topic);
    void unsubscribe(const QString& topic);
    QSet<QString> subscriptions() const { return m_subscriptions; }
    QString currentUrl() const { return m_currentUrl; }

    void sendPing();

signals:
    void connected();
    void disconnected();
    void eventReceived(const QString& eventName, const QJsonObject& data);
    void error(const QString& message);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString& message);
    void onError(QAbstractSocket::SocketError error);
    void onHeartbeat();

private:
    void send(const QJsonObject& msg);

    QWebSocket* m_socket;
    QTimer* m_heartbeatTimer;
    QSet<QString> m_subscriptions;
    QString m_currentUrl;
    bool m_connected = false;
};
