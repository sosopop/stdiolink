#include "driver_3d_temp_scanner/thermal_session.h"

#include <QElapsedTimer>
#include <QThread>

namespace temp_scanner {

ThermalSession::ThermalSession(IThermalTransport* transport)
    : m_transport(transport) {}

ThermalSession::~ThermalSession() {
    close();
}

bool ThermalSession::open(const ThermalTransportParams& params, QString* errorMessage) {
    m_params = params;
    return m_transport->open(params, errorMessage);
}

void ThermalSession::close() {
    if (m_transport) {
        m_transport->close();
    }
}

bool ThermalSession::transact(const QByteArray& request,
                              quint8 expectedFunctionCode,
                              int expectedNormalResponseSize,
                              QByteArray* responseFrame,
                              QString* errorMessage) {
    if (errorMessage) {
        errorMessage->clear();
    }
    if (!m_transport->writeFrame(request, m_params.timeoutMs, errorMessage)) {
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
            break;
        }
        buffer.append(chunk);
        if (buffer.size() < 5) {
            continue;
        }

        const quint8 deviceAddr = static_cast<quint8>(buffer[0]);
        const quint8 functionCode = static_cast<quint8>(buffer[1]);
        if (deviceAddr != m_params.deviceAddr) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Device address mismatch");
            }
            return false;
        }

        int expectedSize = expectedNormalResponseSize;
        if (functionCode == static_cast<quint8>(expectedFunctionCode | 0x80U)) {
            expectedSize = 5;
        } else if (functionCode != expectedFunctionCode) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unexpected function code");
            }
            return false;
        }

        if (buffer.size() < expectedSize) {
            continue;
        }

        if (responseFrame) {
            *responseFrame = buffer.left(expectedSize);
        }
        return true;
    }

    if (errorMessage) {
        if (!buffer.isEmpty()) {
            *errorMessage = QStringLiteral("Incomplete response frame");
        } else if (!transportError.isEmpty()) {
            *errorMessage = transportError;
        } else {
            *errorMessage = QStringLiteral("Read timeout");
        }
    }
    return false;
}

bool ThermalSession::writeSingleRegister(quint16 registerAddress, quint16 value, QString* errorMessage) {
    const QByteArray request = buildWriteSingleRegisterRequest(
        m_params.deviceAddr, registerAddress, value);
    QByteArray response;
    if (!transact(
            request,
            static_cast<quint8>(FunctionCode::WriteSingleRegister),
            expectedWriteSingleRegisterResponseSize(),
            &response,
            errorMessage)) {
        return false;
    }

    ExceptionFrame exceptionFrame;
    const ParseStatus status = parseWriteSingleRegisterResponse(
        response, m_params.deviceAddr, registerAddress, value, &exceptionFrame, errorMessage);
    return status == ParseStatus::Ok;
}

bool ThermalSession::readRegisters(quint16 startRegister,
                                   quint16 registerCount,
                                   QVector<quint16>* registers,
                                   QString* errorMessage) {
    const QByteArray request = buildReadHoldingRegistersRequest(
        m_params.deviceAddr, startRegister, registerCount);
    QByteArray response;
    if (!transact(
            request,
            static_cast<quint8>(FunctionCode::ReadHoldingRegisters),
            expectedReadHoldingRegistersResponseSize(registerCount),
            &response,
            errorMessage)) {
        return false;
    }

    ExceptionFrame exceptionFrame;
    const ParseStatus status = parseReadHoldingRegistersResponse(
        response, m_params.deviceAddr, registerCount, registers, &exceptionFrame, errorMessage);
    return status == ParseStatus::Ok;
}

bool ThermalSession::capture(CaptureResult* result, QString* errorMessage) {
    if (!writeSingleRegister(kRegisterCaptureControl, kCaptureStartValue, errorMessage)) {
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    bool captureDone = false;
    while (timer.elapsed() < m_params.scanTimeoutMs) {
        if (m_params.pollIntervalMs > 0) {
            QThread::msleep(static_cast<unsigned long>(m_params.pollIntervalMs));
        }

        QVector<quint16> stateRegisters;
        if (!readRegisters(kRegisterCaptureControl, 1, &stateRegisters, errorMessage)) {
            return false;
        }
        if (stateRegisters.size() != 1) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Capture state response is invalid");
            }
            return false;
        }

        const quint16 state = stateRegisters[0];
        if (state == kCaptureSuccessValue) {
            captureDone = true;
            break;
        }
        if (state == kCaptureFailureValue) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Device reported capture failure");
            }
            return false;
        }
    }

    if (!captureDone) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Capture timeout");
        }
        return false;
    }

    QVector<quint16> rawPixels;
    if (!readRegisters(0, kImagePixelCount, &rawPixels, errorMessage)) {
        return false;
    }
    if (rawPixels.size() != kImagePixelCount) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Image register count mismatch");
        }
        return false;
    }

    CaptureResult localResult;
    localResult.rawTemperatures = rawPixels;
    localResult.temperaturesDegC.reserve(rawPixels.size());
    localResult.minTempDegC = rawPixelToTemperatureDegC(rawPixels[0]);
    localResult.maxTempDegC = localResult.minTempDegC;
    for (quint16 rawValue : rawPixels) {
        const double temp = rawPixelToTemperatureDegC(rawValue);
        localResult.temperaturesDegC.append(temp);
        if (temp < localResult.minTempDegC) {
            localResult.minTempDegC = temp;
        }
        if (temp > localResult.maxTempDegC) {
            localResult.maxTempDegC = temp;
        }
    }

    if (result) {
        *result = localResult;
    }
    return true;
}

} // namespace temp_scanner
