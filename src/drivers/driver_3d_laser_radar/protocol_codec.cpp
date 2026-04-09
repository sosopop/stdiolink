#include "driver_3d_laser_radar/protocol_codec.h"

#include <cstring>

namespace laser_radar {

namespace {

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

quint32 crc32Mpeg2Step(quint8 byteIn, quint32 crcIn) {
    const quint32 index = ((crcIn >> 24) ^ static_cast<quint32>(byteIn)) & 0xFFu;
    return (crcIn << 8) ^ kCrc32Table[index];
}

void appendU16(QByteArray& bytes, quint16 value) {
    quint8 buf[2];
    qToBigEndian(value, buf);
    bytes.append(reinterpret_cast<const char*>(buf), 2);
}

void appendU32(QByteArray& bytes, quint32 value) {
    quint8 buf[4];
    qToBigEndian(value, buf);
    bytes.append(reinterpret_cast<const char*>(buf), 4);
}

} // namespace

quint32 crc32Stm32(const QByteArray& data) {
    quint32 crc = 0xFFFFFFFFu;
    const int trailingBytes = data.size() % 4;
    const int alignedSize = data.size() - trailingBytes;

    for (int word = 0; word < alignedSize / 4; ++word) {
        for (int index = 4 * word + 3; index >= 4 * word; --index) {
            crc = crc32Mpeg2Step(static_cast<quint8>(data[index]), crc);
        }
    }

    if (trailingBytes > 0) {
        quint8 tail[4] = {0, 0, 0, 0};
        for (int i = 0; i < trailingBytes; ++i) {
            tail[i] = static_cast<quint8>(data[alignedSize + i]);
        }
        for (int i = 3; i >= 0; --i) {
            crc = crc32Mpeg2Step(tail[i], crc);
        }
    }

    return crc;
}

QByteArray encodeFrame(quint16 counter, quint8 addr, quint8 command, const QByteArray& payload) {
    QByteArray frame;
    const quint16 length = static_cast<quint16>(2 + 1 + 1 + payload.size() + 4);
    frame.reserve(kHeaderLen + payload.size() + kCrcLen);
    frame.append(kMagic, kMagicLen);
    appendU16(frame, length);
    appendU16(frame, counter);
    frame.append(static_cast<char>(addr));
    frame.append(static_cast<char>(command));
    frame.append(payload);
    appendU32(frame, crc32Stm32(frame));
    return frame;
}

QString decodeStatusToString(DecodeStatus status) {
    switch (status) {
    case DecodeStatus::Ok: return QStringLiteral("Ok");
    case DecodeStatus::Incomplete: return QStringLiteral("Incomplete");
    case DecodeStatus::BadMagic: return QStringLiteral("BadMagic");
    case DecodeStatus::InvalidLength: return QStringLiteral("InvalidLength");
    case DecodeStatus::CrcError: return QStringLiteral("CrcError");
    case DecodeStatus::AddrMismatch: return QStringLiteral("AddrMismatch");
    case DecodeStatus::CmdMismatch: return QStringLiteral("CmdMismatch");
    }
    return QStringLiteral("Unknown");
}

DecodeStatus tryDecodeFrame(const QByteArray& buffer,
                            int expectAddr,
                            int expectCmd,
                            LaserFrame* frame,
                            QString* errorMessage,
                            int* consumedBytes,
                            bool finalChunk) {
    if (consumedBytes) {
        *consumedBytes = 0;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    if (buffer.size() < kMinFrameLen) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("buffer too short");
        }
        return DecodeStatus::Incomplete;
    }

    const char* data = buffer.constData();
    if (std::memcmp(data, kMagic, kMagicLen) != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("bad magic");
        }
        return DecodeStatus::BadMagic;
    }

    const auto* raw = reinterpret_cast<const uchar*>(data);
    const quint16 length = qFromBigEndian<quint16>(raw + 4);
    if (length < 8 + kMinPayloadLen || 6 + length < kMinFrameLen || 6 + length > kMaxFrameLen) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("invalid frame length");
        }
        return DecodeStatus::InvalidLength;
    }

    const int totalLength = 6 + static_cast<int>(length);
    if (buffer.size() < totalLength) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("frame incomplete");
        }
        return DecodeStatus::Incomplete;
    }

    const quint32 expectedCrc =
        qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(data + totalLength - kCrcLen));
    const quint32 actualCrc = crc32Stm32(buffer.left(totalLength - kCrcLen));
    if (actualCrc != expectedCrc) {
        if (errorMessage) {
            *errorMessage = finalChunk
                ? QString("CRC mismatch: computed 0x%1, frame 0x%2")
                      .arg(actualCrc, 8, 16, QLatin1Char('0'))
                      .arg(expectedCrc, 8, 16, QLatin1Char('0'))
                : QStringLiteral("frame incomplete");
        }
        return finalChunk ? DecodeStatus::CrcError : DecodeStatus::Incomplete;
    }

    const quint16 counter = qFromBigEndian<quint16>(raw + 6);
    const quint8 addr = static_cast<quint8>(data[8]);
    const quint8 command = static_cast<quint8>(data[9]);
    const int payloadLen = static_cast<int>(length) - 8;
    if (payloadLen < kMinPayloadLen) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("payload too short");
        }
        return DecodeStatus::InvalidLength;
    }

    if (expectAddr >= 0 && addr != static_cast<quint8>(expectAddr)) {
        if (errorMessage) {
            *errorMessage = QString("addr mismatch: expected %1, got %2").arg(expectAddr).arg(addr);
        }
        return DecodeStatus::AddrMismatch;
    }
    if (expectCmd >= 0 && command != static_cast<quint8>(expectCmd)) {
        if (errorMessage) {
            *errorMessage = QString("cmd mismatch: expected %1, got %2").arg(expectCmd).arg(command);
        }
        return DecodeStatus::CmdMismatch;
    }

    if (frame) {
        frame->counter = counter;
        frame->addr = addr;
        frame->command = command;
        frame->length = length;
        frame->payload = buffer.mid(kHeaderLen, payloadLen);
        frame->crc32 = expectedCrc;
    }
    if (consumedBytes) {
        *consumedBytes = totalLength;
    }
    return DecodeStatus::Ok;
}

QByteArray makeU32Payload(quint32 value) {
    QByteArray payload;
    appendU32(payload, value);
    return payload;
}

QByteArray makeReadRegPayload(quint16 regId) {
    QByteArray payload;
    appendU16(payload, regId);
    appendU32(payload, 100);
    return payload;
}

QByteArray makeWriteRegPayload(quint16 regId, quint32 value) {
    QByteArray payload;
    appendU16(payload, regId);
    appendU32(payload, value);
    return payload;
}

QByteArray makeScanFieldPayload(qint32 beginXMilliDeg,
                                qint32 endXMilliDeg,
                                qint32 stepXMicroDeg,
                                qint32 beginYMilliDeg,
                                qint32 endYMilliDeg,
                                qint32 stepYMicroDeg) {
    QByteArray payload;
    appendU32(payload, static_cast<quint32>(beginXMilliDeg));
    appendU32(payload, static_cast<quint32>(endXMilliDeg));
    appendU32(payload, static_cast<quint32>(stepXMicroDeg));
    appendU32(payload, static_cast<quint32>(beginYMilliDeg));
    appendU32(payload, static_cast<quint32>(endYMilliDeg));
    appendU32(payload, static_cast<quint32>(stepYMicroDeg));
    return payload;
}

bool parseU32Payload(const QByteArray& payload, quint32* value) {
    if (payload.size() != 4 || !value) {
        return false;
    }
    *value = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(payload.constData()));
    return true;
}

bool parseRegResponse(const QByteArray& payload, quint16* regId, quint32* value) {
    if (payload.size() != 6 || !regId || !value) {
        return false;
    }
    const auto* data = reinterpret_cast<const uchar*>(payload.constData());
    *regId = qFromBigEndian<quint16>(data);
    *value = qFromBigEndian<quint32>(data + 2);
    return true;
}

bool parseCancelResponse(const QByteArray& payload, quint16* lastCounter, quint8* lastCode,
                         quint8* resultCode) {
    if (payload.size() != 4 || !lastCounter || !lastCode || !resultCode) {
        return false;
    }
    const auto* data = reinterpret_cast<const uchar*>(payload.constData());
    *lastCounter = qFromBigEndian<quint16>(data);
    *lastCode = static_cast<quint8>(payload[2]);
    *resultCode = static_cast<quint8>(payload[3]);
    return true;
}

bool parseQueryResponse(const QByteArray& payload, quint16* lastCounter, quint8* lastCode,
                        quint32* resultA, quint32* resultB) {
    if (payload.size() != 11 || !lastCounter || !lastCode || !resultA || !resultB) {
        return false;
    }
    const auto* data = reinterpret_cast<const uchar*>(payload.constData());
    *lastCounter = qFromBigEndian<quint16>(data);
    *lastCode = static_cast<quint8>(payload[2]);
    *resultA = qFromBigEndian<quint32>(data + 3);
    *resultB = qFromBigEndian<quint32>(data + 7);
    return true;
}

bool parseSegmentResponse(const QByteArray& payload, quint32* segId, quint32* segCount,
                          quint16* segLen, QByteArray* segData) {
    if (payload.size() < 10 || !segId || !segCount || !segLen || !segData) {
        return false;
    }
    const auto* data = reinterpret_cast<const uchar*>(payload.constData());
    *segId = qFromBigEndian<quint32>(data);
    *segCount = qFromBigEndian<quint32>(data + 4);
    *segLen = qFromBigEndian<quint16>(data + 8);
    if (payload.size() != 10 + static_cast<int>(*segLen)) {
        return false;
    }
    *segData = payload.mid(10, *segLen);
    return true;
}

QString commandName(quint8 command) {
    switch (command) {
    case CmdId::ReadReg: return QStringLiteral("read_reg");
    case CmdId::WriteReg: return QStringLiteral("write_reg");
    case CmdId::Test: return QStringLiteral("test");
    case CmdId::CalibX: return QStringLiteral("calib_x");
    case CmdId::Cancel: return QStringLiteral("cancel");
    case CmdId::Query: return QStringLiteral("query");
    case CmdId::MoveX: return QStringLiteral("move_x");
    case CmdId::CalibLidar: return QStringLiteral("calib_lidar");
    case CmdId::ScanField: return QStringLiteral("scan_field");
    case CmdId::GetData: return QStringLiteral("get_data");
    case CmdId::SetScanMode: return QStringLiteral("set_scan_mode");
    case CmdId::SetImagingMode: return QStringLiteral("set_imaging_mode");
    case CmdId::Reboot: return QStringLiteral("reboot");
    default: return QStringLiteral("unknown");
    }
}

QString workModeName(quint32 modeCode) {
    switch (modeCode) {
    case 10: return QStringLiteral("boot");
    case 20: return QStringLiteral("imaging");
    default: return QStringLiteral("unknown");
    }
}

} // namespace laser_radar
