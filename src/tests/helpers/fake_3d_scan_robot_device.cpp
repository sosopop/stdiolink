#include "helpers/fake_3d_scan_robot_device.h"

#include <QtEndian>

namespace scan_robot {

bool Fake3DScanRobotDevice::open(const RadarTransportParams& params, QString* errorMessage) {
    m_params = params;
    if (m_openFail) {
        if (errorMessage)
            *errorMessage = m_openFailMessage.isEmpty()
                ? QString("Failed to open serial port %1").arg(params.port)
                : m_openFailMessage;
        return false;
    }
    m_open = true;
    return true;
}

bool Fake3DScanRobotDevice::writeFrame(const QByteArray& frame, int /*timeoutMs*/,
                                        QString* /*errorMessage*/) {
    m_sentFrames.append(frame);
    if (m_customHandler) {
        m_customHandler(frame, *this);
    }
    return true;
}

bool Fake3DScanRobotDevice::readSome(QByteArray& chunk, int /*timeoutMs*/,
                                      QString* errorMessage) {
    if (m_readTimeout) {
        if (errorMessage) *errorMessage = QStringLiteral("Serial read timeout");
        return false;
    }
    if (m_responseQueue.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("No response queued");
        return false;
    }
    chunk = m_responseQueue.takeFirst();
    return true;
}

void Fake3DScanRobotDevice::close() {
    m_open = false;
    m_closeCount++;
}

void Fake3DScanRobotDevice::setOpenFail(bool fail, const QString& message) {
    m_openFail = fail;
    m_openFailMessage = message;
}

void Fake3DScanRobotDevice::setReadTimeout(bool timeout) {
    m_readTimeout = timeout;
}

void Fake3DScanRobotDevice::enqueueRawResponse(const QByteArray& frame) {
    m_responseQueue.append(frame);
}

void Fake3DScanRobotDevice::enqueueReadRegisterSuccess(quint8 addr, quint8 counter,
                                                         quint16 regId, quint32 value) {
    QByteArray payload = makeRegPayload(regId, value);
    QByteArray frame = encodeFrame(counter, addr, CmdId::RegRead, payload);
    m_responseQueue.append(frame);
}

void Fake3DScanRobotDevice::enqueueWriteRegisterSuccess(quint8 addr, quint8 counter,
                                                          quint16 regId, quint32 value) {
    QByteArray payload = makeRegPayload(regId, value);
    QByteArray frame = encodeFrame(counter, addr, CmdId::RegWrite, payload);
    m_responseQueue.append(frame);
}

void Fake3DScanRobotDevice::enqueueTestComSuccess(quint8 addr, quint8 counter,
                                                    quint32 inputValue) {
    quint32 complement = ~inputValue;
    QByteArray frame = encodeFrame(counter, addr, CmdId::TestCom, makeU32Payload(complement));
    m_responseQueue.append(frame);
}

void Fake3DScanRobotDevice::enqueueSimpleResponse(quint8 addr, quint8 counter, quint8 cmd,
                                                    const QByteArray& payload, bool interrupt) {
    QByteArray frame = encodeFrame(counter, addr, cmd, payload, interrupt);
    m_responseQueue.append(frame);
}

void Fake3DScanRobotDevice::enqueueQueryResponse(quint8 addr, quint8 counter,
                                                   quint8 lastCtr, quint8 lastCmd,
                                                   quint32 resultCode) {
    QByteArray payload;
    payload.append(static_cast<char>(lastCtr));
    payload.append(static_cast<char>(lastCmd));
    quint8 buf[4];
    qToBigEndian(resultCode, buf);
    payload.append(reinterpret_cast<const char*>(buf), 4);
    QByteArray frame = encodeFrame(counter, addr, CmdId::Query, payload);
    m_responseQueue.append(frame);
}

void Fake3DScanRobotDevice::enqueueScanSegments(quint8 addr,
                                                  const QList<QByteArray>& segments,
                                                  quint16 segSize) {
    Q_UNUSED(segSize);
    for (int i = 0; i < segments.size(); ++i) {
        const QByteArray& seg = segments[i];
        QByteArray payload;
        quint8 idxBuf[4];
        qToBigEndian(static_cast<quint32>(i), idxBuf);
        payload.append(reinterpret_cast<const char*>(idxBuf), 4);
        quint8 lenBuf[2];
        qToBigEndian(static_cast<quint16>(seg.size()), lenBuf);
        payload.append(reinterpret_cast<const char*>(lenBuf), 2);
        payload.append(seg);
        QByteArray frame = encodeFrame(0, addr, CmdId::GetData, payload);
        m_responseQueue.append(frame);
    }
}

void Fake3DScanRobotDevice::enqueueSegmentSizeResponse(quint8 addr, quint8 counter,
                                                         quint16 segSize) {
    enqueueReadRegisterSuccess(addr, counter, RegId::SegmentSize, segSize);
}

void Fake3DScanRobotDevice::enqueueInterruptProgress(quint8 addr, quint8 counter,
                                                       quint16 currentLine, quint16 totalLines) {
    quint32 raw = (static_cast<quint32>(currentLine) << 16) | totalLines;
    QByteArray frame = encodeFrame(counter, addr, InsertCmdId::ScanProgress,
                                   makeU32Payload(raw), true);
    m_responseQueue.append(frame);
}

void Fake3DScanRobotDevice::enqueueInterruptStopReply(quint8 addr, quint8 counter,
                                                        quint32 value) {
    QByteArray frame = encodeFrame(counter, addr, InsertCmdId::ScanCancel,
                                   makeU32Payload(value), true);
    m_responseQueue.append(frame);
}

void Fake3DScanRobotDevice::enqueueInterruptTestReply(quint8 addr, quint8 counter,
                                                        quint32 value) {
    QByteArray frame = encodeFrame(counter, addr, InsertCmdId::Test,
                                   makeU32Payload(value), true);
    m_responseQueue.append(frame);
}

QByteArray Fake3DScanRobotDevice::lastSentFrame() const {
    return m_sentFrames.isEmpty() ? QByteArray() : m_sentFrames.last();
}

void Fake3DScanRobotDevice::setCustomHandler(
    std::function<void(const QByteArray&, Fake3DScanRobotDevice&)> handler) {
    m_customHandler = std::move(handler);
}

} // namespace scan_robot
