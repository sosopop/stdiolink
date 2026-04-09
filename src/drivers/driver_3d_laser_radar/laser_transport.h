#pragma once

#include <QByteArray>
#include <QString>

namespace laser_radar {

struct LaserTransportParams {
    QString host;
    int port = 23;
    int timeoutMs = 5000;
    int taskTimeoutMs = -1;
    int queryIntervalMs = 1000;
};

class ILaserTransport {
public:
    virtual ~ILaserTransport() = default;
    virtual bool open(const LaserTransportParams& params, QString* errorMessage) = 0;
    virtual bool writeFrame(const QByteArray& frame, int timeoutMs, QString* errorMessage) = 0;
    virtual bool readSome(QByteArray& chunk, int timeoutMs, QString* errorMessage) = 0;
    virtual void close() = 0;
};

ILaserTransport* newLaserTransport();

} // namespace laser_radar
