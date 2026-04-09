#include "driver_3d_laser_radar/laser_transport.h"

#include <QAbstractSocket>
#include <QTcpSocket>

namespace laser_radar {

namespace {

class LaserTcpTransport : public ILaserTransport {
public:
    ~LaserTcpTransport() override { close(); }

    bool open(const LaserTransportParams& params, QString* errorMessage) override {
        close();
        m_socket.connectToHost(params.host, static_cast<quint16>(params.port));
        if (!m_socket.waitForConnected(params.timeoutMs)) {
            if (errorMessage) {
                *errorMessage = QString("Failed to connect to %1:%2: %3")
                                    .arg(params.host)
                                    .arg(params.port)
                                    .arg(m_socket.errorString());
            }
            return false;
        }
        return true;
    }

    bool writeFrame(const QByteArray& frame, int timeoutMs, QString* errorMessage) override {
        if (m_socket.state() != QAbstractSocket::ConnectedState) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("TCP socket is not connected");
            }
            return false;
        }
        const qint64 written = m_socket.write(frame);
        if (written != frame.size()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("TCP write failed");
            }
            return false;
        }
        if (!m_socket.waitForBytesWritten(timeoutMs)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("TCP write timeout");
            }
            return false;
        }
        return true;
    }

    bool readSome(QByteArray& chunk, int timeoutMs, QString* errorMessage) override {
        chunk.clear();
        if (!m_socket.waitForReadyRead(timeoutMs)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("TCP read timeout");
            }
            return false;
        }

        chunk = m_socket.readAll();
        while (m_socket.waitForReadyRead(10)) {
            chunk.append(m_socket.readAll());
        }
        if (!chunk.isEmpty()) {
            return true;
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("TCP read returned no data");
        }
        return false;
    }

    void close() override {
        if (m_socket.state() == QAbstractSocket::UnconnectedState) {
            return;
        }
        m_socket.disconnectFromHost();
        if (m_socket.state() != QAbstractSocket::UnconnectedState) {
            m_socket.waitForDisconnected(100);
        }
        m_socket.abort();
    }

private:
    QTcpSocket m_socket;
};

} // namespace

ILaserTransport* newLaserTransport() {
    return new LaserTcpTransport();
}

} // namespace laser_radar
