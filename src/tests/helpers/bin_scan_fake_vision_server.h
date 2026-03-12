#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QQueue>
#include <QSet>
#include <QTcpSocket>
#include <QTimer>

#include "http_test_server.h"

class FakeVisionServer : public HttpTestServer {
public:
    explicit FakeVisionServer(QObject* parent = nullptr)
        : HttpTestServer(parent) {
        route("POST", "/api/user/login", [this](const Request& req) {
            ++m_loginCallCount;
            m_lastLoginBody = parseBody(req.body);
            return dequeue(m_loginQueue, makeApiError("unexpected login request"));
        });
        route("POST", "/api/vessel/command", [this](const Request& req) {
            ++m_scanCallCount;
            m_lastScanBody = parseBody(req.body);
            Response response = dequeue(m_scanQueue, makeApiError("unexpected scan request"));
            maybeEmitQueuedScanEvents();
            return response;
        });
        route("POST", "/api/vessellog/last", [this](const Request& req) {
            ++m_lastLogCallCount;
            m_lastLogBody = parseBody(req.body);
            return dequeue(m_lastLogQueue, makeApiError("unexpected vessellog.last request"));
        });

        listen(QHostAddress::LocalHost, 0);
    }

    void enqueueLoginDone(const QString& token) {
        m_loginQueue.enqueue(QueuedReply::apiDone(QJsonObject{{"token", token}, {"role", 0}}));
    }

    void enqueueLoginError(const QString& message) {
        m_loginQueue.enqueue(QueuedReply::apiError(message));
    }

    void enqueueLoginHang() {
        m_loginQueue.enqueue(QueuedReply::hang());
    }

    void enqueueScanDone() {
        m_scanQueue.enqueue(QueuedReply::apiDone(QJsonObject{{"accepted", true}}));
    }

    void enqueueScanError(const QString& message) {
        m_scanQueue.enqueue(QueuedReply::apiError(message));
    }

    void enqueueScanHang() {
        m_scanQueue.enqueue(QueuedReply::hang());
    }

    void enqueueScannerErrorEvent(const QString& error, int delayMs = 0) {
        m_pendingScanEvents.enqueue(PendingWsEvent{
            "scanner.error",
            QJsonObject{
                {"id", 15},
                {"error", error},
            },
            delayMs,
        });
    }

    void rejectNextWsConnect() {
        m_rejectNextWsConnect = true;
    }

    void disconnectAfterNextWsConnect() {
        m_disconnectAfterNextWsConnect = true;
    }

    void enqueueLastLogNewerThanNow() {
        m_lastLogQueue.enqueue(QueuedReply::dynamicDone([]() {
            return QJsonObject{
                {"logTime", QDateTime::currentDateTime().addSecs(1).toString("yyyy-MM-dd HH:mm:ss")},
                {"pointCloudPath", "/tmp/pc/latest.pcd"},
                {"volume", 12.34},
            };
        }));
    }

    void enqueueLastLogOlderThanNow() {
        m_lastLogQueue.enqueue(QueuedReply::dynamicDone([]() {
            return QJsonObject{
                {"logTime", QDateTime::currentDateTime().addSecs(-3600).toString("yyyy-MM-dd HH:mm:ss")},
                {"pointCloudPath", "/tmp/pc/old.pcd"},
                {"volume", 8.76},
            };
        }));
    }

    void enqueueLastLogError(const QString& message) {
        m_lastLogQueue.enqueue(QueuedReply::apiError(message));
    }

    void enqueueLastLogHang() {
        m_lastLogQueue.enqueue(QueuedReply::hang());
    }

    int loginCallCount() const { return m_loginCallCount; }
    int scanCallCount() const { return m_scanCallCount; }
    int lastLogCallCount() const { return m_lastLogCallCount; }

    const QJsonObject& lastLoginBody() const { return m_lastLoginBody; }
    const QJsonObject& lastScanBody() const { return m_lastScanBody; }
    const QJsonObject& lastLogBody() const { return m_lastLogBody; }
    const QSet<QString>& subscribedTopics() const { return m_seenSubscriptions; }

private:
    struct PendingWsEvent {
        QString eventName;
        QJsonObject data;
        int delayMs = 0;
    };

    struct QueuedReply {
        enum class Kind {
            StaticBody,
            DynamicBody,
            Hang,
        };

        Kind kind = Kind::StaticBody;
        QJsonObject body;
        std::function<QJsonObject()> bodyFactory;

        static QueuedReply apiDone(const QJsonObject& data) {
            QueuedReply item;
            item.kind = Kind::StaticBody;
            item.body = QJsonObject{{"code", 0}, {"message", "ok"}, {"data", data}};
            return item;
        }

        static QueuedReply apiError(const QString& message) {
            QueuedReply item;
            item.kind = Kind::StaticBody;
            item.body = QJsonObject{{"code", 1}, {"message", message}, {"data", QJsonObject{}}};
            return item;
        }

        static QueuedReply dynamicDone(std::function<QJsonObject()> builder) {
            QueuedReply item;
            item.kind = Kind::DynamicBody;
            item.bodyFactory = std::move(builder);
            return item;
        }

        static QueuedReply hang() {
            QueuedReply item;
            item.kind = Kind::Hang;
            return item;
        }
    };

    static QJsonObject parseBody(const QByteArray& body) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            return {};
        }
        return doc.object();
    }

    static Response makeApiError(const QString& message) {
        const QJsonObject obj{
            {"code", 1},
            {"message", message},
            {"data", QJsonObject{}},
        };
        return Response{200, "application/json", QJsonDocument(obj).toJson(QJsonDocument::Compact), 0};
    }

    bool shouldHandleSocketRead(QTcpSocket* sock) const override {
        return !m_wsBuffers.contains(sock);
    }

    bool handleUpgradeRequest(QTcpSocket* sock, const Request& req) override {
        QByteArray routePath = req.path;
        const int qmark = routePath.indexOf('?');
        if (qmark >= 0) {
            routePath = routePath.left(qmark);
        }
        if (routePath != "/ws") {
            return false;
        }

        if (m_rejectNextWsConnect) {
            m_rejectNextWsConnect = false;
            const QByteArray body = "websocket rejected";
            QByteArray out;
            out += "HTTP/1.1 503 Service Unavailable\r\n";
            out += "Content-Type: text/plain\r\n";
            out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
            out += "Connection: close\r\n\r\n";
            out += body;
            sock->write(out);
            sock->flush();
            sock->disconnectFromHost();
            return true;
        }

        const QByteArray key = req.headers.value("sec-websocket-key").trimmed();
        if (key.isEmpty()) {
            sock->disconnectFromHost();
            return true;
        }

        const QByteArray acceptSeed = key + QByteArrayLiteral("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        const QByteArray accept = QCryptographicHash::hash(acceptSeed, QCryptographicHash::Sha1).toBase64();

        QByteArray out;
        out += "HTTP/1.1 101 Switching Protocols\r\n";
        out += "Upgrade: websocket\r\n";
        out += "Connection: Upgrade\r\n";
        out += "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
        sock->write(out);
        sock->flush();

        m_wsBuffers[sock].clear();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() { handleWsReadyRead(sock); });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock]() {
            m_wsBuffers.remove(sock);
            m_wsSubscriptions.remove(sock);
        });

        if (m_disconnectAfterNextWsConnect) {
            m_disconnectAfterNextWsConnect = false;
            QTimer::singleShot(0, sock, [sock]() {
                if (sock->state() == QAbstractSocket::ConnectedState) {
                    QByteArray frame;
                    frame.append(static_cast<char>(0x88));
                    frame.append(static_cast<char>(0x00));
                    sock->write(frame);
                    sock->flush();
                    sock->disconnectFromHost();
                }
            });
        }
        return true;
    }

    Response dequeue(QQueue<QueuedReply>& queue, const Response& fallback) {
        if (queue.isEmpty()) {
            return fallback;
        }

        const QueuedReply item = queue.dequeue();
        if (item.kind == QueuedReply::Kind::Hang) {
            return Response{200, "application/json", QByteArray(), 60000};
        }

        QJsonObject body = item.body;
        if (item.kind == QueuedReply::Kind::DynamicBody && item.bodyFactory) {
            body = QJsonObject{
                {"code", 0},
                {"message", "ok"},
                {"data", item.bodyFactory()},
            };
        }
        return Response{200, "application/json", QJsonDocument(body).toJson(QJsonDocument::Compact), 0};
    }

    void maybeEmitQueuedScanEvents() {
        while (!m_pendingScanEvents.isEmpty()) {
            const PendingWsEvent event = m_pendingScanEvents.dequeue();
            auto emitOne = [this, event]() { broadcastEvent(event.eventName, event.data); };
            if (event.delayMs > 0) {
                QTimer::singleShot(event.delayMs, this, emitOne);
            } else {
                emitOne();
            }
        }
    }

    void handleWsReadyRead(QTcpSocket* sock) {
        if (!sock) {
            return;
        }
        QByteArray& buffer = m_wsBuffers[sock];
        buffer.append(sock->readAll());

        while (true) {
            QString textMessage;
            bool closeFrame = false;
            if (!tryTakeWsTextFrame(buffer, textMessage, closeFrame)) {
                break;
            }
            if (closeFrame) {
                sock->disconnectFromHost();
                return;
            }
            if (textMessage.isEmpty()) {
                continue;
            }

            const QJsonDocument doc = QJsonDocument::fromJson(textMessage.toUtf8());
            if (!doc.isObject()) {
                continue;
            }
            const QJsonObject obj = doc.object();
            const QString type = obj.value("type").toString();
            if (type == "sub") {
                const QString topic = obj.value("topic").toString();
                if (!topic.isEmpty()) {
                    m_wsSubscriptions[sock].insert(topic);
                    m_seenSubscriptions.insert(topic);
                }
            } else if (type == "unsub") {
                m_wsSubscriptions[sock].remove(obj.value("topic").toString());
            } else if (type == "ping") {
                sendWsFrame(sock, 0xA, QByteArray());
            }
        }
    }

    bool tryTakeWsTextFrame(QByteArray& buffer, QString& outText, bool& outCloseFrame) {
        outText.clear();
        outCloseFrame = false;

        if (buffer.size() < 2) {
            return false;
        }

        const quint8 b0 = static_cast<quint8>(buffer.at(0));
        const quint8 b1 = static_cast<quint8>(buffer.at(1));
        const quint8 opcode = b0 & 0x0F;
        const bool masked = (b1 & 0x80) != 0;
        quint64 payloadLen = static_cast<quint8>(b1 & 0x7F);
        int offset = 2;

        if (payloadLen == 126) {
            if (buffer.size() < offset + 2) {
                return false;
            }
            payloadLen = (static_cast<quint8>(buffer.at(offset)) << 8)
                       | static_cast<quint8>(buffer.at(offset + 1));
            offset += 2;
        } else if (payloadLen == 127) {
            if (buffer.size() < offset + 8) {
                return false;
            }
            payloadLen = 0;
            for (int i = 0; i < 8; ++i) {
                payloadLen = (payloadLen << 8) | static_cast<quint8>(buffer.at(offset + i));
            }
            offset += 8;
        }

        if (!masked || buffer.size() < offset + 4 + static_cast<int>(payloadLen)) {
            return false;
        }

        const QByteArray mask = buffer.mid(offset, 4);
        offset += 4;
        QByteArray payload = buffer.mid(offset, static_cast<int>(payloadLen));
        buffer.remove(0, offset + static_cast<int>(payloadLen));

        for (int i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<char>(payload.at(i) ^ mask.at(i % 4));
        }

        if (opcode == 0x8) {
            outCloseFrame = true;
            return true;
        }
        if (opcode == 0x9) {
            return true;
        }
        if (opcode != 0x1) {
            return true;
        }

        outText = QString::fromUtf8(payload);
        return true;
    }

    void sendWsFrame(QTcpSocket* sock, quint8 opcode, const QByteArray& payload) {
        if (!sock || sock->state() != QAbstractSocket::ConnectedState) {
            return;
        }

        QByteArray frame;
        frame.append(static_cast<char>(0x80 | (opcode & 0x0F)));
        if (payload.size() < 126) {
            frame.append(static_cast<char>(payload.size()));
        } else if (payload.size() <= 0xFFFF) {
            frame.append(static_cast<char>(126));
            frame.append(static_cast<char>((payload.size() >> 8) & 0xFF));
            frame.append(static_cast<char>(payload.size() & 0xFF));
        } else {
            frame.append(static_cast<char>(127));
            quint64 size = static_cast<quint64>(payload.size());
            for (int i = 7; i >= 0; --i) {
                frame.append(static_cast<char>((size >> (i * 8)) & 0xFF));
            }
        }
        frame.append(payload);
        sock->write(frame);
        sock->flush();
    }

    void broadcastEvent(const QString& eventName, const QJsonObject& eventData) {
        const QJsonObject inner{
            {"event", eventName},
            {"data", eventData},
        };
        const QJsonObject envelope{
            {"type", "pub"},
            {"message", QString::fromUtf8(QJsonDocument(inner).toJson(QJsonDocument::Compact))},
        };
        const QByteArray payload = QJsonDocument(envelope).toJson(QJsonDocument::Compact);

        for (auto it = m_wsSubscriptions.begin(); it != m_wsSubscriptions.end(); ++it) {
            QTcpSocket* sock = it.key();
            if (!sock || sock->state() != QAbstractSocket::ConnectedState) {
                continue;
            }
            if (!it.value().contains("vessel.notify")) {
                continue;
            }
            sendWsFrame(sock, 0x1, payload);
        }
    }

    QQueue<QueuedReply> m_loginQueue;
    QQueue<QueuedReply> m_scanQueue;
    QQueue<QueuedReply> m_lastLogQueue;
    QQueue<PendingWsEvent> m_pendingScanEvents;

    int m_loginCallCount = 0;
    int m_scanCallCount = 0;
    int m_lastLogCallCount = 0;

    QJsonObject m_lastLoginBody;
    QJsonObject m_lastScanBody;
    QJsonObject m_lastLogBody;
    QMap<QTcpSocket*, QByteArray> m_wsBuffers;
    QMap<QTcpSocket*, QSet<QString>> m_wsSubscriptions;
    QSet<QString> m_seenSubscriptions;
    bool m_rejectNextWsConnect = false;
    bool m_disconnectAfterNextWsConnect = false;
};
