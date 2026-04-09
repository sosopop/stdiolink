#pragma once

#include <QByteArray>
#include <QString>
#include <QtEndian>

namespace laser_radar {

namespace CmdId {
constexpr quint8 ReadReg = 1;
constexpr quint8 WriteReg = 2;
constexpr quint8 Test = 3;
constexpr quint8 CalibX = 4;
constexpr quint8 Cancel = 5;
constexpr quint8 Query = 6;
constexpr quint8 MoveX = 7;
constexpr quint8 CalibLidar = 8;
constexpr quint8 ScanField = 11;
constexpr quint8 GetData = 12;
constexpr quint8 SetScanMode = 14;
constexpr quint8 SetImagingMode = 15;
constexpr quint8 Reboot = 16;
}

namespace RegId {
constexpr quint16 WorkMode = 1;
constexpr quint16 DeviceStatus = 2;
constexpr quint16 DeviceCode = 3;
constexpr quint16 LidarModelCode = 4;
constexpr quint16 DistanceUnit = 5;
constexpr quint16 TimeSinceBoot = 6;
constexpr quint16 FirmwareVersion = 14;
constexpr quint16 DataBlockSize = 18;
constexpr quint16 XAxisRatio = 22;
constexpr quint16 TotalBytesForTransfer = 40;
}

namespace QueryOp {
constexpr quint32 Read = 100;
constexpr quint32 Reset = 200;
}

namespace ImmediateFeedback {
constexpr quint32 Accepted = 10;
constexpr quint32 Rejected = 20;
}

namespace CancelFeedback {
constexpr quint8 CanStop = 10;
constexpr quint8 CannotStop = 20;
constexpr quint8 AlreadyStopped = 30;
}

namespace TaskResult {
constexpr quint32 Idle = 0;
constexpr quint32 Running = 1000;
constexpr quint32 FailedBase = 2000;
constexpr quint32 Cancelled = 3000;
constexpr quint32 Success = 4000;
constexpr quint32 SuccessWithBlankScanline = 4001;
}

constexpr quint8 kDeviceAddr = 0;
constexpr char kMagic[] = "LIDA";
constexpr int kMagicLen = 4;
constexpr int kLengthFieldLen = 2;
constexpr int kHeaderLen = 10;
constexpr int kCrcLen = 4;
constexpr int kMinPayloadLen = 4;
constexpr int kMinFrameLen = 18;
constexpr int kMaxFrameLen = 1400;

struct LaserFrame {
    quint16 counter = 0;
    quint8 addr = 0;
    quint8 command = 0;
    quint16 length = 0;
    QByteArray payload;
    quint32 crc32 = 0;
};

enum class DecodeStatus {
    Ok,
    Incomplete,
    BadMagic,
    InvalidLength,
    CrcError,
    AddrMismatch,
    CmdMismatch,
};

quint32 crc32Stm32(const QByteArray& data);
QByteArray encodeFrame(quint16 counter, quint8 addr, quint8 command, const QByteArray& payload);
QString decodeStatusToString(DecodeStatus status);

DecodeStatus tryDecodeFrame(const QByteArray& buffer,
                            int expectAddr,
                            int expectCmd,
                            LaserFrame* frame,
                            QString* errorMessage,
                            int* consumedBytes = nullptr,
                            bool finalChunk = false);

QByteArray makeU32Payload(quint32 value);
QByteArray makeReadRegPayload(quint16 regId);
QByteArray makeWriteRegPayload(quint16 regId, quint32 value);
QByteArray makeScanFieldPayload(qint32 beginXMilliDeg,
                                qint32 endXMilliDeg,
                                qint32 stepXMicroDeg,
                                qint32 beginYMilliDeg,
                                qint32 endYMilliDeg,
                                qint32 stepYMicroDeg);

bool parseU32Payload(const QByteArray& payload, quint32* value);
bool parseRegResponse(const QByteArray& payload, quint16* regId, quint32* value);
bool parseCancelResponse(const QByteArray& payload, quint16* lastCounter, quint8* lastCode,
                         quint8* resultCode);
bool parseQueryResponse(const QByteArray& payload, quint16* lastCounter, quint8* lastCode,
                        quint32* resultA, quint32* resultB);
bool parseSegmentResponse(const QByteArray& payload, quint32* segId, quint32* segCount,
                          quint16* segLen, QByteArray* segData);

QString commandName(quint8 command);
QString workModeName(quint32 modeCode);

} // namespace laser_radar
