#pragma once

#include <QHttpServer>
#include <QObject>
#include <QTimer>
#include <QVector>

namespace stdiolink {
class DriverCatalog;
}

namespace stdiolink_server {

class DriverLabWsConnection;

class DriverLabWsHandler : public QObject {
    Q_OBJECT
public:
    explicit DriverLabWsHandler(stdiolink::DriverCatalog* catalog,
                                QObject* parent = nullptr);
    ~DriverLabWsHandler() override;

    void registerVerifier(QHttpServer& server);
    int activeConnectionCount() const;
    void closeAll();

    static constexpr int kMaxConnections = 10;
    static constexpr int kPingIntervalMs = 30000;
    static constexpr int kPongTimeoutMs  = kPingIntervalMs * 2;

    // Test-only helpers — not for production use
    void setPingIntervalForTest(int ms);
    DriverLabWsConnection* connectionAt(int index) const;

    struct ConnectionParams {
        QString driverId;
        QString runMode;
        QStringList extraArgs;
    };
    static ConnectionParams parseConnectionParams(const QUrl& url);

private slots:
    void onConnectionClosed(DriverLabWsConnection* conn);
    void onPingTick();

private:
    void sweepDeadConnections();

    stdiolink::DriverCatalog* m_catalog;
    QHttpServer* m_server = nullptr;
    QVector<DriverLabWsConnection*> m_connections;
    QTimer m_pingTimer;
};

} // namespace stdiolink_server
