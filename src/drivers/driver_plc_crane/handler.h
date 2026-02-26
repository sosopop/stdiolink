#pragma once

#include "modbus_client.h"
#include "stdiolink/driver/meta_command_handler.h"
#include <QHash>
#include <memory>

using namespace stdiolink;
using namespace stdiolink::meta;

class PlcCraneHandler : public IMetaCommandHandler {
public:
    PlcCraneHandler() { buildMeta(); }
    const DriverMeta& driverMeta() const override { return m_meta; }
    void handle(const QString& cmd, const QJsonValue& data,
                IResponder& resp) override;

private:
    void buildMeta();
    modbus::ModbusClient* getClient(const QString& host, int port,
                                     int timeout, IResponder& resp);

    DriverMeta m_meta;
    QHash<modbus::ConnectionKey, std::shared_ptr<modbus::ModbusClient>> m_connections;
};
