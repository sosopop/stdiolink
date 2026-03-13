#include "driver_3d_scan_robot/radar_session.h"

#include <QElapsedTimer>
#include <QThread>

namespace scan_robot {

RadarSession::RadarSession(IRadarTransport* transport)
    : m_transport(transport) {}

RadarSession::~RadarSession() {
    close();
}

bool RadarSession::open(const RadarTransportParams& params, QString* errorMessage) {
    m_params = params;
    return m_transport->open(params, errorMessage);
}

void RadarSession::close() {
    m_transport->close();
}

quint8 RadarSession::nextCounter() {
    quint8 c = m_counter;
    m_counter = (m_counter + 1) % 256;
    return c;
}

quint8 RadarSession::nextInsertCounter() {
    quint8 c = m_insertCounter;
    m_insertCounter = (m_insertCounter + 1) % 256;
    return c;
}

bool RadarSession::sendAndReceive(quint8 command, const QByteArray& payload,
                                   RadarFrame* response, QString* errorMessage,
                                   bool interrupt,
                                   SessionErrorKind* errorKind) {
    if (errorKind) {
        *errorKind = SessionErrorKind::None;
    }
    quint8 ctr = interrupt ? nextInsertCounter() : nextCounter();
    QByteArray frame = encodeFrame(ctr, m_params.addr, command, payload, interrupt);

    if (!m_transport->writeFrame(frame, m_params.timeoutMs, errorMessage)) {
        if (errorKind) {
            *errorKind = SessionErrorKind::Transport;
        }
        return false;
    }

    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    const FrameChannel channel = interrupt ? FrameChannel::Interrupt : FrameChannel::Main;

    while (timer.elapsed() < m_params.timeoutMs) {
        QByteArray chunk;
        const int remaining = qMax(1, m_params.timeoutMs - static_cast<int>(timer.elapsed()));
        if (!m_transport->readSome(chunk, remaining, errorMessage)) {
            if (buffer.isEmpty()) {
                if (errorKind) {
                    *errorKind = SessionErrorKind::Transport;
                }
                return false;
            }

            int consumedBytes = 0;
            const DecodeStatus finalStatus = tryDecodeFrame(
                buffer, m_params.addr, command, response, errorMessage,
                channel, &consumedBytes, true);
            if (finalStatus == DecodeStatus::Ok) {
                return true;
            }
            if (errorKind) {
                *errorKind = SessionErrorKind::Protocol;
            }
            if (errorMessage && errorMessage->isEmpty()) {
                *errorMessage = QStringLiteral("Decode failed: ") + decodeStatusToString(finalStatus);
            }
            return false;
        }

        buffer.append(chunk);
        int consumedBytes = 0;
        const DecodeStatus status = tryDecodeFrame(
            buffer, m_params.addr, command, response, errorMessage,
            channel, &consumedBytes, false);
        if (status == DecodeStatus::Ok) {
            return true;
        }
        if (status != DecodeStatus::Incomplete) {
            if (errorKind) {
                *errorKind = SessionErrorKind::Protocol;
            }
            if (errorMessage && errorMessage->isEmpty()) {
                *errorMessage = QStringLiteral("Decode failed: ") + decodeStatusToString(status);
            }
            return false;
        }
    }

    if (errorKind) {
        *errorKind = buffer.isEmpty() ? SessionErrorKind::Transport : SessionErrorKind::Protocol;
    }
    if (errorMessage && errorMessage->isEmpty()) {
        *errorMessage = buffer.isEmpty() ? QStringLiteral("Serial read timeout")
                                         : QStringLiteral("Incomplete frame");
    }
    return false;
}

bool RadarSession::waitTaskCompleted(quint8 expectedCounter, quint8 expectedCommand,
                                     int taskTimeoutMs, int queryIntervalMs, int interCmdDelayMs,
                                     QueryTaskResult* result, QString* errorMessage) {
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < taskTimeoutMs) {
        QThread::msleep(static_cast<unsigned long>(queryIntervalMs));

        if (timer.elapsed() >= taskTimeoutMs) break;

        // inter-command delay before query
        if (interCmdDelayMs > 0)
            QThread::msleep(static_cast<unsigned long>(interCmdDelayMs));

        RadarFrame queryResponse;
        QByteArray queryPayload = makeU32Payload(100);
        if (!sendAndReceive(CmdId::Query, queryPayload, &queryResponse, errorMessage))
            continue;  // retry on transient failure

        quint8 lastCtr = 0, lastCmd = 0;
        quint32 resultCode = 0;
        if (!parseQueryResponse(queryResponse.payload, &lastCtr, &lastCmd, &resultCode)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Failed to parse query response");
            continue;
        }

        // Check if it matches our task
        if (lastCtr != expectedCounter || lastCmd != expectedCommand)
            continue;  // stale result from previous task

        result->lastCounter = lastCtr;
        result->lastCommand = lastCmd;
        result->resultCode  = resultCode;

        if (resultCode == TaskResult::StillRunning)
            continue;

        // Task completed (success or failure)
        return true;
    }

    if (errorMessage)
        *errorMessage = QStringLiteral("Task timeout");
    return false;
}

bool RadarSession::readSegmentSize(quint16* segSize, QString* errorMessage) {
    RadarFrame response;
    QByteArray payload = makeRegPayload(RegId::SegmentSize, 100);
    if (!sendAndReceive(CmdId::RegRead, payload, &response, errorMessage))
        return false;

    quint16 regAddr;
    quint32 value;
    if (!parseRegResponse(response.payload, &regAddr, &value)) {
        if (errorMessage) *errorMessage = QStringLiteral("Failed to parse segment size response");
        return false;
    }
    if (regAddr != RegId::SegmentSize) {
        if (errorMessage) *errorMessage = QStringLiteral("Segment size register mismatch");
        return false;
    }
    *segSize = static_cast<quint16>(value);
    return true;
}

bool RadarSession::collectScanData(int totalBytes, int interCmdDelayMs,
                                   ScanAggregateResult* result, QString* errorMessage) {
    // Read segment size from device
    quint16 segSize = 0;
    if (!readSegmentSize(&segSize, errorMessage))
        return false;

    if (segSize == 0 || segSize > 1376 || (segSize % 16) != 0) {
        if (errorMessage)
            *errorMessage = QString("Invalid segment size: %1").arg(segSize);
        return false;
    }

    int segCount = totalBytes / segSize;
    if (totalBytes % segSize != 0) segCount++;

    QByteArray aggregated;
    aggregated.resize(totalBytes);
    aggregated.fill(0);

    for (int i = 0; i < segCount; ++i) {
        if (interCmdDelayMs > 0)
            QThread::msleep(static_cast<unsigned long>(interCmdDelayMs));

        // Retry up to 5 times per segment (matching MatLab get_segment.m)
        bool segOk = false;
        for (int attempt = 0; attempt < 5; ++attempt) {
            if (attempt > 0)
                QThread::msleep(300);

            RadarFrame segResponse;
            QByteArray segPayload = makeU32Payload(static_cast<quint32>(i));
            if (!sendAndReceive(CmdId::GetData, segPayload, &segResponse, errorMessage))
                continue;

            quint32 segIndex;
            quint16 segLen;
            QByteArray segData;
            if (!parseSegmentResponse(segResponse.payload, &segIndex, &segLen, &segData))
                continue;

            if (static_cast<int>(segIndex) != i)
                continue;

            // Copy segment data into aggregated buffer
            int offset = i * segSize;
            int copyLen = qMin(static_cast<int>(segLen), totalBytes - offset);
            std::memcpy(aggregated.data() + offset, segData.constData(), copyLen);
            segOk = true;
            break;
        }

        if (!segOk) {
            if (errorMessage)
                *errorMessage = QString("Failed to fetch segment %1 after retries").arg(i);
            return false;
        }
    }

    result->segmentCount = segCount;
    result->byteCount    = totalBytes;
    result->data         = aggregated;
    return true;
}

} // namespace scan_robot
