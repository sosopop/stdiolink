#include "driverlab_ws_handler.h"
#include "driverlab_ws_connection.h"

#include <QHttpServerWebSocketUpgradeResponse>
#include <QUrlQuery>
#include <QWebSocket>

#include "stdiolink/host/driver_catalog.h"

namespace stdiolink_server {

DriverLabWsHandler::DriverLabWsHandler(stdiolink::DriverCatalog* catalog,
                                       QObject* parent)
    : QObject(parent)
    , m_catalog(catalog) {
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
    });
}

int DriverLabWsHandler::activeConnectionCount() const {
    return m_connections.size();
}

void DriverLabWsHandler::closeAll() {
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

} // namespace stdiolink_server
