#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace temp_scanner {

constexpr quint16 kRegisterCaptureControl = 60000;
constexpr quint16 kCaptureStartValue = 100;
constexpr quint16 kCaptureSuccessValue = 10;
constexpr quint16 kCaptureFailureValue = 20;
constexpr int kImageWidth = 32;
constexpr int kImageHeight = 24;
constexpr int kImagePixelCount = kImageWidth * kImageHeight;

enum class FunctionCode : quint8 {
    ReadHoldingRegisters = 0x03,
    WriteSingleRegister = 0x06,
};

enum class ParseStatus {
    Ok,
    NeedMore,
    CrcError,
    AddressMismatch,
    FunctionMismatch,
    ExceptionResponse,
    InvalidLength,
    InvalidByteCount,
    EchoMismatch,
};

struct ExceptionFrame {
    quint8 functionCode = 0;
    quint8 exceptionCode = 0;
};

QByteArray buildReadHoldingRegistersRequest(quint8 deviceAddr, quint16 startRegister, quint16 registerCount);
QByteArray buildWriteSingleRegisterRequest(quint8 deviceAddr, quint16 registerAddress, quint16 value);

int expectedReadHoldingRegistersResponseSize(quint16 registerCount);
int expectedWriteSingleRegisterResponseSize();

quint16 calculateModbusCrc16(const QByteArray& data);
bool verifyModbusCrc16(const QByteArray& frame);

ParseStatus parseReadHoldingRegistersResponse(const QByteArray& frame,
                                             quint8 expectedDeviceAddr,
                                             quint16 expectedRegisterCount,
                                             QVector<quint16>* registers,
                                             ExceptionFrame* exceptionFrame = nullptr,
                                             QString* errorMessage = nullptr);

ParseStatus parseWriteSingleRegisterResponse(const QByteArray& frame,
                                            quint8 expectedDeviceAddr,
                                            quint16 expectedRegisterAddress,
                                            quint16 expectedValue,
                                            ExceptionFrame* exceptionFrame = nullptr,
                                            QString* errorMessage = nullptr);

QString modbusExceptionMessage(quint8 exceptionCode);
double rawPixelToTemperatureDegC(quint16 rawValue);

} // namespace temp_scanner
