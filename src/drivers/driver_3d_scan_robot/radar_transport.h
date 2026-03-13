#pragma once

#include <QByteArray>
#include <QString>

namespace scan_robot {

struct RadarTransportParams {
    QString port;
    int     baudRate           = 115200;
    quint8  addr               = 1;
    int     timeoutMs          = 5000;
    int     taskTimeoutMs      = -1;   // -1 = derive from command defaults
    int     queryIntervalMs    = 1000;
    int     interCommandDelayMs = 250;
};

// 传输接口：串口或 fake 测试桩均可实现
class IRadarTransport {
public:
    virtual ~IRadarTransport() = default;
    virtual bool open(const RadarTransportParams& params, QString* errorMessage) = 0;
    virtual bool writeFrame(const QByteArray& frame, int timeoutMs, QString* errorMessage) = 0;
    virtual bool readSome(QByteArray& chunk, int timeoutMs, QString* errorMessage) = 0;
    virtual void close() = 0;
};

} // namespace scan_robot
