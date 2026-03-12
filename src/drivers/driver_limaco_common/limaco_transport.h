#pragma once

#include <QJsonObject>
#include <QVector>

#include "stdiolink/driver/iresponder.h"
#include "stdiolink/driver/meta_builder.h"

namespace limaco_driver {

struct LimacoConnectionOptions {
    QString transport = "serial";
    int unitId = 1;
    int timeoutMs = 2000;
    QString host = "127.0.0.1";
    int port = 502;
    QString portName;
    int baudRate = 9600;
    int dataBits = 8;
    QString stopBits = "1";
    QString parity = "even";
};

class LimacoModbusTransport {
public:
    bool readHoldingRegisters(const QJsonObject& params,
                              uint16_t address,
                              uint16_t count,
                              QVector<uint16_t>& registers,
                              stdiolink::IResponder& responder);

    bool writeMultipleRegisters(const QJsonObject& params,
                                uint16_t address,
                                const QVector<uint16_t>& values,
                                stdiolink::IResponder& responder);

private:
    bool parseOptions(const QJsonObject& params,
                      LimacoConnectionOptions& options,
                      stdiolink::IResponder& responder) const;
};

stdiolink::meta::FieldBuilder limacoConnectionParam(const QString& name);
void addLimacoConnectionParams(stdiolink::meta::CommandBuilder& command);

} // namespace limaco_driver
