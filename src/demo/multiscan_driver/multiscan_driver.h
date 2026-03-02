#pragma once

#include <stdiolink/driver/meta_command_handler.h>

class MultiscanDriver : public stdiolink::IMetaCommandHandler {
public:
    MultiscanDriver();

    const stdiolink::meta::DriverMeta& driverMeta() const override;
    void handle(const QString& cmd, const QJsonValue& data, stdiolink::IResponder& resp) override;

private:
    void buildMeta();
    void handleScanTargets(const QJsonValue& data, stdiolink::IResponder& resp);
    void handleConfigureChannels(const QJsonValue& data, stdiolink::IResponder& resp);

    stdiolink::meta::DriverMeta m_meta;
};
