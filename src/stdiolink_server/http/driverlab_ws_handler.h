#pragma once

#include <QHttpServer>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVector>

namespace stdiolink {
class DriverCatalog;
}

namespace stdiolink_server {

class DriverLabWsConnection;

class DriverLabWsHandler : public QObject {
    Q_OBJECT
public:
    explicit DriverLabWsHandler(stdiolink::DriverCatalog* catalog, QObject* parent = nullptr);
    ~DriverLabWsHandler();

    void registerVerifier(QHttpServer& server);
    int activeConnectionCount() const;
    void closeAll();

    static constexpr int kMaxConnections = 10;

private slots:
    void onNewWebSocketConnection();
    void onConnectionClosed(DriverLabWsConnection* conn);

private:
    QHttpServerWebSocketUpgradeResponse verifyUpgrade(const QHttpServerRequest& request);

    struct ConnectionParams {
        QString driverId;
        QString runMode;
        QStringList extraArgs;
    };
    static ConnectionParams parseConnectionParams(const QUrl& url);

    stdiolink::DriverCatalog* m_catalog;
    QHttpServer* m_server = nullptr;
    QVector<DriverLabWsConnection*> m_connections;
};

} // namespace stdiolink_server
