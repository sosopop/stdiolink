#pragma once

#include <QString>
#include <QVector>

#include <cstdint>

namespace limaco_driver {

struct Limaco1DistanceData {
    QVector<uint16_t> rawRegisters;
    double distanceMeters = 0.0;
    uint16_t statusRegister = 0;
    bool distanceValid = false;
};

struct Limaco5PointData {
    int index = 0;
    uint16_t distanceRaw = 0;
    double distanceMeters = 0.0;
    uint16_t state = 0;
    bool valid = false;
};

struct Limaco5DistanceData {
    QString format;
    QVector<uint16_t> rawRegisters;
    QVector<Limaco5PointData> points;
};

bool decodeLimaco1Distance(const QVector<uint16_t>& rawRegisters,
                           Limaco1DistanceData& decoded,
                           QString* errorMessage = nullptr);

bool decodeLimaco5Distance(const QVector<uint16_t>& rawRegisters,
                           Limaco5DistanceData& decoded,
                           QString* errorMessage = nullptr);

} // namespace limaco_driver
