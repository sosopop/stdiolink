#pragma once

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQueue>

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
            return dequeue(m_scanQueue, makeApiError("unexpected scan request"));
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

private:
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

    QQueue<QueuedReply> m_loginQueue;
    QQueue<QueuedReply> m_scanQueue;
    QQueue<QueuedReply> m_lastLogQueue;

    int m_loginCallCount = 0;
    int m_scanCallCount = 0;
    int m_lastLogCallCount = 0;

    QJsonObject m_lastLoginBody;
    QJsonObject m_lastScanBody;
    QJsonObject m_lastLogBody;
};
