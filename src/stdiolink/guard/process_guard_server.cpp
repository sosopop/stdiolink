#include "process_guard_server.h"
#include <QUuid>

namespace stdiolink {

ProcessGuardServer::ProcessGuardServer(QObject* parent)
    : m_name("stdiolink_guard_" + QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    Q_UNUSED(parent)
}

ProcessGuardServer::~ProcessGuardServer() {
    stop();
}

bool ProcessGuardServer::start() {
    return start(m_name);
}

bool ProcessGuardServer::start(const QString& nameOverride) {
    if (m_server) {
        stop();
    }

    m_name = nameOverride;
    m_server = new QLocalServer();
    m_server->setSocketOptions(QLocalServer::WorldAccessOption);

    if (!m_server->listen(m_name)) {
        delete m_server;
        m_server = nullptr;
        return false;
    }

    QObject::connect(m_server, &QLocalServer::newConnection, [this]() {
        while (m_server->hasPendingConnections()) {
            QLocalSocket* sock = m_server->nextPendingConnection();
            if (sock) {
                m_connections.append(sock);
            }
        }
    });

    return true;
}

void ProcessGuardServer::stop() {
    // Clear connections first — sockets are parented to m_server,
    // so deleting server afterwards won't double-free.
    qDeleteAll(m_connections);
    m_connections.clear();

    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
}

QString ProcessGuardServer::guardName() const {
    return m_name;
}

bool ProcessGuardServer::isListening() const {
    return m_server && m_server->isListening();
}

} // namespace stdiolink
