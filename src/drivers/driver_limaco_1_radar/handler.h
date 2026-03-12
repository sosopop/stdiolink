#pragma once

#include "driver_limaco_common/limaco_transport.h"
#include "stdiolink/driver/meta_command_handler.h"

class Limaco1RadarHandler : public stdiolink::IMetaCommandHandler {
public:
    Limaco1RadarHandler();

    const stdiolink::meta::DriverMeta& driverMeta() const override { return m_meta; }
    void handle(const QString& cmd, const QJsonValue& data, stdiolink::IResponder& responder) override;

private:
    void buildMeta();

    stdiolink::meta::DriverMeta m_meta;
    limaco_driver::LimacoModbusTransport m_transport;
};
