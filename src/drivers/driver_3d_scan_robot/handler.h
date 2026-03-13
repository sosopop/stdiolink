#pragma once

#include "stdiolink/driver/meta_command_handler.h"

namespace scan_robot {
class IRadarTransport;
}

class ThreeDScanRobotHandler : public stdiolink::IMetaCommandHandler {
public:
    ThreeDScanRobotHandler();

    const stdiolink::meta::DriverMeta& driverMeta() const override { return m_meta; }
    void handle(const QString& cmd, const QJsonValue& data,
                stdiolink::IResponder& responder) override;

    // Allow injection of custom transport (for testing)
    void setTransportFactory(std::function<scan_robot::IRadarTransport*()> factory);

private:
    void buildMeta();

    stdiolink::meta::DriverMeta m_meta;
    std::function<scan_robot::IRadarTransport*()> m_transportFactory;
};
