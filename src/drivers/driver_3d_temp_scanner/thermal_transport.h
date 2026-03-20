#pragma once

#include <QByteArray>
#include <QString>

namespace temp_scanner {

struct ThermalTransportParams {
    QString portName;
    int baudRate = 115200;
    QString parity = "none";
    QString stopBits = "1";
    quint8 deviceAddr = 1;
    int timeoutMs = 3000;
    int scanTimeoutMs = 25000;
    int pollIntervalMs = 1000;
};

class IThermalTransport {
public:
    virtual ~IThermalTransport() = default;
    virtual bool open(const ThermalTransportParams& params, QString* errorMessage) = 0;
    virtual bool writeFrame(const QByteArray& frame, int timeoutMs, QString* errorMessage) = 0;
    virtual bool readSome(QByteArray& chunk, int timeoutMs, QString* errorMessage) = 0;
    virtual void close() = 0;
};

IThermalTransport* newThermalTransport();

} // namespace temp_scanner
