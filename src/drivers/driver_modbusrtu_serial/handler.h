#pragma once

#include "modbus_rtu_serial_client.h"
#include "stdiolink/driver/meta_command_handler.h"
#include <QMap>
#include <memory>

using namespace stdiolink;
using namespace stdiolink::meta;

class SerialConnectionManager {
public:
    static SerialConnectionManager& instance() {
        static SerialConnectionManager mgr;
        return mgr;
    }

    ModbusRtuSerialClient* getConnection(
        const QString& portName, int baudRate, int dataBits,
        const QString& stopBits, const QString& parity,
        QString& errorMsg);

private:
    SerialConnectionManager() = default;
    ~SerialConnectionManager() { m_connections.clear(); }

    struct ConnectionInfo {
        std::shared_ptr<ModbusRtuSerialClient> client;
        int baudRate;
        int dataBits;
        QString stopBits;
        QString parity;
    };
    QMap<QString, ConnectionInfo> m_connections;
};

class ModbusRtuSerialHandler : public IMetaCommandHandler {
public:
    ModbusRtuSerialHandler() { buildMeta(); }
    const DriverMeta& driverMeta() const override { return m_meta; }
    void handle(const QString& cmd, const QJsonValue& data,
                IResponder& resp) override;

private:
    void buildMeta();
    ModbusRtuSerialClient* getClient(const QJsonObject& p, IResponder& resp);
    QJsonArray coilsToJson(const QVector<bool>& coils);
    QJsonArray registersToJson(const QVector<uint16_t>& regs,
                               const QString& dataType, const QString& byteOrder);

    DriverMeta m_meta;
};
