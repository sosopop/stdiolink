#include "websocket_client.h"

#include <QJsonDocument>

WebSocketClient::WebSocketClient(QObject* parent)
    : QObject(parent)
    , m_socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , m_heartbeatTimer(new QTimer(this))
    , m_connected(false)
{
    connect(m_socket, &QWebSocket::connected,
            this, &WebSocketClient::onConnected);
    connect(m_socket, &QWebSocket::disconnected,
            this, &WebSocketClient::onDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived,
            this, &WebSocketClient::onTextMessageReceived);
    connect(m_socket, &QWebSocket::errorOccurred,
            this, &WebSocketClient::onError);
    connect(m_heartbeatTimer, &QTimer::timeout,
            this, &WebSocketClient::onHeartbeat);
}

WebSocketClient::~WebSocketClient()
{
    disconnect();
}

bool WebSocketClient::connectToServer(const QString& url)
{
    if (m_connected) {
        return true;
    }
    m_socket->open(QUrl(url));
    return true;
}

void WebSocketClient::disconnect()
{
    m_heartbeatTimer->stop();
    m_subscriptions.clear();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }
    m_connected = false;
}

bool WebSocketClient::isConnected() const
{
    return m_connected;
}

void WebSocketClient::subscribe(const QString& topic)
{
    if (!m_connected) return;
    QJsonObject msg;
    msg["type"] = "sub";
    msg["topic"] = topic;
    send(msg);
    m_subscriptions.insert(topic);
}

void WebSocketClient::unsubscribe(const QString& topic)
{
    if (!m_connected) return;
    QJsonObject msg;
    msg["type"] = "unsub";
    msg["topic"] = topic;
    send(msg);
    m_subscriptions.remove(topic);
}

void WebSocketClient::sendPing()
{
    if (!m_connected) return;
    QJsonObject msg;
    msg["type"] = "ping";
    send(msg);
}

void WebSocketClient::send(const QJsonObject& msg)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->sendTextMessage(
            QJsonDocument(msg).toJson(QJsonDocument::Compact));
    }
}

void WebSocketClient::onConnected()
{
    m_connected = true;
    m_heartbeatTimer->start(10000); // 10 seconds heartbeat
    emit connected();
}

void WebSocketClient::onDisconnected()
{
    m_connected = false;
    m_heartbeatTimer->stop();
    emit disconnected();
}

void WebSocketClient::onTextMessageReceived(const QString& message)
{
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return;

    QJsonObject json = doc.object();
    QString type = json["type"].toString();

    if (type == "pong") {
        // Heartbeat response, ignore
        return;
    }

    if (type == "pub") {
        QString msgContent = json["message"].toString();
        QJsonDocument eventDoc = QJsonDocument::fromJson(msgContent.toUtf8());
        if (eventDoc.isObject()) {
            QJsonObject eventObj = eventDoc.object();
            QString eventName = eventObj["event"].toString();
            emit eventReceived(eventName, eventObj);
        }
    }
}

void WebSocketClient::onError(QAbstractSocket::SocketError err)
{
    Q_UNUSED(err)
    emit error(m_socket->errorString());
}

void WebSocketClient::onHeartbeat()
{
    sendPing();
}
