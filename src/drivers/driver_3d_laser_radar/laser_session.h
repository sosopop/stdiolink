#pragma once

#include "driver_3d_laser_radar/laser_transport.h"
#include "driver_3d_laser_radar/protocol_codec.h"

namespace laser_radar {

struct QueryTaskResult {
    quint16 lastCounter = 0;
    quint8 lastCommand = 0;
    quint32 resultA = 0;
    quint32 resultB = 0;
};

struct ScanAggregateResult {
    int segmentCount = 0;
    int byteCount = 0;
    QByteArray data;
};

enum class SessionErrorKind {
    None,
    Transport,
    Protocol,
};

class LaserSession {
public:
    explicit LaserSession(ILaserTransport* transport);
    ~LaserSession();

    bool open(const LaserTransportParams& params, QString* errorMessage);
    void close();

    quint16 nextCounter() const { return m_counter; }

    bool sendAndReceive(quint8 command, const QByteArray& payload, LaserFrame* response,
                        QString* errorMessage, SessionErrorKind* errorKind = nullptr);
    bool sendExpectNoImmediateResponse(quint8 command, const QByteArray& payload, quint16* counter,
                                       QString* errorMessage,
                                       SessionErrorKind* errorKind = nullptr);

    bool readRegister(quint16 regId, quint32* value, QString* errorMessage,
                      SessionErrorKind* errorKind = nullptr);
    bool writeRegister(quint16 regId, quint32 writeValue, quint32* echoedValue,
                       QString* errorMessage, SessionErrorKind* errorKind = nullptr);
    bool query(quint32 op, QueryTaskResult* result, QString* errorMessage,
               SessionErrorKind* errorKind = nullptr);
    bool cancel(quint16* lastCounter, quint8* lastCommand, quint8* resultCode,
                QString* errorMessage, SessionErrorKind* errorKind = nullptr);
    bool readSegment(quint32 segmentIndex, quint32* segmentCount, QByteArray* segmentData,
                     QString* errorMessage, SessionErrorKind* errorKind = nullptr);
    bool waitTaskCompleted(quint16 expectedCounter, quint8 expectedCommand, int taskTimeoutMs,
                           QueryTaskResult* result, QString* errorMessage,
                           SessionErrorKind* errorKind = nullptr);
    bool collectAllSegments(ScanAggregateResult* result, QString* errorMessage,
                            SessionErrorKind* errorKind = nullptr);

private:
    quint16 reserveCounter();

    ILaserTransport* m_transport = nullptr;
    LaserTransportParams m_params;
    quint16 m_counter = 0;
};

} // namespace laser_radar
