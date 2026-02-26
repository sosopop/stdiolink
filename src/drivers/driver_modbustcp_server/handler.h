#pragma once

#include "modbus_tcp_server.h"
#include "stdiolink/driver/meta_command_handler.h"
#include "stdiolink/driver/stdio_responder.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class ModbusTcpServerHandler : public IMetaCommandHandler {
public:
    ModbusTcpServerHandler();
    const DriverMeta& driverMeta() const override { return m_meta; }
    void handle(const QString& cmd, const QJsonValue& data,
                IResponder& resp) override;

private:
    void buildMeta();
    void connectEvents();

    DriverMeta m_meta;
    ModbusTcpServer m_server;
    StdioResponder m_eventResponder;
};
