#pragma once

#include <QJsonObject>
#include <QJsonValue>

#include "stdiolink/driver/meta_command_handler.h"

struct PqwAnalogOutputConnectionOptions {
    QString portName;
    int baudRate = 9600;
    QString parity = "none";
    QString stopBits = "1";
    int unitId = 1;
    int timeoutMs = 2000;
};

struct PqwAnalogOutputConfigWrite {
    QString field;
    quint16 reg = 0;
    quint16 rawValue = 0;
    QJsonValue value;
};

class PqwAnalogOutputHandler : public stdiolink::IMetaCommandHandler {
public:
    PqwAnalogOutputHandler();

    const stdiolink::meta::DriverMeta& driverMeta() const override { return m_meta; }
    void handle(const QString& cmd, const QJsonValue& data,
                stdiolink::IResponder& responder) override;

    static bool engineeringValueToRaw(double value,
                                      quint16& rawValue,
                                      QString* errorMessage = nullptr);
    static bool commWatchdogMsToRaw(int watchdogMs,
                                    quint16& rawValue,
                                    QString* errorMessage = nullptr);
    static int commWatchdogRawToMs(quint16 rawValue);
    static bool resolveConnectionOptions(const QString& cmd,
                                         const QJsonObject& params,
                                         PqwAnalogOutputConnectionOptions& options,
                                         QString* errorMessage = nullptr);
    static bool buildCommConfigWrites(const QJsonObject& params,
                                      QVector<PqwAnalogOutputConfigWrite>& writes,
                                      QString* errorMessage = nullptr);

private:
    void buildMeta();

    stdiolink::meta::DriverMeta m_meta;
};
