#include "driver_3d_temp_scanner/protocol_codec.h"

#include <QDataStream>
#include <QIODevice>

namespace temp_scanner {

namespace {

constexpr quint16 kCrc16Table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

QByteArray finalizeRequest(const QByteArray& payload) {
    QByteArray request = payload;
    const quint16 crc = calculateModbusCrc16(payload);
    request.append(static_cast<char>(crc & 0xFF));
    request.append(static_cast<char>((crc >> 8) & 0xFF));
    return request;
}

ParseStatus parseCommonHeader(const QByteArray& frame,
                              quint8 expectedDeviceAddr,
                              quint8 expectedFunctionCode,
                              ExceptionFrame* exceptionFrame,
                              QString* errorMessage) {
    if (frame.size() < 5) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Frame too short");
        }
        return ParseStatus::NeedMore;
    }
    if (!verifyModbusCrc16(frame)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CRC error");
        }
        return ParseStatus::CrcError;
    }

    const quint8 deviceAddr = static_cast<quint8>(frame[0]);
    if (deviceAddr != expectedDeviceAddr) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Device address mismatch");
        }
        return ParseStatus::AddressMismatch;
    }

    const quint8 functionCode = static_cast<quint8>(frame[1]);
    if (functionCode == static_cast<quint8>(expectedFunctionCode | 0x80U)) {
        if (exceptionFrame) {
            exceptionFrame->functionCode = functionCode;
            exceptionFrame->exceptionCode = static_cast<quint8>(frame[2]);
        }
        if (errorMessage) {
            *errorMessage = modbusExceptionMessage(static_cast<quint8>(frame[2]));
        }
        return ParseStatus::ExceptionResponse;
    }
    if (functionCode != expectedFunctionCode) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Function code mismatch");
        }
        return ParseStatus::FunctionMismatch;
    }
    return ParseStatus::Ok;
}

} // namespace

QByteArray buildReadHoldingRegistersRequest(quint8 deviceAddr, quint16 startRegister, quint16 registerCount) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << deviceAddr
           << static_cast<quint8>(FunctionCode::ReadHoldingRegisters)
           << startRegister
           << registerCount;
    return finalizeRequest(payload);
}

QByteArray buildWriteSingleRegisterRequest(quint8 deviceAddr, quint16 registerAddress, quint16 value) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << deviceAddr
           << static_cast<quint8>(FunctionCode::WriteSingleRegister)
           << registerAddress
           << value;
    return finalizeRequest(payload);
}

int expectedReadHoldingRegistersResponseSize(quint16 registerCount) {
    return 1 + 1 + 1 + registerCount * 2 + 2;
}

int expectedWriteSingleRegisterResponseSize() {
    return 8;
}

quint16 calculateModbusCrc16(const QByteArray& data) {
    quint16 crc = 0xFFFF;
    for (char ch : data) {
        const quint8 byte = static_cast<quint8>(ch);
        crc = static_cast<quint16>((crc >> 8) ^ kCrc16Table[(crc ^ byte) & 0xFF]);
    }
    return crc;
}

bool verifyModbusCrc16(const QByteArray& frame) {
    if (frame.size() < 4) {
        return false;
    }
    const QByteArray withoutCrc = frame.left(frame.size() - 2);
    const quint16 actual = calculateModbusCrc16(withoutCrc);
    const quint16 expected = static_cast<quint8>(frame[frame.size() - 2]) |
                             (static_cast<quint16>(static_cast<quint8>(frame[frame.size() - 1])) << 8);
    return actual == expected;
}

ParseStatus parseReadHoldingRegistersResponse(const QByteArray& frame,
                                             quint8 expectedDeviceAddr,
                                             quint16 expectedRegisterCount,
                                             QVector<quint16>* registers,
                                             ExceptionFrame* exceptionFrame,
                                             QString* errorMessage) {
    const ParseStatus headerStatus = parseCommonHeader(
        frame,
        expectedDeviceAddr,
        static_cast<quint8>(FunctionCode::ReadHoldingRegisters),
        exceptionFrame,
        errorMessage);
    if (headerStatus != ParseStatus::Ok) {
        return headerStatus;
    }

    const int expectedSize = expectedReadHoldingRegistersResponseSize(expectedRegisterCount);
    if (frame.size() < expectedSize) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Incomplete register response");
        }
        return ParseStatus::NeedMore;
    }
    if (frame.size() != expectedSize) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Register response length mismatch");
        }
        return ParseStatus::InvalidLength;
    }

    const quint8 byteCount = static_cast<quint8>(frame[2]);
    const quint16 expectedByteCount = static_cast<quint16>(expectedRegisterCount * 2);
    if (expectedRegisterCount > 255) {
        if (byteCount != 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Large register response must use byte_count=0");
            }
            return ParseStatus::InvalidByteCount;
        }
    } else if (byteCount != expectedByteCount) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Register response byte_count mismatch");
        }
        return ParseStatus::InvalidByteCount;
    }

    if (registers) {
        registers->clear();
        registers->reserve(expectedRegisterCount);
        for (int i = 0; i < expectedRegisterCount; ++i) {
            const int offset = 3 + i * 2;
            const quint16 value = (static_cast<quint8>(frame[offset]) << 8) |
                                  static_cast<quint8>(frame[offset + 1]);
            registers->append(value);
        }
    }
    return ParseStatus::Ok;
}

ParseStatus parseWriteSingleRegisterResponse(const QByteArray& frame,
                                            quint8 expectedDeviceAddr,
                                            quint16 expectedRegisterAddress,
                                            quint16 expectedValue,
                                            ExceptionFrame* exceptionFrame,
                                            QString* errorMessage) {
    const ParseStatus headerStatus = parseCommonHeader(
        frame,
        expectedDeviceAddr,
        static_cast<quint8>(FunctionCode::WriteSingleRegister),
        exceptionFrame,
        errorMessage);
    if (headerStatus != ParseStatus::Ok) {
        return headerStatus;
    }

    if (frame.size() < expectedWriteSingleRegisterResponseSize()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Incomplete write response");
        }
        return ParseStatus::NeedMore;
    }
    if (frame.size() != expectedWriteSingleRegisterResponseSize()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Write response length mismatch");
        }
        return ParseStatus::InvalidLength;
    }

    const quint16 echoedRegister = (static_cast<quint8>(frame[2]) << 8) |
                                   static_cast<quint8>(frame[3]);
    const quint16 echoedValue = (static_cast<quint8>(frame[4]) << 8) |
                                static_cast<quint8>(frame[5]);
    if (echoedRegister != expectedRegisterAddress || echoedValue != expectedValue) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Write response echo mismatch");
        }
        return ParseStatus::EchoMismatch;
    }
    return ParseStatus::Ok;
}

QString modbusExceptionMessage(quint8 exceptionCode) {
    switch (exceptionCode) {
    case 1:
        return QStringLiteral("Illegal function");
    case 2:
        return QStringLiteral("Illegal data address");
    case 3:
        return QStringLiteral("Illegal data value");
    case 4:
        return QStringLiteral("Slave device failure");
    default:
        return QStringLiteral("Modbus exception: %1").arg(exceptionCode);
    }
}

double rawPixelToTemperatureDegC(quint16 rawValue) {
    return static_cast<double>(rawValue) * 0.01 - 273.15;
}

} // namespace temp_scanner
