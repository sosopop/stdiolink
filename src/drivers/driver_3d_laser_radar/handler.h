#pragma once

#include <functional>

#include "stdiolink/driver/meta_command_handler.h"

namespace laser_radar {
class ILaserTransport;
}

class ThreeDLaserRadarHandler : public stdiolink::IMetaCommandHandler {
public:
    ThreeDLaserRadarHandler();

    const stdiolink::meta::DriverMeta& driverMeta() const override { return m_meta; }
    bool autoValidateParams() const override { return false; }
    void handle(const QString& cmd, const QJsonValue& data,
                stdiolink::IResponder& responder) override;

    void setTransportFactory(std::function<laser_radar::ILaserTransport*()> factory);

private:
    void buildMeta();

    stdiolink::meta::DriverMeta m_meta;
    std::function<laser_radar::ILaserTransport*()> m_transportFactory;
};
