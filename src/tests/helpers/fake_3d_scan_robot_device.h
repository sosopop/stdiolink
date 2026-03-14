#pragma once

#include <QByteArray>
#include <QList>
#include <QMutex>
#include <functional>

#include "driver_3d_scan_robot/protocol_codec.h"
#include "driver_3d_scan_robot/radar_transport.h"

namespace scan_robot {

// ── Fake3DScanRobotDevice ───────────────────────────────
// A scripted fake device that implements IRadarTransport for testing.
// Tests enqueue expected responses; the fake returns them in order.

class Fake3DScanRobotDevice : public IRadarTransport {
public:
    // ── IRadarTransport implementation ──────────────────
    bool open(const RadarTransportParams& params, QString* errorMessage) override;
    bool writeFrame(const QByteArray& frame, int timeoutMs, QString* errorMessage) override;
    bool readSome(QByteArray& chunk, int timeoutMs, QString* errorMessage) override;
    void close() override;

    // ── Configuration ───────────────────────────────────
    void setOpenFail(bool fail, const QString& message = {});
    void setReadTimeout(bool timeout);

    // ── Response scripting ──────────────────────────────

    // Enqueue a raw response frame (will be returned by next readSome)
    void enqueueRawResponse(const QByteArray& frame);

    // Convenience: enqueue a register read success
    void enqueueReadRegisterSuccess(quint8 addr, quint8 counter,
                                     quint16 regId, quint32 value);

    // Convenience: enqueue a register write success
    void enqueueWriteRegisterSuccess(quint8 addr, quint8 counter,
                                      quint16 regId, quint32 value);

    // Convenience: enqueue a test echo (bitwise complement)
    void enqueueTestComSuccess(quint8 addr, quint8 counter, quint32 inputValue);

    // Convenience: enqueue a simple command response with u32 payload
    void enqueueSimpleResponse(quint8 addr, quint8 counter, quint8 cmd,
                                const QByteArray& payload, bool interrupt = false);

    // Convenience: enqueue a query response (cmd=6)
    void enqueueQueryResponse(quint8 addr, quint8 counter,
                               quint8 lastCtr, quint8 lastCmd, quint32 resultCode);

    // Convenience: enqueue scan segments (each segment is a get_data response)
    void enqueueScanSegments(quint8 addr, const QList<QByteArray>& segments,
                              quint16 segSize);

    // Convenience: enqueue a segment size register read success
    void enqueueSegmentSizeResponse(quint8 addr, quint8 counter, quint16 segSize);

    // Convenience: enqueue interrupt progress (scan_progress)
    void enqueueInterruptProgress(quint8 addr, quint8 counter,
                                   quint16 currentLine, quint16 totalLines);

    // Convenience: enqueue interrupt stop reply
    void enqueueInterruptStopReply(quint8 addr, quint8 counter, quint32 value);

    // Convenience: enqueue interrupt test reply
    void enqueueInterruptTestReply(quint8 addr, quint8 counter, quint32 value);

    // ── Inspection ──────────────────────────────────────
    bool isOpen() const { return m_open; }
    int sentFrameCount() const { return m_sentFrames.size(); }
    QByteArray lastSentFrame() const;
    RadarTransportParams lastParams() const { return m_params; }
    int closeCount() const { return m_closeCount; }

    // Custom handler: if set, called on each writeFrame to generate dynamic responses
    void setCustomHandler(std::function<void(const QByteArray&, Fake3DScanRobotDevice&)> handler);

private:
    bool m_open = false;
    bool m_openFail = false;
    bool m_readTimeout = false;
    QString m_openFailMessage;
    RadarTransportParams m_params;

    QList<QByteArray> m_responseQueue;
    QList<QByteArray> m_sentFrames;
    int m_closeCount = 0;

    std::function<void(const QByteArray&, Fake3DScanRobotDevice&)> m_customHandler;
};

} // namespace scan_robot
