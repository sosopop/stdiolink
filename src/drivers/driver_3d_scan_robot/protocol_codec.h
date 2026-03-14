#pragma once

#include <QByteArray>
#include <QString>
#include <QtEndian>

namespace scan_robot {

// ── 主命令号（JD3D 通道）──────────────────────────────────
// 编号来源: 协议文档 + 参考 C++ v3dradarproto
namespace CmdId {
constexpr quint8 RegRead            = 1;
constexpr quint8 RegWrite           = 2;
constexpr quint8 TestCom            = 3;   // 同时用于 get_addr（广播地址255）
constexpr quint8 Calibration        = 4;
constexpr quint8 MoveDist           = 5;   // 协议文档“获取指定角度的距离”
constexpr quint8 Query              = 6;
constexpr quint8 Move               = 7;
constexpr quint8 GetDistance         = 8;
constexpr quint8 ScanLine           = 10;
constexpr quint8 ScanFrame          = 11;
constexpr quint8 GetData            = 12;
} // namespace CmdId

// ── 中断式命令号（JD3I 通道）──────────────────────────────
namespace InsertCmdId {
constexpr quint8 ScanProgress       = 3;
constexpr quint8 Test               = 6;
constexpr quint8 ScanCancel         = 8;
} // namespace InsertCmdId

// ── 寄存器号 ────────────────────────────────────────────
namespace RegId {
constexpr quint16 WorkMode           = 0;
constexpr quint16 DeviceStatus       = 1;
constexpr quint16 McuTemperature     = 4;
constexpr quint16 BoardTemperature   = 5;
constexpr quint16 XProximitySwitch   = 6;
constexpr quint16 YProximitySwitch   = 7;
constexpr quint16 XMotorCalib        = 8;
constexpr quint16 YMotorCalib        = 9;
constexpr quint16 DirectionAngle     = 10;
constexpr quint16 FirmwareVersion    = 12;
constexpr quint16 SlaveAddress       = 13;
constexpr quint16 SegmentSize        = 15;
} // namespace RegId

// ── 长任务结果码 ────────────────────────────────────────
namespace TaskResult {
constexpr quint32 StillRunning  = 0;
constexpr quint32 Success       = 10;
constexpr quint32 Failed        = 20;
} // namespace TaskResult

// ── 帧魔数 ──────────────────────────────────────────────
constexpr char kMagicJD3D[] = "JD3D";
constexpr char kMagicJD3I[] = "JD3I";
constexpr int  kMagicLen    = 4;
constexpr int  kHeaderLen   = 8;   // magic(4) + counter(1) + option(1) + addr(1) + cmd(1)
constexpr int  kCrcLen      = 4;
constexpr int  kMinFrameLen = kHeaderLen + kCrcLen + 4; // 至少 4 字节 payload

// ── 解码后的协议帧 ──────────────────────────────────────
struct RadarFrame {
    quint8     counter = 0;
    quint8     option  = 0;
    quint8     addr    = 0;
    quint8     command = 0;
    QByteArray payload;
    quint32    crc32   = 0;
};

// ── CRC32-STM32 ─────────────────────────────────────────
quint32 crc32Stm32(const QByteArray& data);

// ── 编码 ────────────────────────────────────────────────
QByteArray encodeFrame(quint8 counter, quint8 addr, quint8 command,
                       const QByteArray& payload, bool interrupt = false);

// ── 解码状态 ────────────────────────────────────────────
enum class DecodeStatus {
    Ok,             // 成功解码一帧
    Incomplete,     // 数据不足，需要更多字节
    BadMagic,       // 帧头不匹配
    ChannelMismatch,// 主/中断通道不匹配
    CrcError,       // CRC 校验失败
    AddrMismatch,   // 地址不匹配
    CmdMismatch,    // 命令不匹配
};

enum class FrameChannel {
    Any,
    Main,
    Interrupt,
};

QString decodeStatusToString(DecodeStatus s);

// 尝试从 buffer 开头解码一帧。
// expectAddr / expectCmd == 0 时跳过对应校验。
// 成功后 buffer 不会被清除；调用方需自行管理已消费的字节量。
DecodeStatus tryDecodeFrame(const QByteArray& buffer,
                            quint8 expectAddr,
                            quint8 expectCmd,
                            RadarFrame* frame,
                            QString* errorMessage,
                            FrameChannel expectedChannel = FrameChannel::Any,
                            int* consumedBytes = nullptr,
                            bool finalChunk = false);

// ── 便捷 payload 构建 ──────────────────────────────────
QByteArray makeU32Payload(quint32 value);
QByteArray makeRegPayload(quint16 regAddr, quint32 value);
QByteArray makeScanLinePayload(quint16 angleX, quint16 beginY, quint16 endY,
                               quint16 stepY, quint16 speedY);
QByteArray makeScanFramePayload(quint16 beginX, quint16 endX, quint16 stepX,
                                quint16 beginY, quint16 endY, quint16 stepY,
                                quint16 speedY);

// ── 便捷 payload 解析 ──────────────────────────────────
bool parseRegResponse(const QByteArray& payload, quint16* regAddr, quint32* value);
bool parseQueryResponse(const QByteArray& payload, quint8* lastCounter,
                        quint8* lastCommand, quint32* resultCode);
bool parseSegmentResponse(const QByteArray& payload, quint32* segIndex,
                          quint16* segLen, QByteArray* segData);

} // namespace scan_robot
