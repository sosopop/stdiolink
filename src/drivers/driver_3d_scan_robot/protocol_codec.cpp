#include "driver_3d_scan_robot/protocol_codec.h"

#include <QtEndian>
#include <cstring>

namespace scan_robot {

// ── CRC32-STM32 (MPEG-2 variant, per-byte, word-reversed) ─
static const quint32 kCrc32Table[256] = {
    0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
    0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61, 0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
    0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
    0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd,
    0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039, 0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5,
    0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
    0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
    0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d,
    0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
    0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca,
    0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02,
    0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
    0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692,
    0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6, 0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,
    0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
    0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a,
    0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637, 0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
    0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
    0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b,
    0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff, 0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623,
    0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
    0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
    0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7, 0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b,
    0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
    0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c,
    0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24,
    0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
    0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,
    0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c,
    0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
    0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c,
    0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668, 0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4,
};

static quint32 crc32Mpeg2Step(quint8 byteIn, quint32 crcIn) {
    quint32 index = ((crcIn >> 24) ^ static_cast<quint32>(byteIn));
    return (crcIn << 8) ^ kCrc32Table[index];
}

quint32 crc32Stm32(const QByteArray& data) {
    quint32 crc = 0xFFFFFFFF;
    const int len = data.size();
    const int ib = len % 4;
    const int ia = len - ib;

    for (int x = 0; x < ia / 4; ++x) {
        for (int y = 4 * x + 3; y >= 4 * x; --y) {
            crc = crc32Mpeg2Step(static_cast<quint8>(data[y]), crc);
        }
    }

    if (ib) {
        quint8 m[4] = {0, 0, 0, 0};
        for (int x = 0; x < ib; ++x) {
            m[x] = static_cast<quint8>(data[ia + x]);
        }
        for (int x = 3; x >= 0; --x) {
            crc = crc32Mpeg2Step(m[x], crc);
        }
    }
    return crc;
}

// ── 编码 ────────────────────────────────────────────────

QByteArray encodeFrame(quint8 counter, quint8 addr, quint8 command,
                       const QByteArray& payload, bool interrupt) {
    const int payloadLen = payload.size();
    QByteArray frame;
    frame.reserve(kHeaderLen + payloadLen + kCrcLen);

    // magic
    frame.append(interrupt ? kMagicJD3I : kMagicJD3D, kMagicLen);
    // header fields
    frame.append(static_cast<char>(counter));
    frame.append(static_cast<char>(0));  // option
    frame.append(static_cast<char>(addr));
    frame.append(static_cast<char>(command));
    // payload
    frame.append(payload);

    // CRC over bytes [4..end) i.e. from counter to end of payload
    QByteArray crcInput = frame.mid(kMagicLen);
    quint32 crc = crc32Stm32(crcInput);

    quint8 crcBytes[4];
    qToBigEndian(crc, crcBytes);
    frame.append(reinterpret_cast<const char*>(crcBytes), 4);

    return frame;
}

// ── 解码 ────────────────────────────────────────────────

QString decodeStatusToString(DecodeStatus s) {
    switch (s) {
    case DecodeStatus::Ok:           return QStringLiteral("Ok");
    case DecodeStatus::Incomplete:   return QStringLiteral("Incomplete");
    case DecodeStatus::BadMagic:     return QStringLiteral("BadMagic");
    case DecodeStatus::ChannelMismatch: return QStringLiteral("ChannelMismatch");
    case DecodeStatus::CrcError:     return QStringLiteral("CrcError");
    case DecodeStatus::AddrMismatch: return QStringLiteral("AddrMismatch");
    case DecodeStatus::CmdMismatch:  return QStringLiteral("CmdMismatch");
    }
    return QStringLiteral("Unknown");
}

DecodeStatus tryDecodeFrame(const QByteArray& buffer,
                            quint8 expectAddr,
                            quint8 expectCmd,
                            RadarFrame* frame,
                            QString* errorMessage,
                            FrameChannel expectedChannel,
                            int* consumedBytes,
                            bool finalChunk) {
    if (consumedBytes) {
        *consumedBytes = 0;
    }
    if (buffer.size() < kMinFrameLen) {
        if (errorMessage) *errorMessage = QStringLiteral("buffer too short");
        return DecodeStatus::Incomplete;
    }

    // Validate magic
    const char* data = buffer.constData();
    bool isJD3D = (std::memcmp(data, kMagicJD3D, kMagicLen) == 0);
    bool isJD3I = (std::memcmp(data, kMagicJD3I, kMagicLen) == 0);
    if (!isJD3D && !isJD3I) {
        if (errorMessage) *errorMessage = QStringLiteral("bad magic");
        return DecodeStatus::BadMagic;
    }
    if ((expectedChannel == FrameChannel::Main && !isJD3D)
        || (expectedChannel == FrameChannel::Interrupt && !isJD3I)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("channel mismatch");
        }
        return DecodeStatus::ChannelMismatch;
    }

    // Parse header
    frame->counter = static_cast<quint8>(data[4]);
    frame->option  = static_cast<quint8>(data[5]);
    frame->addr    = static_cast<quint8>(data[6]);
    frame->command = static_cast<quint8>(data[7]);

    // Address check
    if (expectAddr != 0 && frame->addr != expectAddr) {
        if (errorMessage) {
            *errorMessage = QString("addr mismatch: expected %1, got %2")
                                .arg(expectAddr).arg(frame->addr);
        }
        return DecodeStatus::AddrMismatch;
    }

    // Command check
    if (expectCmd != 0 && frame->command != expectCmd) {
        if (errorMessage) {
            *errorMessage = QString("cmd mismatch: expected %1, got %2")
                                .arg(expectCmd).arg(frame->command);
        }
        return DecodeStatus::CmdMismatch;
    }

    DecodeStatus bestStatus = finalChunk ? DecodeStatus::CrcError : DecodeStatus::Incomplete;
    QString bestError = finalChunk ? QStringLiteral("CRC mismatch") : QStringLiteral("frame incomplete");

    for (int frameLen = kMinFrameLen; frameLen <= buffer.size(); ++frameLen) {
        const int payloadLen = frameLen - kHeaderLen - kCrcLen;
        if (payloadLen < 0) {
            continue;
        }

        const QByteArray crcInput = buffer.mid(kMagicLen, frameLen - kMagicLen - kCrcLen);
        const quint32 computedCrc = crc32Stm32(crcInput);
        const quint32 frameCrc = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(data + frameLen - kCrcLen));

        if (computedCrc != frameCrc) {
            bestStatus = finalChunk ? DecodeStatus::CrcError : DecodeStatus::Incomplete;
            if (finalChunk) {
                bestError = QString("CRC mismatch: computed 0x%1, frame 0x%2")
                                .arg(computedCrc, 8, 16, QLatin1Char('0'))
                                .arg(frameCrc, 8, 16, QLatin1Char('0'));
            }
            continue;
        }

        frame->payload = buffer.mid(kHeaderLen, payloadLen);
        frame->crc32 = frameCrc;
        if (consumedBytes) {
            *consumedBytes = frameLen;
        }
        return DecodeStatus::Ok;
    }

    if (errorMessage) {
        *errorMessage = bestError;
    }
    return bestStatus;
}

// ── Payload 构建 ────────────────────────────────────────

QByteArray makeU32Payload(quint32 value) {
    quint8 buf[4];
    qToBigEndian(value, buf);
    return QByteArray(reinterpret_cast<const char*>(buf), 4);
}

QByteArray makeRegPayload(quint16 regAddr, quint32 value) {
    quint8 buf[6];
    qToBigEndian(regAddr, buf);
    qToBigEndian(value, buf + 2);
    return QByteArray(reinterpret_cast<const char*>(buf), 6);
}

QByteArray makeScanLinePayload(quint16 angleX, quint16 beginY, quint16 endY,
                               quint16 stepY, quint16 sampleCount) {
    quint8 buf[10];
    qToBigEndian(angleX, buf);
    qToBigEndian(beginY, buf + 2);
    qToBigEndian(endY, buf + 4);
    qToBigEndian(stepY, buf + 6);
    qToBigEndian(sampleCount, buf + 8);
    return QByteArray(reinterpret_cast<const char*>(buf), 10);
}

QByteArray makeScanFramePayload(quint16 beginX, quint16 endX, quint16 stepX,
                                quint16 beginY, quint16 endY, quint16 stepY,
                                quint16 sampleCount) {
    quint8 buf[14];
    qToBigEndian(beginX, buf);
    qToBigEndian(endX, buf + 2);
    qToBigEndian(stepX, buf + 4);
    qToBigEndian(beginY, buf + 6);
    qToBigEndian(endY, buf + 8);
    qToBigEndian(stepY, buf + 10);
    qToBigEndian(sampleCount, buf + 12);
    return QByteArray(reinterpret_cast<const char*>(buf), 14);
}

// ── Payload 解析 ────────────────────────────────────────

bool parseRegResponse(const QByteArray& payload, quint16* regAddr, quint32* value) {
    if (payload.size() < 6) return false;
    const auto* d = reinterpret_cast<const uchar*>(payload.constData());
    *regAddr = qFromBigEndian<quint16>(d);
    *value   = qFromBigEndian<quint32>(d + 2);
    return true;
}

bool parseQueryResponse(const QByteArray& payload, quint8* lastCounter,
                        quint8* lastCommand, quint32* resultCode) {
    if (payload.size() < 6) return false;
    *lastCounter = static_cast<quint8>(payload[0]);
    *lastCommand = static_cast<quint8>(payload[1]);
    const auto* d = reinterpret_cast<const uchar*>(payload.constData());
    *resultCode  = qFromBigEndian<quint32>(d + 2);
    return true;
}

bool parseSegmentResponse(const QByteArray& payload, quint32* segIndex,
                          quint16* segLen, QByteArray* segData) {
    if (payload.size() < 6) return false;
    const auto* d = reinterpret_cast<const uchar*>(payload.constData());
    *segIndex = qFromBigEndian<quint32>(d);
    *segLen   = qFromBigEndian<quint16>(d + 4);
    if (static_cast<int>(*segLen) + 6 > payload.size()) return false;
    *segData = payload.mid(6, *segLen);
    return true;
}

} // namespace scan_robot
