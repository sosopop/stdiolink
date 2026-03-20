#include "driver_3d_temp_scanner/thermal_transport.h"

#include <QSerialPort>

namespace temp_scanner {

namespace {

class ThermalSerialTransport final : public IThermalTransport {
public:
    ~ThermalSerialTransport() override { close(); }

    bool open(const ThermalTransportParams& params, QString* errorMessage) override {
        close();
        m_serial.setPortName(params.portName);
        m_serial.setBaudRate(params.baudRate);
        m_serial.setDataBits(QSerialPort::Data8);
        m_serial.setStopBits(params.stopBits == "2" ? QSerialPort::TwoStop : QSerialPort::OneStop);
        if (params.parity == "even") {
            m_serial.setParity(QSerialPort::EvenParity);
        } else if (params.parity == "odd") {
            m_serial.setParity(QSerialPort::OddParity);
        } else {
            m_serial.setParity(QSerialPort::NoParity);
        }

        if (!m_serial.open(QIODevice::ReadWrite)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Failed to open serial port %1: %2")
                                    .arg(params.portName, m_serial.errorString());
            }
            return false;
        }
        m_serial.clear(QSerialPort::AllDirections);
        return true;
    }

    bool writeFrame(const QByteArray& frame, int timeoutMs, QString* errorMessage) override {
        m_serial.clear(QSerialPort::Input);
        const qint64 written = m_serial.write(frame);
        if (written != frame.size()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Serial write failed");
            }
            return false;
        }
        if (!m_serial.waitForBytesWritten(timeoutMs)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Serial write timeout");
            }
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
        if (!chunk.isEmpty()) {
            return true;
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Serial read returned empty chunk");
        }
        return false;
    }

    void close() override {
        if (m_serial.isOpen()) {
            m_serial.close();
        }
    }

private:
    QSerialPort m_serial;
};

} // namespace

IThermalTransport* newThermalTransport() {
    return new ThermalSerialTransport();
}

} // namespace temp_scanner
