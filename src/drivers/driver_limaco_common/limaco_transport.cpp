#include "driver_limaco_common/limaco_transport.h"

#include <QHash>

#include <memory>

#include "driver_modbusrtu/modbus_rtu_client.h"
#include "driver_modbusrtu_serial/modbus_rtu_serial_client.h"

using stdiolink::IResponder;
using stdiolink::meta::CommandBuilder;
using stdiolink::meta::FieldBuilder;
using stdiolink::meta::FieldType;

namespace limaco_driver {

namespace {

using modbus::ConnectionKey;
using modbus::ModbusResult;
using modbus::ModbusRtuClient;

struct TcpConnectionManager {
    static TcpConnectionManager& instance() {
        static TcpConnectionManager manager;
        return manager;
    }

    ModbusRtuClient* getClient(const QString& host, quint16 port) {
        const ConnectionKey key{host, port};
        auto it = m_connections.find(key);
        if (it != m_connections.end() && it.value()->isConnected()) {
            return it.value().get();
        }

        auto client = std::make_shared<ModbusRtuClient>();
        if (!client->connectToServer(host, port)) {
            return nullptr;
        }

        ModbusRtuClient* rawClient = client.get();
        m_connections.insert(key, client);
        return rawClient;
    }

private:
    QHash<ConnectionKey, std::shared_ptr<ModbusRtuClient>> m_connections;
};

struct SerialConnectionInfo {
    std::shared_ptr<ModbusRtuSerialClient> client;
    int baudRate = 9600;
    int dataBits = 8;
    QString stopBits = "1";
    QString parity = "even";
};

class SerialConnectionManager {
public:
    static SerialConnectionManager& instance() {
        static SerialConnectionManager manager;
        return manager;
    }

    ModbusRtuSerialClient* getClient(const QString& portName,
                                     int baudRate,
                                     int dataBits,
                                     const QString& stopBits,
                                     const QString& parity,
                                     QString& errorMessage) {
        auto it = m_connections.find(portName);
        if (it != m_connections.end()) {
            const SerialConnectionInfo& info = it.value();
            if (info.client->isOpen()) {
                if (info.baudRate != baudRate
                    || info.dataBits != dataBits
                    || info.stopBits != stopBits
                    || info.parity != parity) {
                    errorMessage = QString(
                        "Serial port %1 is already open with different parameters")
                        .arg(portName);
                    return nullptr;
                }
                return info.client.get();
            }
            m_connections.erase(it);
        }

        auto client = std::make_shared<ModbusRtuSerialClient>();
        if (!client->open(portName, baudRate, dataBits, stopBits, parity)) {
            errorMessage = QString("Failed to open serial port %1").arg(portName);
            return nullptr;
        }

        SerialConnectionInfo info;
        info.client = client;
        info.baudRate = baudRate;
        info.dataBits = dataBits;
        info.stopBits = stopBits;
        info.parity = parity;
        m_connections.insert(portName, info);
        return client.get();
    }

private:
    QHash<QString, SerialConnectionInfo> m_connections;
};

bool isOneOf(const QString& value, const QStringList& values) {
    return values.contains(value, Qt::CaseInsensitive);
}

void respondInvalidParam(IResponder& responder, const QString& message) {
    responder.error(3, QJsonObject{{"message", message}});
}

void respondIoError(IResponder& responder, int code, const QString& message) {
    responder.error(code, QJsonObject{{"message", message}});
}

} // namespace

bool LimacoModbusTransport::parseOptions(const QJsonObject& params,
                                         LimacoConnectionOptions& options,
                                         IResponder& responder) const {
    options.transport = params.value("transport").toString("serial").trimmed().toLower();
    if (options.transport.isEmpty()) {
        options.transport = "serial";
    }
    if (options.transport != "serial" && options.transport != "tcp") {
        respondInvalidParam(responder, "transport must be serial or tcp");
        return false;
    }

    options.unitId = params.value("unit_id").toInt(1);
    if (options.unitId < 1 || options.unitId > 247) {
        respondInvalidParam(responder, "unit_id must be 1-247");
        return false;
    }

    options.timeoutMs = params.value("timeout").toInt(2000);
    if (options.timeoutMs < 1 || options.timeoutMs > 30000) {
        respondInvalidParam(responder, "timeout must be 1-30000 ms");
        return false;
    }

    if (options.transport == "tcp") {
        options.host = params.value("host").toString("127.0.0.1").trimmed();
        if (options.host.isEmpty()) {
            options.host = "127.0.0.1";
        }

        options.port = params.value("port").toInt(502);
        if (options.port < 1 || options.port > 65535) {
            respondInvalidParam(responder, "port must be 1-65535");
            return false;
        }
        return true;
    }

    options.portName = params.value("port_name").toString().trimmed();
    if (options.portName.isEmpty()) {
        respondInvalidParam(responder, "port_name is required when transport=serial");
        return false;
    }

    options.baudRate = params.value("baud_rate").toInt(9600);
    if (options.baudRate <= 0) {
        respondInvalidParam(responder, "baud_rate must be positive");
        return false;
    }

    options.dataBits = params.value("data_bits").toInt(8);
    if (options.dataBits < 5 || options.dataBits > 8) {
        respondInvalidParam(responder, "data_bits must be 5-8");
        return false;
    }

    options.stopBits = params.value("stop_bits").toString("1").trimmed();
    if (!isOneOf(options.stopBits, {"1", "1.5", "2"})) {
        respondInvalidParam(responder, "stop_bits must be one of 1, 1.5, 2");
        return false;
    }

    options.parity = params.value("parity").toString("even").trimmed().toLower();
    if (!isOneOf(options.parity, {"none", "even", "odd"})) {
        respondInvalidParam(responder, "parity must be one of none, even, odd");
        return false;
    }

    return true;
}

bool LimacoModbusTransport::readHoldingRegisters(const QJsonObject& params,
                                                 uint16_t address,
                                                 uint16_t count,
                                                 QVector<uint16_t>& registers,
                                                 IResponder& responder) {
    LimacoConnectionOptions options;
    if (!parseOptions(params, options, responder)) {
        return false;
    }

    if (options.transport == "tcp") {
        ModbusRtuClient* client = TcpConnectionManager::instance()
            .getClient(options.host, static_cast<quint16>(options.port));
        if (!client) {
            respondIoError(
                responder,
                1,
                QString("Failed to connect to %1:%2").arg(options.host).arg(options.port));
            return false;
        }

        client->setTimeout(options.timeoutMs);
        client->setUnitId(static_cast<uint8_t>(options.unitId));
        const ModbusResult result = client->readHoldingRegisters(address, count);
        if (!result.success) {
            respondIoError(responder, 2, result.errorMessage);
            return false;
        }

        registers = result.registers;
        return true;
    }

    QString errorMessage;
    ModbusRtuSerialClient* client = SerialConnectionManager::instance().getClient(
        options.portName,
        options.baudRate,
        options.dataBits,
        options.stopBits,
        options.parity,
        errorMessage);
    if (!client) {
        respondIoError(responder, 1, errorMessage);
        return false;
    }

    const SerialModbusResult result = client->readHoldingRegisters(
        static_cast<quint8>(options.unitId),
        address,
        count,
        options.timeoutMs);
    if (!result.success) {
        respondIoError(responder, 2, result.errorMessage);
        return false;
    }

    registers = result.registers;
    return true;
}

bool LimacoModbusTransport::writeMultipleRegisters(const QJsonObject& params,
                                                   uint16_t address,
                                                   const QVector<uint16_t>& values,
                                                   IResponder& responder) {
    LimacoConnectionOptions options;
    if (!parseOptions(params, options, responder)) {
        return false;
    }

    if (options.transport == "tcp") {
        ModbusRtuClient* client = TcpConnectionManager::instance()
            .getClient(options.host, static_cast<quint16>(options.port));
        if (!client) {
            respondIoError(
                responder,
                1,
                QString("Failed to connect to %1:%2").arg(options.host).arg(options.port));
            return false;
        }

        client->setTimeout(options.timeoutMs);
        client->setUnitId(static_cast<uint8_t>(options.unitId));
        const ModbusResult result = client->writeMultipleRegisters(address, values);
        if (!result.success) {
            respondIoError(responder, 2, result.errorMessage);
            return false;
        }
        return true;
    }

    QString errorMessage;
    ModbusRtuSerialClient* client = SerialConnectionManager::instance().getClient(
        options.portName,
        options.baudRate,
        options.dataBits,
        options.stopBits,
        options.parity,
        errorMessage);
    if (!client) {
        respondIoError(responder, 1, errorMessage);
        return false;
    }

    const SerialModbusResult result = client->writeMultipleRegisters(
        static_cast<quint8>(options.unitId),
        address,
        values,
        options.timeoutMs);
    if (!result.success) {
        respondIoError(responder, 2, result.errorMessage);
        return false;
    }

    return true;
}

FieldBuilder limacoConnectionParam(const QString& name) {
    if (name == "transport") {
        return FieldBuilder("transport", FieldType::Enum)
            .defaultValue("serial")
            .enumValues(QStringList{"serial", "tcp"})
            .description("连接方式：serial=RS485 串口直连 / tcp=Modbus RTU Over TCP 网关");
    }
    if (name == "unit_id") {
        return FieldBuilder("unit_id", FieldType::Int)
            .defaultValue(1)
            .range(1, 247)
            .description("雷达 Modbus 从站地址（1-247）");
    }
    if (name == "timeout") {
        return FieldBuilder("timeout", FieldType::Int)
            .defaultValue(2000)
            .range(1, 30000)
            .unit("ms")
            .description("单次 Modbus 读写超时（毫秒），默认 2000");
    }
    if (name == "host") {
        return FieldBuilder("host", FieldType::String)
            .defaultValue("127.0.0.1")
            .description("transport=tcp 时的网关/转换器 IP 地址");
    }
    if (name == "port") {
        return FieldBuilder("port", FieldType::Int)
            .defaultValue(502)
            .range(1, 65535)
            .description("transport=tcp 时的端口号，默认 502");
    }
    if (name == "port_name") {
        return FieldBuilder("port_name", FieldType::String)
            .description("transport=serial 时必填，RS485 串口名称（如 COM3 或 /dev/ttyUSB0）");
    }
    if (name == "baud_rate") {
        return FieldBuilder("baud_rate", FieldType::Int)
            .defaultValue(9600)
            .enumValues(QStringList{"1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"})
            .description("transport=serial 时的波特率，需与雷达设置一致，默认 9600");
    }
    if (name == "data_bits") {
        return FieldBuilder("data_bits", FieldType::Int)
            .defaultValue(8)
            .enumValues(QStringList{"5", "6", "7", "8"})
            .description("transport=serial 时的数据位，默认 8");
    }
    if (name == "stop_bits") {
        return FieldBuilder("stop_bits", FieldType::Enum)
            .defaultValue("1")
            .enumValues(QStringList{"1", "1.5", "2"})
            .description("transport=serial 时的停止位，默认 1");
    }

    return FieldBuilder("parity", FieldType::Enum)
        .defaultValue("even")
        .enumValues(QStringList{"none", "even", "odd"})
        .description("transport=serial 时的校验位，雷达默认 even（偶校验）");
}

void addLimacoConnectionParams(CommandBuilder& command) {
    command
        .param(limacoConnectionParam("transport"))
        .param(limacoConnectionParam("unit_id"))
        .param(limacoConnectionParam("timeout"))
        .param(limacoConnectionParam("host"))
        .param(limacoConnectionParam("port"))
        .param(limacoConnectionParam("port_name"))
        .param(limacoConnectionParam("baud_rate"))
        .param(limacoConnectionParam("data_bits"))
        .param(limacoConnectionParam("stop_bits"))
        .param(limacoConnectionParam("parity"));
}

} // namespace limaco_driver
