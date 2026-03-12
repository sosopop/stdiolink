#include "driver_limaco_common/limaco_decode.h"

namespace limaco_driver {

namespace {

bool isV2Layout(const QVector<uint16_t>& rawRegisters) {
    if (rawRegisters.size() < 15) {
        return false;
    }

    return (rawRegisters[0] & 0xFFu) == 0x14u
        && (rawRegisters[3] & 0xFFu) == 0x14u
        && (rawRegisters[6] & 0xFFu) == 0x14u
        && (rawRegisters[9] & 0xFFu) == 0x14u
        && (rawRegisters[12] & 0xFFu) == 0x14u;
}

} // namespace

bool decodeLimaco1Distance(const QVector<uint16_t>& rawRegisters,
                           Limaco1DistanceData& decoded,
                           QString* errorMessage) {
    if (rawRegisters.size() != 5) {
        if (errorMessage) {
            *errorMessage = QString("Expected 5 holding registers, got %1")
                .arg(rawRegisters.size());
        }
        return false;
    }

    decoded.rawRegisters = rawRegisters;
    decoded.statusRegister = rawRegisters[3];
    decoded.distanceValid = decoded.statusRegister < 50;
    if (decoded.distanceValid) {
        decoded.distanceMeters =
            ((static_cast<double>(rawRegisters[0]) * 65535.0) + rawRegisters[1]) / 10000.0;
    } else {
        decoded.distanceMeters = 0.0;
    }
    return true;
}

bool decodeLimaco5Distance(const QVector<uint16_t>& rawRegisters,
                           Limaco5DistanceData& decoded,
                           QString* errorMessage) {
    if (rawRegisters.size() != 15) {
        if (errorMessage) {
            *errorMessage = QString("Expected 15 holding registers, got %1")
                .arg(rawRegisters.size());
        }
        return false;
    }

    decoded.rawRegisters = rawRegisters;
    decoded.points.clear();

    const bool v2Layout = isV2Layout(rawRegisters);
    const int offset = v2Layout ? 1 : 0;
    const int padding = v2Layout ? 3 : 2;
    decoded.format = v2Layout ? "v2" : "legacy";

    for (int i = 0; i < 5; ++i) {
        const int distanceIndex = offset + (padding * i);
        const int stateIndex = distanceIndex + 1;

        Limaco5PointData point;
        point.index = i + 1;
        point.distanceRaw = rawRegisters[distanceIndex];
        point.distanceMeters = static_cast<double>(point.distanceRaw) / 100.0;
        point.state = rawRegisters[stateIndex];
        point.valid = point.state == 0;
        decoded.points.append(point);
    }

    return true;
}

} // namespace limaco_driver
