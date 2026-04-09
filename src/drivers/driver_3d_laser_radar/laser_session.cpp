#include "driver_3d_laser_radar/laser_session.h"

#include <QElapsedTimer>
#include <QThread>

namespace laser_radar {

LaserSession::LaserSession(ILaserTransport* transport)
    : m_transport(transport) {}

LaserSession::~LaserSession() {
    close();
}

bool LaserSession::open(const LaserTransportParams& params, QString* errorMessage) {
    m_params = params;
    return m_transport->open(params, errorMessage);
}

void LaserSession::close() {
    if (m_transport) {
        m_transport->close();
    }
}

quint16 LaserSession::reserveCounter() {
    const quint16 counter = m_counter;
    m_counter = static_cast<quint16>(m_counter + 1);
    return counter;
}

bool LaserSession::sendAndReceive(quint8 command, const QByteArray& payload, LaserFrame* response,
                                  QString* errorMessage, SessionErrorKind* errorKind) {
    if (errorKind) {
        *errorKind = SessionErrorKind::None;
    }
    const quint16 counter = reserveCounter();
    const QByteArray frame = encodeFrame(counter, kDeviceAddr, command, payload);
    if (!m_transport->writeFrame(frame, m_params.timeoutMs, errorMessage)) {
        if (errorKind) {
            *errorKind = SessionErrorKind::Transport;
        }
        return false;
    }

    QByteArray buffer;
    QString transportError;
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < m_params.timeoutMs) {
        QByteArray chunk;
        const int remaining = qMax(1, m_params.timeoutMs - static_cast<int>(timer.elapsed()));
        if (!m_transport->readSome(chunk, remaining, &transportError)) {
            if (buffer.isEmpty()) {
                if (errorKind) {
                    *errorKind = SessionErrorKind::Transport;
                }
                if (errorMessage && errorMessage->isEmpty()) {
                    *errorMessage = transportError;
                }
                return false;
            }

            const DecodeStatus finalStatus = tryDecodeFrame(
                buffer, kDeviceAddr, command, response, errorMessage, nullptr, true);
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
        const DecodeStatus status = tryDecodeFrame(
            buffer, kDeviceAddr, command, response, errorMessage, nullptr, false);
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
        *errorMessage = buffer.isEmpty() ? QStringLiteral("TCP read timeout")
                                         : QStringLiteral("Incomplete frame");
    }
    return false;
}

bool LaserSession::sendExpectNoImmediateResponse(quint8 command, const QByteArray& payload,
                                                 quint16* counter, QString* errorMessage,
                                                 SessionErrorKind* errorKind) {
    if (errorKind) {
        *errorKind = SessionErrorKind::None;
    }
    const quint16 reserved = reserveCounter();
    const QByteArray frame = encodeFrame(reserved, kDeviceAddr, command, payload);
    if (!m_transport->writeFrame(frame, m_params.timeoutMs, errorMessage)) {
        if (errorKind) {
            *errorKind = SessionErrorKind::Transport;
        }
        return false;
    }
    if (counter) {
        *counter = reserved;
    }

    QByteArray chunk;
    QString readError;
    if (!m_transport->readSome(chunk, m_params.timeoutMs, &readError)) {
        if (readError.toLower().contains("timeout")) {
            return true;
        }
        if (errorKind) {
            *errorKind = SessionErrorKind::Transport;
        }
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = readError;
        }
        return false;
    }

    if (errorKind) {
        *errorKind = SessionErrorKind::Protocol;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral(
            "Long-running command returned immediate data unexpectedly");
    }
    return false;
}

bool LaserSession::readRegister(quint16 regId, quint32* value, QString* errorMessage,
                                SessionErrorKind* errorKind) {
    LaserFrame response;
    if (!sendAndReceive(CmdId::ReadReg, makeReadRegPayload(regId), &response, errorMessage,
                        errorKind)) {
        return false;
    }
    quint16 returnedRegId = 0;
    quint32 returnedValue = 0;
    if (!parseRegResponse(response.payload, &returnedRegId, &returnedValue)) {
        if (errorKind) {
            *errorKind = SessionErrorKind::Protocol;
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid register response");
        }
        return false;
    }
    if (returnedRegId == 0xFFFFu) {
        if (errorKind) {
            *errorKind = SessionErrorKind::Protocol;
        }
        if (errorMessage) {
            *errorMessage = QString("Device returned register error code %1").arg(returnedValue);
        }
        return false;
    }
    if (returnedRegId != regId) {
        if (errorKind) {
            *errorKind = SessionErrorKind::Protocol;
        }
        if (errorMessage) {
            *errorMessage = QString("Register mismatch: expected %1, got %2")
                                .arg(regId)
                                .arg(returnedRegId);
        }
        return false;
    }
    if (value) {
        *value = returnedValue;
    }
    return true;
}

bool LaserSession::writeRegister(quint16 regId, quint32 writeValue, quint32* echoedValue,
                                 QString* errorMessage, SessionErrorKind* errorKind) {
    LaserFrame response;
    if (!sendAndReceive(CmdId::WriteReg, makeWriteRegPayload(regId, writeValue), &response,
                        errorMessage, errorKind)) {
        return false;
    }
    quint16 returnedRegId = 0;
    quint32 returnedValue = 0;
    if (!parseRegResponse(response.payload, &returnedRegId, &returnedValue)) {
        if (errorKind) {
            *errorKind = SessionErrorKind::Protocol;
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid register write response");
        }
        return false;
    }
    if (returnedRegId == 0xFFFFu) {
        if (errorKind) {
            *errorKind = SessionErrorKind::Protocol;
        }
        if (errorMessage) {
            *errorMessage = QString("Device returned register error code %1").arg(returnedValue);
        }
        return false;
    }
    if (returnedRegId != regId || returnedValue != writeValue) {
        if (errorKind) {
            *errorKind = SessionErrorKind::Protocol;
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Register write echo mismatch");
        }
        return false;
    }
    if (echoedValue) {
        *echoedValue = returnedValue;
    }
    return true;
}

bool LaserSession::query(quint32 op, QueryTaskResult* result, QString* errorMessage,
                         SessionErrorKind* errorKind) {
    LaserFrame response;
    if (!sendAndReceive(CmdId::Query, makeU32Payload(op), &response, errorMessage, errorKind)) {
        return false;
    }
    QueryTaskResult localResult;
    if (!parseQueryResponse(response.payload, &localResult.lastCounter, &localResult.lastCommand,
                            &localResult.resultA, &localResult.resultB)) {
        if (errorKind) {
            *errorKind = SessionErrorKind::Protocol;
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid query response");
        }
        return false;
    }
    if (result) {
        *result = localResult;
    }
    return true;
}

bool LaserSession::cancel(quint16* lastCounter, quint8* lastCommand, quint8* resultCode,
                          QString* errorMessage, SessionErrorKind* errorKind) {
    LaserFrame response;
    if (!sendAndReceive(CmdId::Cancel, makeU32Payload(100), &response, errorMessage, errorKind)) {
        return false;
    }
    quint16 localCounter = 0;
    quint8 localCommand = 0;
    quint8 localResult = 0;
    if (!parseCancelResponse(response.payload, &localCounter, &localCommand, &localResult)) {
        if (errorKind) {
            *errorKind = SessionErrorKind::Protocol;
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid cancel response");
        }
        return false;
    }
    if (lastCounter) {
        *lastCounter = localCounter;
    }
    if (lastCommand) {
        *lastCommand = localCommand;
    }
    if (resultCode) {
        *resultCode = localResult;
    }
    return true;
}

bool LaserSession::readSegment(quint32 segmentIndex, quint32* segmentCount, QByteArray* segmentData,
                               QString* errorMessage, SessionErrorKind* errorKind) {
    LaserFrame response;
    if (!sendAndReceive(CmdId::GetData, makeU32Payload(segmentIndex), &response, errorMessage,
                        errorKind)) {
        return false;
    }
    quint32 returnedSegId = 0;
    quint32 returnedSegCount = 0;
    quint16 segLen = 0;
    QByteArray localData;
    if (!parseSegmentResponse(response.payload, &returnedSegId, &returnedSegCount, &segLen,
                              &localData)) {
        if (errorKind) {
            *errorKind = SessionErrorKind::Protocol;
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid segment response");
        }
        return false;
    }
    if (returnedSegId != segmentIndex) {
        if (errorKind) {
            *errorKind = SessionErrorKind::Protocol;
        }
        if (errorMessage) {
            *errorMessage = QString("Segment mismatch: expected %1, got %2")
                                .arg(segmentIndex)
                                .arg(returnedSegId);
        }
        return false;
    }
    if (segmentCount) {
        *segmentCount = returnedSegCount;
    }
    if (segmentData) {
        *segmentData = localData;
    }
    Q_UNUSED(segLen);
    return true;
}

bool LaserSession::waitTaskCompleted(quint16 expectedCounter, quint8 expectedCommand,
                                     int taskTimeoutMs, QueryTaskResult* result,
                                     QString* errorMessage, SessionErrorKind* errorKind) {
    if (errorKind) {
        *errorKind = SessionErrorKind::None;
    }
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < taskTimeoutMs) {
        if (m_params.queryIntervalMs > 0) {
            QThread::msleep(static_cast<unsigned long>(m_params.queryIntervalMs));
        }

        QueryTaskResult queryResult;
        SessionErrorKind queryError = SessionErrorKind::None;
        QString queryErrorMessage;
        if (!query(QueryOp::Read, &queryResult, &queryErrorMessage, &queryError)) {
            if (queryError == SessionErrorKind::Transport) {
                continue;
            }
            if (errorKind) {
                *errorKind = queryError;
            }
            if (errorMessage) {
                *errorMessage = queryErrorMessage;
            }
            return false;
        }
        if (queryResult.lastCounter != expectedCounter || queryResult.lastCommand != expectedCommand) {
            continue;
        }
        if (result) {
            *result = queryResult;
        }
        if (queryResult.resultA == TaskResult::Running) {
            continue;
        }
        return true;
    }

    if (errorKind) {
        *errorKind = SessionErrorKind::Protocol;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("Task timeout");
    }
    return false;
}

bool LaserSession::collectAllSegments(ScanAggregateResult* result, QString* errorMessage,
                                      SessionErrorKind* errorKind) {
    if (errorKind) {
        *errorKind = SessionErrorKind::None;
    }
    QByteArray aggregated;
    quint32 expectedSegments = 0;
    int receivedSegments = 0;

    for (quint32 segmentIndex = 0;; ++segmentIndex) {
        quint32 segmentCount = 0;
        QByteArray segmentData;
        if (!readSegment(segmentIndex, &segmentCount, &segmentData, errorMessage, errorKind)) {
            return false;
        }
        if (segmentCount == 0) {
            if (errorKind) {
                *errorKind = SessionErrorKind::Protocol;
            }
            if (errorMessage) {
                *errorMessage = QStringLiteral("Device returned zero segment count");
            }
            return false;
        }
        if (segmentIndex == 0) {
            expectedSegments = segmentCount;
        } else if (segmentCount != expectedSegments) {
            if (errorKind) {
                *errorKind = SessionErrorKind::Protocol;
            }
            if (errorMessage) {
                *errorMessage = QStringLiteral("Segment count changed during transfer");
            }
            return false;
        }

        aggregated.append(segmentData);
        ++receivedSegments;
        if (segmentIndex + 1 >= expectedSegments) {
            break;
        }
    }

    if (result) {
        result->segmentCount = receivedSegments;
        result->byteCount = aggregated.size();
        result->data = aggregated;
    }
    return true;
}

} // namespace laser_radar
