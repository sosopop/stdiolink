#include "driverlab_ws_handler.h"
#include "driverlab_ws_connection.h"

#include <QUrlQuery>

#include "stdiolink/host/driver_catalog.h"

namespace stdiolink_server {

DriverLabWsHandler::DriverLabWsHandler(stdiolink::DriverCatalog* catalog, QObject* parent)
    : QObject(parent), m_catalog(catalog) {}

DriverLabWsHandler::~DriverLabWsHandler() {
    closeAll();
}

void DriverLabWsHandler::registerVerifier(QHttpServer& server) {
    m_server = &server;

    server.addWebSocketUpgradeVerifier(
        this, [this](const QHttpServerRequest& request) { return verifyUpgrade(request); });

    connect(m_server, &QHttpServer::newWebSocketConnection, this,
            &DriverLabWsHandler::onNewWebSocketConnection);
}

int DriverLabWsHandler::activeConnectionCount() const {
    return m_connections.size();
}

void DriverLabWsHandler::closeAll() {
    // Take ownership of the list to avoid mutation during iteration
    auto connections = std::move(m_connections);
    m_connections.clear();
    qDeleteAll(connections);
}

QHttpServerWebSocketUpgradeResponse DriverLabWsHandler::verifyUpgrade(
    const QHttpServerRequest& request) {
    const QString path = request.url().path();
    if (!path.startsWith(QStringLiteral("/api/driverlab/")))
        return QHttpServerWebSocketUpgradeResponse::passToNext();

    const QString driverId = path.mid(QStringLiteral("/api/driverlab/").size());
    if (driverId.isEmpty() || !m_catalog->hasDriver(driverId))
        return QHttpServerWebSocketUpgradeResponse::deny(404, "driver not found");

    if (m_connections.size() >= kMaxConnections)
        return QHttpServerWebSocketUpgradeResponse::deny(429, "too many connections");

    // Validate runMode if provided
    QUrlQuery query(request.url());
    QString runMode = query.queryItemValue(QStringLiteral("runMode"));
    if (!runMode.isEmpty() && runMode != "oneshot" && runMode != "keepalive")
        return QHttpServerWebSocketUpgradeResponse::deny(400, "invalid runMode");

    return QHttpServerWebSocketUpgradeResponse::accept();
}

DriverLabWsHandler::ConnectionParams DriverLabWsHandler::parseConnectionParams(const QUrl& url) {
    ConnectionParams params;

    const QString path = url.path();
    if (path.startsWith(QStringLiteral("/api/driverlab/")))
        params.driverId = path.mid(QStringLiteral("/api/driverlab/").size());

    QUrlQuery query(url);
    params.runMode = query.queryItemValue(QStringLiteral("runMode"));
    if (params.runMode.isEmpty())
        params.runMode = QStringLiteral("oneshot");

    QString argsStr = query.queryItemValue(QStringLiteral("args"));
    if (!argsStr.isEmpty())
        params.extraArgs = argsStr.split(',', Qt::SkipEmptyParts);

    return params;
}

void DriverLabWsHandler::onNewWebSocketConnection() {
    while (m_server->hasPendingWebSocketConnections()) {
        auto socket = m_server->nextPendingWebSocketConnection();
        if (!socket)
            continue;

        // Re-parse parameters from the socket's request URL
        const ConnectionParams params = parseConnectionParams(socket->requestUrl());

        if (params.driverId.isEmpty() || !m_catalog->hasDriver(params.driverId)) {
            socket->close();
            continue;
        }

        const auto driverConfig = m_catalog->getConfig(params.driverId);

        auto* conn =
            new DriverLabWsConnection(std::move(socket), params.driverId, driverConfig.program,
                                      params.runMode, driverConfig.args + params.extraArgs, this);

        connect(conn, &DriverLabWsConnection::closed, this,
                &DriverLabWsHandler::onConnectionClosed);

        m_connections.append(conn);
    }
}

void DriverLabWsHandler::onConnectionClosed(DriverLabWsConnection* conn) {
    m_connections.removeOne(conn);
    conn->deleteLater();
}

} // namespace stdiolink_server
