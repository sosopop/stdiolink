#pragma once

#include <functional>

#include "stdiolink/driver/meta_command_handler.h"

namespace temp_scanner {
class IThermalTransport;
}

class ThreeDTempScannerHandler : public stdiolink::IMetaCommandHandler {
public:
    ThreeDTempScannerHandler();

    const stdiolink::meta::DriverMeta& driverMeta() const override { return m_meta; }
    bool autoValidateParams() const override { return false; }
    void handle(const QString& cmd, const QJsonValue& data, stdiolink::IResponder& responder) override;

    void setTransportFactory(std::function<temp_scanner::IThermalTransport*()> factory);

private:
    void buildMeta();

    stdiolink::meta::DriverMeta m_meta;
    std::function<temp_scanner::IThermalTransport*()> m_transportFactory;
};
