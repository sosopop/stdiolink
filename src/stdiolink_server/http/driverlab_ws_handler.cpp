#include "driverlab_ws_handler.h"
#include "driverlab_ws_connection.h"

#include <QDateTime>
#include <QHttpServerWebSocketUpgradeResponse>
#include <QUrlQuery>
#include <QWebSocket>

#include "stdiolink/host/driver_catalog.h"

namespace stdiolink_server {

DriverLabWsHandler::DriverLabWsHandler(stdiolink::DriverCatalog* catalog,
                                       QObject* parent)
    : QObject(parent)
    , m_catalog(catalog) {
    m_pingTimer.setInterval(kPingIntervalMs);
    connect(&m_pingTimer, &QTimer::timeout,
            this, &DriverLabWsHandler::onPingTick);
    m_pingTimer.start();
}

DriverLabWsHandler::~DriverLabWsHandler() {
    closeAll();
}

void DriverLabWsHandler::registerVerifier(QHttpServer& server) {
    m_server = &server;

    server.addWebSocketUpgradeVerifier(
        this,
        [this](const QHttpServerRequest& request) -> QHttpServerWebSocketUpgradeResponse {
            const QString path = request.url().path();
            if (!path.startsWith(QStringLiteral("/api/driverlab/"))) {
                return QHttpServerWebSocketUpgradeResponse::passToNext();
            }

            const QString driverId = path.mid(QStringLiteral("/api/driverlab/").size());
            if (driverId.isEmpty() || !m_catalog->hasDriver(driverId)) {
                return QHttpServerWebSocketUpgradeResponse::deny(
                    404, "driver not found");
            }

            if (m_connections.size() >= kMaxConnections) {
                return QHttpServerWebSocketUpgradeResponse::deny(
                    429, "too many connections");
            }

            // Validate runMode if provided
            const QUrlQuery query(request.url());
            const QString runMode = query.queryItemValue("runMode");
            if (!runMode.isEmpty() && runMode != "oneshot" && runMode != "keepalive") {
                return QHttpServerWebSocketUpgradeResponse::deny(
                    400, "invalid runMode");
            }

            return QHttpServerWebSocketUpgradeResponse::accept();
        });

    connect(&server, &QHttpServer::newWebSocketConnection,
            this, [this]() {
        auto socket = std::unique_ptr<QWebSocket>(m_server->nextPendingWebSocketConnection());
        if (!socket) {
            return;
        }

        const ConnectionParams params = parseConnectionParams(socket->requestUrl());
        if (params.driverId.isEmpty()) {
            socket->close(QWebSocketProtocol::CloseCodeBadOperation, "invalid request");
            return;
        }

        if (!m_catalog->hasDriver(params.driverId)) {
            socket->close(QWebSocketProtocol::CloseCodeBadOperation, "driver not found");
            return;
        }

        const auto cfg = m_catalog->getConfig(params.driverId);

        auto* conn = new DriverLabWsConnection(
            std::move(socket),
            params.driverId,
            cfg.program,
            params.runMode,
            params.extraArgs,
            this);

        connect(conn, &DriverLabWsConnection::closed,
                this, &DriverLabWsHandler::onConnectionClosed);

        m_connections.append(conn);

        // Restart ping timer if it was stopped (e.g. by closeAll())
        if (!m_pingTimer.isActive()) {
            m_pingTimer.start();
        }
    });
}

int DriverLabWsHandler::activeConnectionCount() const {
    return m_connections.size();
}

void DriverLabWsHandler::closeAll() {
    m_pingTimer.stop();
    const auto connections = m_connections;
    for (auto* conn : connections) {
        delete conn;
    }
    m_connections.clear();
}

void DriverLabWsHandler::onConnectionClosed(DriverLabWsConnection* conn) {
    m_connections.removeOne(conn);
    conn->deleteLater();
}

DriverLabWsHandler::ConnectionParams
DriverLabWsHandler::parseConnectionParams(const QUrl& url) {
    ConnectionParams params;

    const QString path = url.path();
    const QString prefix = QStringLiteral("/api/driverlab/");
    if (!path.startsWith(prefix)) {
        return params;
    }

    params.driverId = path.mid(prefix.size());

    const QUrlQuery query(url);
    params.runMode = query.queryItemValue("runMode");
    if (params.runMode.isEmpty()) {
        params.runMode = "oneshot";
    }

    const QString argsStr = query.queryItemValue("args");
    if (!argsStr.isEmpty()) {
        params.extraArgs = argsStr.split(',', Qt::SkipEmptyParts);
    }

    return params;
}

void DriverLabWsHandler::onPingTick() {
    sweepDeadConnections();
    for (auto* conn : m_connections) {
        conn->sendPing();
    }
}

void DriverLabWsHandler::sweepDeadConnections() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVector<DriverLabWsConnection*> dead;
    for (auto* conn : m_connections) {
        const qint64 elapsed = conn->lastPongAt().msecsTo(now);
        if (elapsed > kPongTimeoutMs) {
            dead.append(conn);
        }
    }
    for (auto* conn : dead) {
        m_connections.removeOne(conn);
        conn->closeForPongTimeout();
        conn->deleteLater();
    }
}

void DriverLabWsHandler::setPingIntervalForTest(int ms) {
    m_pingTimer.setInterval(ms);
}

DriverLabWsConnection* DriverLabWsHandler::connectionAt(int index) const {
    if (index < 0 || index >= m_connections.size()) {
        return nullptr;
    }
    return m_connections.at(index);
}

} // namespace stdiolink_server
