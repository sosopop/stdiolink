#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <memory>

#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"
#include "modbus_rtu_client.h"
#include "modbus_types.h"

using namespace stdiolink;
using namespace stdiolink::meta;
using namespace modbus;

/**
 * 连接管理器 - 自动缓存连接
 */
class ConnectionManager {
public:
    static ConnectionManager& instance() {
        static ConnectionManager mgr;
        return mgr;
    }

    ModbusRtuClient* getClient(const QString& host, quint16 port, int timeout) {
        ConnectionKey key{host, port};

        auto it = m_connections.find(key);
        if (it != m_connections.end() && it.value()->isConnected()) {
            return it.value().get();
        }

        auto client = std::make_shared<ModbusRtuClient>(timeout);
        if (client->connectToServer(host, port)) {
            ModbusRtuClient* ptr = client.get();
            m_connections[key] = std::move(client);
            return ptr;
        }
        return nullptr;
    }

    void disconnectAll() {
        m_connections.clear();
    }

private:
    ConnectionManager() = default;
    ~ConnectionManager() { disconnectAll(); }

    QHash<ConnectionKey, std::shared_ptr<ModbusRtuClient>> m_connections;
};

/**
 * Modbus RTU Over TCP Driver Handler
 */
class ModbusRtuHandler : public IMetaCommandHandler {
public:
    ModbusRtuHandler() { buildMeta(); }

    const DriverMeta& driverMeta() const override { return m_meta; }

    void handle(const QString& cmd, const QJsonValue& data, IResponder& resp) override;

private:
    void buildMeta();

    // 辅助函数
    ModbusRtuClient* getClient(const QJsonObject& p, IResponder& resp);
    QJsonArray coilsToJson(const QVector<bool>& coils);
    QJsonArray registersToJson(const QVector<uint16_t>& regs,
                               const QString& dataType, const QString& byteOrder);

    DriverMeta m_meta;
};

// 获取客户端连接
ModbusRtuClient* ModbusRtuHandler::getClient(const QJsonObject& p, IResponder& resp)
{
    QString host = p["host"].toString();
    int port = p["port"].toInt(502);
    int timeout = p["timeout"].toInt(3000);

    auto* client = ConnectionManager::instance().getClient(host, port, timeout);
    if (!client) {
        resp.error(1, QJsonObject{{"message", "Failed to connect to " + host}});
    }
    return client;
}

// 线圈数组转 JSON
QJsonArray ModbusRtuHandler::coilsToJson(const QVector<bool>& coils)
{
    QJsonArray arr;
    for (bool v : coils) {
        arr.append(v);
    }
    return arr;
}

// 寄存器数组转 JSON（支持类型转换）
QJsonArray ModbusRtuHandler::registersToJson(const QVector<uint16_t>& regs,
                                              const QString& dataType,
                                              const QString& byteOrder)
{
    QJsonArray arr;
    ByteOrderConverter conv(parseByteOrder(byteOrder));
    DataType dt = parseDataType(dataType);
    int step = registersPerType(dt);

    for (int i = 0; i + step <= regs.size(); i += step) {
        switch (dt) {
        case DataType::Int16:
            arr.append(conv.toInt16(regs, i));
            break;
        case DataType::UInt16:
            arr.append(conv.toUInt16(regs, i));
            break;
        case DataType::Int32:
            arr.append(conv.toInt32(regs, i));
            break;
        case DataType::UInt32:
            arr.append(static_cast<qint64>(conv.toUInt32(regs, i)));
            break;
        case DataType::Float32:
            arr.append(conv.toFloat32(regs, i));
            break;
        case DataType::Int64:
            arr.append(conv.toInt64(regs, i));
            break;
        case DataType::UInt64:
            arr.append(static_cast<qint64>(conv.toUInt64(regs, i)));
            break;
        case DataType::Float64:
            arr.append(conv.toFloat64(regs, i));
            break;
        }
    }
    return arr;
}

// 命令处理
void ModbusRtuHandler::handle(const QString& cmd, const QJsonValue& data, IResponder& resp)
{
    QJsonObject p = data.toObject();

    if (cmd == "status") {
        resp.done(0, QJsonObject{{"status", "ready"}});
        return;
    }

    // 获取连接
    auto* client = getClient(p, resp);
    if (!client) return;

    int unitId = p["unit_id"].toInt(1);
    client->setUnitId(unitId);

    if (cmd == "read_coils") {
        int addr = p["address"].toInt();
        int count = p["count"].toInt(1);
        auto result = client->readCoils(addr, count);
        if (result.success) {
            resp.done(0, QJsonObject{{"values", coilsToJson(result.coils)}});
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else if (cmd == "write_coils") {
        int addr = p["address"].toInt();
        QJsonArray arr = p["values"].toArray();
        QVector<bool> values;
        for (const auto& v : arr) {
            values.append(v.toBool());
        }
        auto result = client->writeMultipleCoils(addr, values);
        if (result.success) {
            resp.done(0, QJsonObject{{"written", values.size()}});
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else if (cmd == "read_discrete_inputs") {
        int addr = p["address"].toInt();
        int count = p["count"].toInt(1);
        auto result = client->readDiscreteInputs(addr, count);
        if (result.success) {
            resp.done(0, QJsonObject{{"values", coilsToJson(result.coils)}});
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else if (cmd == "write_coil") {
        int addr = p["address"].toInt();
        bool value = p["value"].toBool();
        auto result = client->writeSingleCoil(addr, value);
        if (result.success) {
            resp.done(0, QJsonObject{{"written", true}});
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else if (cmd == "read_holding_registers") {
        int addr = p["address"].toInt();
        int count = p["count"].toInt(1);
        QString dataType = p["data_type"].toString("uint16");
        QString byteOrder = p["byte_order"].toString("big_endian");
        auto result = client->readHoldingRegisters(addr, count);
        if (result.success) {
            resp.done(0, QJsonObject{
                {"values", registersToJson(result.registers, dataType, byteOrder)},
                {"raw", [&]() {
                    QJsonArray arr;
                    for (auto v : result.registers) arr.append(v);
                    return arr;
                }()}
            });
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else if (cmd == "write_holding_register") {
        int addr = p["address"].toInt();
        int value = p["value"].toInt();
        auto result = client->writeSingleRegister(addr, value);
        if (result.success) {
            resp.done(0, QJsonObject{{"written", true}});
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else if (cmd == "write_holding_registers") {
        int addr = p["address"].toInt();
        double value = p["value"].toDouble();
        QString dataType = p["data_type"].toString("uint16");
        QString byteOrder = p["byte_order"].toString("big_endian");

        ByteOrderConverter conv(parseByteOrder(byteOrder));
        QVector<uint16_t> regs;

        DataType dt = parseDataType(dataType);
        switch (dt) {
        case DataType::Int16:
            regs = conv.fromInt16(static_cast<int16_t>(value));
            break;
        case DataType::UInt16:
            regs = conv.fromUInt16(static_cast<uint16_t>(value));
            break;
        case DataType::Int32:
            regs = conv.fromInt32(static_cast<int32_t>(value));
            break;
        case DataType::UInt32:
            regs = conv.fromUInt32(static_cast<uint32_t>(value));
            break;
        case DataType::Float32:
            regs = conv.fromFloat32(static_cast<float>(value));
            break;
        case DataType::Int64:
            regs = conv.fromInt64(static_cast<int64_t>(value));
            break;
        case DataType::UInt64:
            regs = conv.fromUInt64(static_cast<uint64_t>(value));
            break;
        case DataType::Float64:
            regs = conv.fromFloat64(value);
            break;
        }

        auto result = client->writeMultipleRegisters(addr, regs);
        if (result.success) {
            resp.done(0, QJsonObject{{"written", regs.size()}});
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else if (cmd == "write_holding_registers_raw") {
        int addr = p["address"].toInt();
        QJsonArray arr = p["values"].toArray();
        QVector<uint16_t> values;
        for (const auto& v : arr) {
            values.append(v.toInt());
        }
        auto result = client->writeMultipleRegisters(addr, values);
        if (result.success) {
            resp.done(0, QJsonObject{{"written", values.size()}});
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else if (cmd == "read_input_registers") {
        int addr = p["address"].toInt();
        int count = p["count"].toInt(1);
        QString dataType = p["data_type"].toString("uint16");
        QString byteOrder = p["byte_order"].toString("big_endian");
        auto result = client->readInputRegisters(addr, count);
        if (result.success) {
            resp.done(0, QJsonObject{
                {"values", registersToJson(result.registers, dataType, byteOrder)},
                {"raw", [&]() {
                    QJsonArray arr;
                    for (auto v : result.registers) arr.append(v);
                    return arr;
                }()}
            });
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else {
        resp.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
    }
}

// 构建元数据 - 连接参数构建器
static FieldBuilder connectionParam(const QString& name) {
    if (name == "host") {
        return FieldBuilder("host", FieldType::String)
            .required()
            .description("Modbus RTU Over TCP 服务器地址")
            .placeholder("192.168.1.1");
    }
    if (name == "port") {
        return FieldBuilder("port", FieldType::Int)
            .defaultValue(502)
            .range(1, 65535)
            .description("Modbus RTU Over TCP 端口");
    }
    if (name == "unit_id") {
        return FieldBuilder("unit_id", FieldType::Int)
            .defaultValue(1)
            .range(1, 247)
            .description("从站地址");
    }
    return FieldBuilder("timeout", FieldType::Int)
        .defaultValue(3000)
        .range(100, 30000)
        .unit("ms")
        .description("超时时间");
}

// 数据类型枚举
static QStringList dataTypeEnum() {
    return {"int16", "uint16", "int32", "uint32", "float32", "int64", "uint64", "float64"};
}

// 字节序枚举
static QStringList byteOrderEnum() {
    return {"big_endian", "little_endian", "big_endian_byte_swap", "little_endian_byte_swap"};
}

void ModbusRtuHandler::buildMeta()
{
    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("modbus.rtu", "ModbusRTU Over TCP Master", "1.0.0",
              "Modbus RTU Over TCP 主机驱动，使用 RTU 帧格式（带 CRC16）通过 TCP 通信")
        .vendor("stdiolink")
        .command(CommandBuilder("status")
            .description("获取驱动状态"))
        .command(CommandBuilder("read_coils")
            .description("读取线圈 (FC 0x01)")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required()
                .range(0, 65535)
                .description("起始地址"))
            .param(FieldBuilder("count", FieldType::Int)
                .defaultValue(1)
                .range(1, 2000)
                .description("读取数量")))
        .command(CommandBuilder("read_holding_registers")
            .description("读取保持寄存器 (FC 0x03)")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required()
                .range(0, 65535)
                .description("起始地址"))
            .param(FieldBuilder("count", FieldType::Int)
                .defaultValue(1)
                .range(1, 125)
                .description("读取数量"))
            .param(FieldBuilder("data_type", FieldType::Enum)
                .defaultValue("uint16")
                .enumValues(dataTypeEnum())
                .description("数据类型"))
            .param(FieldBuilder("byte_order", FieldType::Enum)
                .defaultValue("big_endian")
                .enumValues(byteOrderEnum())
                .description("字节序")))
        .command(CommandBuilder("write_coil")
            .description("写单个线圈 (FC 0x05)")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required()
                .range(0, 65535)
                .description("线圈地址"))
            .param(FieldBuilder("value", FieldType::Bool)
                .required()
                .description("线圈值")))
        .command(CommandBuilder("write_coils")
            .description("写多个线圈 (FC 0x0F)")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required()
                .range(0, 65535)
                .description("起始地址"))
            .param(FieldBuilder("values", FieldType::Array)
                .required()
                .description("线圈值数组")))
        .command(CommandBuilder("read_discrete_inputs")
            .description("读取离散输入 (FC 0x02)")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required()
                .range(0, 65535)
                .description("起始地址"))
            .param(FieldBuilder("count", FieldType::Int)
                .defaultValue(1)
                .range(1, 2000)
                .description("读取数量")))
        .command(CommandBuilder("write_holding_register")
            .description("写单个保持寄存器 (FC 0x06)")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required()
                .range(0, 65535)
                .description("寄存器地址"))
            .param(FieldBuilder("value", FieldType::Int)
                .required()
                .range(0, 65535)
                .description("寄存器值")))
        .command(CommandBuilder("write_holding_registers")
            .description("写多个保持寄存器 (FC 0x10，带类型转换)")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required()
                .range(0, 65535)
                .description("起始地址"))
            .param(FieldBuilder("value", FieldType::Double)
                .required()
                .description("要写入的值"))
            .param(FieldBuilder("data_type", FieldType::Enum)
                .defaultValue("uint16")
                .enumValues(dataTypeEnum())
                .description("数据类型"))
            .param(FieldBuilder("byte_order", FieldType::Enum)
                .defaultValue("big_endian")
                .enumValues(byteOrderEnum())
                .description("字节序")))
        .command(CommandBuilder("write_holding_registers_raw")
            .description("写多个保持寄存器 (FC 0x10，原始值)")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required()
                .range(0, 65535)
                .description("起始地址"))
            .param(FieldBuilder("values", FieldType::Array)
                .required()
                .description("寄存器值数组")))
        .command(CommandBuilder("read_input_registers")
            .description("读取输入寄存器 (FC 0x04)")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required()
                .range(0, 65535)
                .description("起始地址"))
            .param(FieldBuilder("count", FieldType::Int)
                .defaultValue(1)
                .range(1, 125)
                .description("读取数量"))
            .param(FieldBuilder("data_type", FieldType::Enum)
                .defaultValue("uint16")
                .enumValues(dataTypeEnum())
                .description("数据类型"))
            .param(FieldBuilder("byte_order", FieldType::Enum)
                .defaultValue("big_endian")
                .enumValues(byteOrderEnum())
                .description("字节序")))
        .build();
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    ModbusRtuHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}