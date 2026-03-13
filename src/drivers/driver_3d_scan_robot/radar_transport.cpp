#include "driver_3d_scan_robot/radar_transport.h"

#include <QElapsedTimer>
#include <QSerialPort>
#include <QThread>

namespace scan_robot {

// ── 串口实现 ────────────────────────────────────────
class RadarSerialTransport : public IRadarTransport {
public:
    ~RadarSerialTransport() override { close(); }

    bool open(const RadarTransportParams& params, QString* errorMessage) override {
        close();
        m_serial.setPortName(params.port);
        m_serial.setBaudRate(params.baudRate);
        m_serial.setDataBits(QSerialPort::Data8);
        m_serial.setStopBits(QSerialPort::OneStop);
        m_serial.setParity(QSerialPort::NoParity);

        if (!m_serial.open(QIODevice::ReadWrite)) {
            if (errorMessage)
                *errorMessage = QString("Failed to open serial port %1: %2")
                                    .arg(params.port, m_serial.errorString());
            return false;
        }
        return true;
    }

    bool writeFrame(const QByteArray& frame, int timeoutMs, QString* errorMessage) override {
        qint64 written = m_serial.write(frame);
        if (written != frame.size()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Serial write failed");
            return false;
        }
        if (!m_serial.waitForBytesWritten(timeoutMs)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Serial write timeout");
            return false;
        }
        return true;
    }

    bool readSome(QByteArray& chunk, int timeoutMs, QString* errorMessage) override {
        chunk.clear();
        if (!m_serial.waitForReadyRead(timeoutMs)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Serial read timeout");
            }
            return false;
        }

        chunk = m_serial.readAll();
        while (m_serial.waitForReadyRead(10)) {
            chunk.append(m_serial.readAll());
        }
        return !chunk.isEmpty();
    }

    void close() override {
        if (m_serial.isOpen())
            m_serial.close();
    }

private:
    QSerialPort m_serial;
};

} // namespace scan_robot

// Factory function used by handler.cpp
scan_robot::IRadarTransport* newRadarSerialTransport() {
    return new scan_robot::RadarSerialTransport();
}
