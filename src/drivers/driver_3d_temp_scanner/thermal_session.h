#pragma once

#include <QVector>

#include "driver_3d_temp_scanner/protocol_codec.h"
#include "driver_3d_temp_scanner/thermal_transport.h"

namespace temp_scanner {

struct CaptureResult {
    int width = kImageWidth;
    int height = kImageHeight;
    QVector<quint16> rawTemperatures;
    QVector<double> temperaturesDegC;
    double minTempDegC = 0.0;
    double maxTempDegC = 0.0;
};

class ThermalSession {
public:
    explicit ThermalSession(IThermalTransport* transport);
    ~ThermalSession();

    bool open(const ThermalTransportParams& params, QString* errorMessage);
    void close();

    bool capture(CaptureResult* result, QString* errorMessage);

private:
    bool writeSingleRegister(quint16 registerAddress, quint16 value, QString* errorMessage);
    bool readRegisters(quint16 startRegister,
                       quint16 registerCount,
                       QVector<quint16>* registers,
                       QString* errorMessage);
    bool transact(const QByteArray& request,
                  quint8 expectedFunctionCode,
                  int expectedNormalResponseSize,
                  QByteArray* responseFrame,
                  QString* errorMessage);

    IThermalTransport* m_transport = nullptr;
    ThermalTransportParams m_params;
};

} // namespace temp_scanner
