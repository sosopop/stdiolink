#pragma once

#include <QMap>
#include <QPair>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <functional>

/// @brief Local HTTP stub server for testing (no Q_OBJECT needed)
class HttpTestServer : public QTcpServer {
public:
    struct Request {
        QByteArray method;
        QByteArray path;
        QMap<QByteArray, QByteArray> headers;
        QByteArray body;
    };

    struct Response {
        int status = 200;
        QByteArray contentType = "text/plain";
        QByteArray body;
        int delayMs = 0;
    };

    using Handler = std::function<Response(const Request&)>;

    explicit HttpTestServer(QObject* parent = nullptr)
        : QTcpServer(parent) {
        connect(this, &QTcpServer::newConnection, [this]() {
            while (auto* sock = nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, [this, sock]() {
                    handleData(sock);
                });
                connect(sock, &QTcpSocket::disconnected,
                        sock, &QObject::deleteLater);
            }
        });
    }

    void route(const QByteArray& method, const QByteArray& path, Handler handler) {
        m_routes[{method, path}] = std::move(handler);
    }

    QString baseUrl() const {
        return QString("http://127.0.0.1:%1").arg(serverPort());
    }

private:
    void handleData(QTcpSocket* sock) {
        m_buffers[sock].append(sock->readAll());
        const QByteArray& buf = m_buffers[sock];

        // Wait for end of headers
        int headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;

        // Parse request line
        int firstLine = buf.indexOf("\r\n");
        QByteArray requestLine = buf.left(firstLine);
        QList<QByteArray> parts = requestLine.split(' ');
        if (parts.size() < 2) {
            sendError(sock, 400);
            return;
        }

        Request req;
        req.method = parts[0];
        req.path = parts[1];

        // Parse headers
        QByteArray headerBlock = buf.mid(firstLine + 2, headerEnd - firstLine - 2);
        for (const QByteArray& line : headerBlock.split('\n')) {
            QByteArray trimmed = line.trimmed();
            int colon = trimmed.indexOf(':');
            if (colon > 0) {
                QByteArray key = trimmed.left(colon).trimmed().toLower();
                QByteArray val = trimmed.mid(colon + 1).trimmed();
                req.headers[key] = val;
            }
        }

        // Check Content-Length for body
        int contentLength = req.headers.value("content-length", "0").toInt();
        int totalExpected = headerEnd + 4 + contentLength;
        if (buf.size() < totalExpected) return; // wait for more data

        req.body = buf.mid(headerEnd + 4, contentLength);
        m_buffers.remove(sock);

        // Strip query string from path for routing
        QByteArray routePath = req.path;
        int qmark = routePath.indexOf('?');
        if (qmark >= 0) routePath = routePath.left(qmark);

        auto it = m_routes.find({req.method, routePath});
        if (it == m_routes.end()) {
            sendError(sock, 404);
            return;
        }

        Response resp = it.value()(req);
        if (resp.delayMs > 0) {
            // Parent timer to socket so it's cleaned up if socket dies
            auto* timer = new QTimer(sock);
            timer->setSingleShot(true);
            connect(timer, &QTimer::timeout, [this, sock, resp]() {
                sendResponse(sock, resp);
            });
            timer->start(resp.delayMs);
        } else {
            sendResponse(sock, resp);
        }
    }

    void sendResponse(QTcpSocket* sock, const Response& resp) {
        if (!sock || sock->state() != QAbstractSocket::ConnectedState) return;
        QByteArray out;
        out += "HTTP/1.1 " + QByteArray::number(resp.status) + " OK\r\n";
        out += "Content-Type: " + resp.contentType + "\r\n";
        out += "Content-Length: " + QByteArray::number(resp.body.size()) + "\r\n";
        out += "Connection: close\r\n";
        out += "\r\n";
        out += resp.body;
        sock->write(out);
        sock->flush();
        sock->disconnectFromHost();
    }

    void sendError(QTcpSocket* sock, int code) {
        sendResponse(sock, {code, "text/plain", QByteArray::number(code)});
    }

    QMap<QPair<QByteArray, QByteArray>, Handler> m_routes;
    QMap<QTcpSocket*, QByteArray> m_buffers;
};
