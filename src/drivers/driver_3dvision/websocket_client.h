#pragma once

#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QSet>
#include <QJsonObject>

class WebSocketClient : public QObject {
    Q_OBJECT
public:
    explicit WebSocketClient(QObject* parent = nullptr);
    ~WebSocketClient();

    bool connectToServer(const QString& url);
    void disconnect();
    bool isConnected() const;

    void subscribe(const QString& topic);
    void unsubscribe(const QString& topic);
    QSet<QString> subscriptions() const { return m_subscriptions; }

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
    bool m_connected;
};
