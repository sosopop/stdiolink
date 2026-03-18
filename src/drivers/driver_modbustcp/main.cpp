#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <memory>

#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"
#include "modbus_client.h"
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

    ModbusClient* getClient(const QString& host, quint16 port, int timeout) {
        ConnectionKey key{host, port};

        auto it = m_connections.find(key);
        if (it != m_connections.end() && it.value()->isConnected()) {
            return it.value().get();
        }

        auto client = std::make_shared<ModbusClient>(timeout);
        if (client->connectToServer(host, port)) {
            ModbusClient* ptr = client.get();
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

    QHash<ConnectionKey, std::shared_ptr<ModbusClient>> m_connections;
};

/**
 * ModbusTCP Driver Handler
 */
class ModbusTcpHandler : public IMetaCommandHandler {
public:
    ModbusTcpHandler() { buildMeta(); }

    const DriverMeta& driverMeta() const override { return m_meta; }

    void handle(const QString& cmd, const QJsonValue& data, IResponder& resp) override;

private:
    void buildMeta();

    // 辅助函数
    ModbusClient* getClient(const QJsonObject& p, IResponder& resp);
    QJsonArray coilsToJson(const QVector<bool>& coils);
    QJsonArray registersToJson(const QVector<uint16_t>& regs,
                               const QString& dataType, const QString& byteOrder);

    DriverMeta m_meta;
};

// 获取客户端连接
ModbusClient* ModbusTcpHandler::getClient(const QJsonObject& p, IResponder& resp)
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
QJsonArray ModbusTcpHandler::coilsToJson(const QVector<bool>& coils)
{
    QJsonArray arr;
    for (bool v : coils) {
        arr.append(v);
    }
    return arr;
}

// 寄存器数组转 JSON（支持类型转换）
QJsonArray ModbusTcpHandler::registersToJson(const QVector<uint16_t>& regs,
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
            arr.append(static_cast<qint64>(conv.toInt64(regs, i)));
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
void ModbusTcpHandler::handle(const QString& cmd, const QJsonValue& data, IResponder& resp)
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
            .description("Modbus TCP 服务器地址，如 192.168.1.100")
            .placeholder("192.168.1.1");
    }
    if (name == "port") {
        return FieldBuilder("port", FieldType::Int)
            .defaultValue(502)
            .range(1, 65535)
            .description("Modbus TCP 端口，默认 502");
    }
    if (name == "unit_id") {
        return FieldBuilder("unit_id", FieldType::Int)
            .defaultValue(1)
            .range(1, 247)
            .description("Modbus 从站地址（1-247），默认 1");
    }
    return FieldBuilder("timeout", FieldType::Int)
        .defaultValue(3000)
        .range(100, 30000)
        .unit("ms")
        .description("单次读写超时（毫秒），默认 3000");
}

// 数据类型枚举
static QStringList dataTypeEnum() {
    return {"int16", "uint16", "int32", "uint32", "float32", "int64", "uint64", "float64"};
}

// 字节序枚举
static QStringList byteOrderEnum() {
    return {"big_endian", "little_endian", "big_endian_byte_swap", "little_endian_byte_swap"};
}

void ModbusTcpHandler::buildMeta()
{
    auto readHolding = CommandBuilder("read_holding_registers")
        .description("读取保持寄存器（功能码 0x03），支持多种数据类型解码")
        .param(connectionParam("host"))
        .param(connectionParam("port"))
        .param(connectionParam("unit_id"))
        .param(connectionParam("timeout"))
        .param(FieldBuilder("address", FieldType::Int)
            .required().range(0, 65535)
            .description("Modbus 起始地址（0-65535）"))
        .param(FieldBuilder("count", FieldType::Int)
            .defaultValue(1).range(1, 125)
            .description("连续读取的数量"))
        .param(FieldBuilder("data_type", FieldType::Enum)
            .defaultValue("uint16").enumValues(dataTypeEnum())
            .description("值解码/编码类型：int16/uint16(1寄存器)、int32/uint32/float32(2寄存器)、int64/uint64/float64(4寄存器)"))
        .param(FieldBuilder("byte_order", FieldType::Enum)
            .defaultValue("big_endian").enumValues(byteOrderEnum())
            .description("多寄存器字节序：big_endian(AB CD) / little_endian(DC BA) / big_endian_byte_swap(BA DC) / little_endian_byte_swap(CD AB)"));
    readHolding.example("读取地址 0 起的 10 个保持寄存器", QStringList{"stdio", "console"},
        QJsonObject{{"host", "127.0.0.1"}, {"port", 502}, {"unit_id", 1},
                    {"address", 0}, {"count", 10}});

    auto writeHolding = CommandBuilder("write_holding_register")
        .description("写单个保持寄存器（功能码 0x06），写入 16 位无符号整数")
        .param(connectionParam("host"))
        .param(connectionParam("port"))
        .param(connectionParam("unit_id"))
        .param(connectionParam("timeout"))
        .param(FieldBuilder("address", FieldType::Int)
            .required().range(0, 65535)
            .description("目标寄存器地址（0-65535）"))
        .param(FieldBuilder("value", FieldType::Int)
            .required().range(0, 65535)
            .description("写入的 16 位无符号整数值（0-65535）"));
    writeHolding.example("向地址 100 写入值 1000", QStringList{"stdio", "console"},
        QJsonObject{{"host", "127.0.0.1"}, {"port", 502}, {"unit_id", 1},
                    {"address", 100}, {"value", 1000}});

    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("modbus.tcp", "ModbusTCP Master", "1.0.0",
              "Modbus TCP 主站驱动，支持读写线圈、离散输入、保持寄存器和输入寄存器")
        .vendor("stdiolink")
        .command(CommandBuilder("status")
            .description("获取驱动存活状态，固定返回 ready")
            .example("查询驱动状态", QStringList{"stdio", "console"}, QJsonObject{}))
        .command(CommandBuilder("read_coils")
            .description("读取线圈状态（功能码 0x01），返回 bool 数组")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535)
                .description("Modbus 起始地址（0-65535）"))
            .param(FieldBuilder("count", FieldType::Int)
                .defaultValue(1).range(1, 2000)
                .description("连续读取的数量"))
            .example("读取地址 0 起的 8 个线圈", QStringList{"stdio", "console"}, QJsonObject{{"host", "127.0.0.1"}, {"port", 502}, {"unit_id", 1}, {"address", 0}, {"count", 8}}))
        .command(readHolding)
        .command(CommandBuilder("write_coil")
            .description("写单个线圈（功能码 0x05），true=ON / false=OFF")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535)
                .description("目标线圈地址（0-65535）"))
            .param(FieldBuilder("value", FieldType::Bool)
                .required()
                .description("线圈值：true=ON / false=OFF"))
            .example("打开地址 0 的线圈", QStringList{"stdio", "console"}, QJsonObject{{"host", "127.0.0.1"}, {"port", 502}, {"unit_id", 1}, {"address", 0}, {"value", true}}))
        .command(CommandBuilder("write_coils")
            .description("批量写多个线圈（功能码 0x0F）")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535)
                .description("Modbus 起始地址（0-65535）"))
            .param(FieldBuilder("values", FieldType::Array)
                .required()
                .description("线圈值 bool 数组，按顺序对应起始地址起的连续线圈"))
            .example("批量设置 4 个线圈", QStringList{"stdio", "console"}, QJsonObject{{"host", "127.0.0.1"}, {"port", 502}, {"unit_id", 1}, {"address", 0}, {"values", QJsonArray{true, false, true, false}}}))
        .command(CommandBuilder("read_discrete_inputs")
            .description("读取离散输入状态（功能码 0x02），返回 bool 数组")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535)
                .description("Modbus 起始地址（0-65535）"))
            .param(FieldBuilder("count", FieldType::Int)
                .defaultValue(1).range(1, 2000)
                .description("连续读取的数量"))
            .example("读取地址 0 起的 8 个离散输入", QStringList{"stdio", "console"}, QJsonObject{{"host", "127.0.0.1"}, {"port", 502}, {"unit_id", 1}, {"address", 0}, {"count", 8}}))
        .command(writeHolding)
        .command(CommandBuilder("write_holding_registers")
            .description("写多个保持寄存器（功能码 0x10），按 data_type 编码后写入")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535)
                .description("Modbus 起始地址（0-65535）"))
            .param(FieldBuilder("value", FieldType::Double)
                .required()
                .description("要写入的值，按 data_type 编码为对应的寄存器序列"))
            .param(FieldBuilder("data_type", FieldType::Enum)
                .defaultValue("uint16").enumValues(dataTypeEnum())
                .description("值解码/编码类型：int16/uint16(1寄存器)、int32/uint32/float32(2寄存器)、int64/uint64/float64(4寄存器)"))
            .param(FieldBuilder("byte_order", FieldType::Enum)
                .defaultValue("big_endian").enumValues(byteOrderEnum())
                .description("多寄存器字节序：big_endian(AB CD) / little_endian(DC BA) / big_endian_byte_swap(BA DC) / little_endian_byte_swap(CD AB)"))
            .example("以 float32 写入 3.14 到地址 100", QStringList{"stdio", "console"}, QJsonObject{{"host", "127.0.0.1"}, {"port", 502}, {"unit_id", 1}, {"address", 100}, {"value", 3.14}, {"data_type", "float32"}}))
        .command(CommandBuilder("write_holding_registers_raw")
            .description("写多个保持寄存器（功能码 0x10），直接写入 uint16 原始值数组")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535)
                .description("Modbus 起始地址（0-65535）"))
            .param(FieldBuilder("values", FieldType::Array)
                .required()
                .description("uint16 原始寄存器值数组，按顺序写入起始地址起的连续寄存器"))
            .example("向地址 0 写入 3 个原始寄存器值", QStringList{"stdio", "console"}, QJsonObject{{"host", "127.0.0.1"}, {"port", 502}, {"unit_id", 1}, {"address", 0}, {"values", QJsonArray{100, 200, 300}}}))
        .command(CommandBuilder("read_input_registers")
            .description("读取输入寄存器（功能码 0x04），支持多种数据类型解码")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535)
                .description("Modbus 起始地址（0-65535）"))
            .param(FieldBuilder("count", FieldType::Int)
                .defaultValue(1).range(1, 125)
                .description("连续读取的数量"))
            .param(FieldBuilder("data_type", FieldType::Enum)
                .defaultValue("uint16").enumValues(dataTypeEnum())
                .description("值解码/编码类型：int16/uint16(1寄存器)、int32/uint32/float32(2寄存器)、int64/uint64/float64(4寄存器)"))
            .param(FieldBuilder("byte_order", FieldType::Enum)
                .defaultValue("big_endian").enumValues(byteOrderEnum())
                .description("多寄存器字节序：big_endian(AB CD) / little_endian(DC BA) / big_endian_byte_swap(BA DC) / little_endian_byte_swap(CD AB)"))
            .example("读取地址 0 起的 10 个输入寄存器", QStringList{"stdio", "console"}, QJsonObject{{"host", "127.0.0.1"}, {"port", 502}, {"unit_id", 1}, {"address", 0}, {"count", 10}}))
        .build();
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    ModbusTcpHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
