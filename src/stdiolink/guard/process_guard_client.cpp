#include "process_guard_client.h"
#include "force_fast_exit.h"
#include <QLocalSocket>

namespace stdiolink {

std::unique_ptr<ProcessGuardClient> ProcessGuardClient::startFromArgs(const QStringList& args) {
    QString guardName;
    for (const QString& arg : args) {
        if (arg.startsWith("--guard=")) {
            guardName = arg.mid(8); // len("--guard=") == 8
            break;
        }
    }

    if (guardName.isEmpty()) {
        return {};
    }

    auto client = std::make_unique<ProcessGuardClient>(guardName);
    client->start();
    return client;
}

ProcessGuardClient::ProcessGuardClient(const QString& guardName)
    : m_guardName(guardName) {
}

ProcessGuardClient::~ProcessGuardClient() {
    stop();
}

void ProcessGuardClient::start() {
    m_thread = new QThread();

    QObject::connect(m_thread, &QThread::started, [this]() {
        QLocalSocket socket;
        socket.connectToServer(m_guardName);

        if (!socket.waitForConnected(3000)) {
            if (!m_stopped.load()) {
                forceFastExit(1);
            }
            return;
        }

        // Block until disconnected
        while (socket.state() == QLocalSocket::ConnectedState) {
            if (m_stopped.load()) {
                socket.disconnectFromServer();
                return;
            }
            // waitForReadyRead will return false on disconnect
            socket.waitForReadyRead(200);
        }

        if (!m_stopped.load()) {
            forceFastExit(1);
        }
    });

    QObject::connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}

void ProcessGuardClient::stop() {
    m_stopped.store(true);

    if (m_thread) {
        m_thread->quit();
        // Thread exits within one poll cycle (~200ms) after m_stopped is set,
        // or after waitForConnected(3000) times out in the worst case.
        m_thread->wait();
        m_thread = nullptr;
    }
}

} // namespace stdiolink
