#include "handler.h"

#include <QJsonArray>
#include <QJsonObject>

#include "stdiolink/driver/meta_builder.h"
#include "modbus_types.h"

using namespace modbus;

static QStringList dataTypeEnum() {
    return {"int16", "uint16", "int32", "uint32", "float32", "int64", "uint64", "float64"};
}

static QStringList byteOrderEnum() {
    return {"big_endian", "little_endian", "big_endian_byte_swap", "little_endian_byte_swap"};
}

static FieldBuilder serialParam(const QString& name) {
    if (name == "port_name") {
        return FieldBuilder("port_name", FieldType::String)
            .required()
            .description("RS485 串口名称，如 COM3 或 /dev/ttyUSB0")
            .placeholder("COM1");
    }
    if (name == "baud_rate") {
        return FieldBuilder("baud_rate", FieldType::Int)
            .defaultValue(9600)
            .enumValues(QStringList{"1200","2400","4800","9600","19200","38400","57600","115200"})
            .description("通信波特率，需与从站设置一致，默认 9600");
    }
    if (name == "data_bits") {
        return FieldBuilder("data_bits", FieldType::Int)
            .defaultValue(8)
            .enumValues(QStringList{"5","6","7","8"})
            .description("数据位长度，默认 8");
    }
    if (name == "stop_bits") {
        return FieldBuilder("stop_bits", FieldType::Enum)
            .defaultValue("1")
            .enumValues(QStringList{"1","1.5","2"})
            .description("停止位，默认 1");
    }
    if (name == "parity") {
        return FieldBuilder("parity", FieldType::Enum)
            .defaultValue("none")
            .enumValues(QStringList{"none","even","odd"})
            .description("校验方式：none=无 / even=偶 / odd=奇，默认 none");
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

ModbusRtuSerialClient* SerialConnectionManager::getConnection(
        const QString& portName, int baudRate, int dataBits,
        const QString& stopBits, const QString& parity,
        QString& errorMsg) {
    auto it = m_connections.find(portName);
    if (it != m_connections.end()) {
        auto& info = it.value();
        if (info.client->isOpen()) {
            if (info.baudRate != baudRate || info.dataBits != dataBits ||
                info.stopBits != stopBits || info.parity != parity) {
                errorMsg = QString("Port %1 already open with different parameters").arg(portName);
                return nullptr;
            }
            return info.client.get();
        }
        m_connections.erase(it);
    }

    auto client = std::make_shared<ModbusRtuSerialClient>();
    if (!client->open(portName, baudRate, dataBits, stopBits, parity)) {
        errorMsg = QString("Failed to open serial port %1").arg(portName);
        return nullptr;
    }

    ConnectionInfo ci;
    ci.client = client;
    ci.baudRate = baudRate;
    ci.dataBits = dataBits;
    ci.stopBits = stopBits;
    ci.parity = parity;
    m_connections[portName] = ci;
    return client.get();
}

ModbusRtuSerialClient* ModbusRtuSerialHandler::getClient(const QJsonObject& p, IResponder& resp) {
    QString portName = p["port_name"].toString();
    int baudRate = p["baud_rate"].toInt(9600);
    int dataBits = p["data_bits"].toInt(8);
    QString stopBits = p["stop_bits"].toString("1");
    QString parity = p["parity"].toString("none");

    QString errorMsg;
    auto* client = SerialConnectionManager::instance()
        .getConnection(portName, baudRate, dataBits, stopBits, parity, errorMsg);
    if (!client) {
        resp.error(1, QJsonObject{{"message", errorMsg}});
    }
    return client;
}

QJsonArray ModbusRtuSerialHandler::coilsToJson(const QVector<bool>& coils) {
    QJsonArray arr;
    for (bool v : coils) arr.append(v);
    return arr;
}

QJsonArray ModbusRtuSerialHandler::registersToJson(const QVector<uint16_t>& regs,
        const QString& dataType, const QString& byteOrder) {
    QJsonArray arr;
    ByteOrderConverter conv(parseByteOrder(byteOrder));
    DataType dt = parseDataType(dataType);
    int step = registersPerType(dt);

    for (int i = 0; i + step <= regs.size(); i += step) {
        switch (dt) {
        case DataType::Int16:   arr.append(conv.toInt16(regs, i)); break;
        case DataType::UInt16:  arr.append(conv.toUInt16(regs, i)); break;
        case DataType::Int32:   arr.append(conv.toInt32(regs, i)); break;
        case DataType::UInt32:  arr.append(static_cast<qint64>(conv.toUInt32(regs, i))); break;
        case DataType::Float32: arr.append(conv.toFloat32(regs, i)); break;
        case DataType::Int64:   arr.append(static_cast<qint64>(conv.toInt64(regs, i))); break;
        case DataType::UInt64:  arr.append(QString::number(conv.toUInt64(regs, i))); break;
        case DataType::Float64: arr.append(conv.toFloat64(regs, i)); break;
        }
    }
    return arr;
}

void ModbusRtuSerialHandler::handle(const QString& cmd, const QJsonValue& data,
        IResponder& resp) {
    QJsonObject p = data.toObject();

    if (cmd == "status") {
        resp.done(0, QJsonObject{{"status", "ready"}});
        return;
    }

    int unitId = p["unit_id"].toInt(1);
    if (unitId < 1 || unitId > 247) {
        resp.error(3, QJsonObject{{"message", "unit_id must be 1-247"}});
        return;
    }

    int timeout = p["timeout"].toInt(3000);

    // Count/type mismatch validation for register reads
    if (cmd == "read_holding_registers" || cmd == "read_input_registers") {
        QString dataType = p["data_type"].toString("uint16");
        int count = p["count"].toInt(1);
        int step = registersPerType(parseDataType(dataType));
        if (step > 1 && count % step != 0) {
            resp.error(3, QJsonObject{{"message",
                QString("count %1 is not a multiple of %2 registers required by %3")
                    .arg(count).arg(step).arg(dataType)}});
            return;
        }
    }

    auto* client = getClient(p, resp);
    if (!client) return;

    if (cmd == "read_coils") {
        int addr = p["address"].toInt();
        int count = p["count"].toInt(1);
        auto result = client->readCoils(unitId, addr, count, timeout);
        if (result.success) {
            resp.done(0, QJsonObject{{"values", coilsToJson(result.coils)}});
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else if (cmd == "read_discrete_inputs") {
        int addr = p["address"].toInt();
        int count = p["count"].toInt(1);
        auto result = client->readDiscreteInputs(unitId, addr, count, timeout);
        if (result.success) {
            resp.done(0, QJsonObject{{"values", coilsToJson(result.coils)}});
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else if (cmd == "read_holding_registers") {
        int addr = p["address"].toInt();
        int count = p["count"].toInt(1);
        QString dataType = p["data_type"].toString("uint16");
        QString byteOrder = p["byte_order"].toString("big_endian");
        auto result = client->readHoldingRegisters(unitId, addr, count, timeout);
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
    else if (cmd == "read_input_registers") {
        int addr = p["address"].toInt();
        int count = p["count"].toInt(1);
        QString dataType = p["data_type"].toString("uint16");
        QString byteOrder = p["byte_order"].toString("big_endian");
        auto result = client->readInputRegisters(unitId, addr, count, timeout);
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
    else if (cmd == "write_coil") {
        int addr = p["address"].toInt();
        bool value = p["value"].toBool();
        auto result = client->writeSingleCoil(unitId, addr, value, timeout);
        if (result.success) {
            resp.done(0, QJsonObject{{"written", true}});
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else if (cmd == "write_coils") {
        int addr = p["address"].toInt();
        QJsonArray arr = p["values"].toArray();
        QVector<bool> values;
        for (const auto& v : arr) values.append(v.toBool());
        auto result = client->writeMultipleCoils(unitId, addr, values, timeout);
        if (result.success) {
            resp.done(0, QJsonObject{{"written", values.size()}});
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else if (cmd == "write_holding_register") {
        int addr = p["address"].toInt();
        int value = p["value"].toInt();
        auto result = client->writeSingleRegister(unitId, addr, value, timeout);
        if (result.success) {
            resp.done(0, QJsonObject{{"written", true}});
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else if (cmd == "write_holding_registers") {
        int addr = p["address"].toInt();
        QJsonValue valueJson = p["value"];
        QString dataType = p["data_type"].toString("uint16");
        QString byteOrder = p["byte_order"].toString("big_endian");

        ByteOrderConverter conv(parseByteOrder(byteOrder));
        QVector<uint16_t> regs;
        DataType dt = parseDataType(dataType);
        switch (dt) {
        case DataType::Int16:   regs = conv.fromInt16(static_cast<int16_t>(valueJson.toDouble())); break;
        case DataType::UInt16:  regs = conv.fromUInt16(static_cast<uint16_t>(valueJson.toDouble())); break;
        case DataType::Int32:   regs = conv.fromInt32(static_cast<int32_t>(valueJson.toDouble())); break;
        case DataType::UInt32:  regs = conv.fromUInt32(static_cast<uint32_t>(valueJson.toDouble())); break;
        case DataType::Float32: regs = conv.fromFloat32(static_cast<float>(valueJson.toDouble())); break;
        case DataType::Int64: {
            qint64 i64 = valueJson.isString() ? valueJson.toString().toLongLong()
                                               : static_cast<qint64>(valueJson.toDouble());
            regs = conv.fromInt64(i64);
            break;
        }
        case DataType::UInt64: {
            quint64 u64 = valueJson.isString() ? valueJson.toString().toULongLong()
                                                : static_cast<quint64>(valueJson.toDouble());
            regs = conv.fromUInt64(u64);
            break;
        }
        case DataType::Float64: regs = conv.fromFloat64(valueJson.toDouble()); break;
        }

        auto result = client->writeMultipleRegisters(unitId, addr, regs, timeout);
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
        for (const auto& v : arr) values.append(v.toInt());
        auto result = client->writeMultipleRegisters(unitId, addr, values, timeout);
        if (result.success) {
            resp.done(0, QJsonObject{{"written", values.size()}});
        } else {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
        }
    }
    else {
        resp.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
    }
}

void ModbusRtuSerialHandler::buildMeta() {
    auto readHolding = CommandBuilder("read_holding_registers")
        .description("读取保持寄存器（功能码 0x03），支持多种数据类型解码")
        .param(serialParam("port_name"))
        .param(serialParam("baud_rate"))
        .param(serialParam("data_bits"))
        .param(serialParam("stop_bits"))
        .param(serialParam("parity"))
        .param(serialParam("unit_id"))
        .param(serialParam("timeout"))
        .param(FieldBuilder("address", FieldType::Int)
            .required().range(0, 65535).description("Modbus 起始地址（0-65535）"))
        .param(FieldBuilder("count", FieldType::Int)
            .defaultValue(1).range(1, 125).description("连续读取的数量"))
        .param(FieldBuilder("data_type", FieldType::Enum)
            .defaultValue("uint16").enumValues(dataTypeEnum())
            .description("值解码/编码类型：int16/uint16(1寄存器)、int32/uint32/float32(2寄存器)、int64/uint64/float64(4寄存器)"))
        .param(FieldBuilder("byte_order", FieldType::Enum)
            .defaultValue("big_endian").enumValues(byteOrderEnum())
            .description("多寄存器字节序：big_endian(AB CD) / little_endian(DC BA) / big_endian_byte_swap(BA DC) / little_endian_byte_swap(CD AB)"));
    readHolding.example("读取地址 0 起的 10 个保持寄存器", QStringList{"stdio", "console"},
        QJsonObject{{"port_name", "COM3"}, {"unit_id", 1},
                    {"address", 0}, {"count", 10}});

    auto writeHolding = CommandBuilder("write_holding_register")
        .description("写单个保持寄存器（功能码 0x06），写入 16 位无符号整数")
        .param(serialParam("port_name"))
        .param(serialParam("baud_rate"))
        .param(serialParam("data_bits"))
        .param(serialParam("stop_bits"))
        .param(serialParam("parity"))
        .param(serialParam("unit_id"))
        .param(serialParam("timeout"))
        .param(FieldBuilder("address", FieldType::Int)
            .required().range(0, 65535).description("目标寄存器地址（0-65535）"))
        .param(FieldBuilder("value", FieldType::Int)
            .required().range(0, 65535).description("写入的 16 位无符号整数值（0-65535）"));
    writeHolding.example("向地址 100 写入值 1000", QStringList{"stdio", "console"},
        QJsonObject{{"port_name", "COM3"}, {"unit_id", 1},
                    {"address", 100}, {"value", 1000}});

    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("modbus.rtu_serial", "ModbusRTU Serial Master", "1.0.0",
              "Modbus RTU 串口主站驱动，通过 RS485 串口直连 Modbus 从站设备")
        .vendor("stdiolink")
        .command(CommandBuilder("status")
            .description("获取驱动存活状态，固定返回 ready")
            .example("查询驱动状态", QStringList{"stdio", "console"}, QJsonObject{}))
        .command(CommandBuilder("read_coils")
            .description("读取线圈状态（功能码 0x01），返回 bool 数组")
            .param(serialParam("port_name"))
            .param(serialParam("baud_rate"))
            .param(serialParam("data_bits"))
            .param(serialParam("stop_bits"))
            .param(serialParam("parity"))
            .param(serialParam("unit_id"))
            .param(serialParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("Modbus 起始地址（0-65535）"))
            .param(FieldBuilder("count", FieldType::Int)
                .defaultValue(1).range(1, 2000).description("连续读取的数量"))
            .example("读取地址 0 起的 8 个线圈", QStringList{"stdio", "console"}, QJsonObject{{"port_name", "COM3"}, {"unit_id", 1}, {"address", 0}, {"count", 8}}))
        .command(CommandBuilder("read_discrete_inputs")
            .description("读取离散输入状态（功能码 0x02），返回 bool 数组")
            .param(serialParam("port_name"))
            .param(serialParam("baud_rate"))
            .param(serialParam("data_bits"))
            .param(serialParam("stop_bits"))
            .param(serialParam("parity"))
            .param(serialParam("unit_id"))
            .param(serialParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("Modbus 起始地址（0-65535）"))
            .param(FieldBuilder("count", FieldType::Int)
                .defaultValue(1).range(1, 2000).description("连续读取的数量"))
            .example("读取地址 0 起的 8 个离散输入", QStringList{"stdio", "console"}, QJsonObject{{"port_name", "COM3"}, {"unit_id", 1}, {"address", 0}, {"count", 8}}))
        .command(readHolding)
        .command(CommandBuilder("read_input_registers")
            .description("读取输入寄存器（功能码 0x04），支持多种数据类型解码")
            .param(serialParam("port_name"))
            .param(serialParam("baud_rate"))
            .param(serialParam("data_bits"))
            .param(serialParam("stop_bits"))
            .param(serialParam("parity"))
            .param(serialParam("unit_id"))
            .param(serialParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("Modbus 起始地址（0-65535）"))
            .param(FieldBuilder("count", FieldType::Int)
                .defaultValue(1).range(1, 125).description("连续读取的数量"))
            .param(FieldBuilder("data_type", FieldType::Enum)
                .defaultValue("uint16").enumValues(dataTypeEnum())
                .description("值解码/编码类型：int16/uint16(1寄存器)、int32/uint32/float32(2寄存器)、int64/uint64/float64(4寄存器)"))
            .param(FieldBuilder("byte_order", FieldType::Enum)
                .defaultValue("big_endian").enumValues(byteOrderEnum())
                .description("多寄存器字节序：big_endian(AB CD) / little_endian(DC BA) / big_endian_byte_swap(BA DC) / little_endian_byte_swap(CD AB)"))
            .example("读取地址 0 起的 10 个输入寄存器", QStringList{"stdio", "console"}, QJsonObject{{"port_name", "COM3"}, {"unit_id", 1}, {"address", 0}, {"count", 10}}))
        .command(CommandBuilder("write_coil")
            .description("写单个线圈（功能码 0x05），true=ON / false=OFF")
            .param(serialParam("port_name"))
            .param(serialParam("baud_rate"))
            .param(serialParam("data_bits"))
            .param(serialParam("stop_bits"))
            .param(serialParam("parity"))
            .param(serialParam("unit_id"))
            .param(serialParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("目标线圈地址（0-65535）"))
            .param(FieldBuilder("value", FieldType::Bool)
                .required().description("线圈值：true=ON / false=OFF"))
            .example("打开地址 0 的线圈", QStringList{"stdio", "console"}, QJsonObject{{"port_name", "COM3"}, {"unit_id", 1}, {"address", 0}, {"value", true}}))
        .command(CommandBuilder("write_coils")
            .description("批量写多个线圈（功能码 0x0F）")
            .param(serialParam("port_name"))
            .param(serialParam("baud_rate"))
            .param(serialParam("data_bits"))
            .param(serialParam("stop_bits"))
            .param(serialParam("parity"))
            .param(serialParam("unit_id"))
            .param(serialParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("Modbus 起始地址（0-65535）"))
            .param(FieldBuilder("values", FieldType::Array)
                .required().description("线圈值 bool 数组，按顺序对应起始地址起的连续线圈"))
            .example("批量设置 4 个线圈", QStringList{"stdio", "console"}, QJsonObject{{"port_name", "COM3"}, {"unit_id", 1}, {"address", 0}, {"values", QJsonArray{true, false, true, false}}}))
        .command(writeHolding)
        .command(CommandBuilder("write_holding_registers")
            .description("写多个保持寄存器（功能码 0x10），按 data_type 编码后写入")
            .param(serialParam("port_name"))
            .param(serialParam("baud_rate"))
            .param(serialParam("data_bits"))
            .param(serialParam("stop_bits"))
            .param(serialParam("parity"))
            .param(serialParam("unit_id"))
            .param(serialParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("Modbus 起始地址（0-65535）"))
            .param(FieldBuilder("value", FieldType::Any)
                .required().description("要写入的值，按 data_type 编码；int64/uint64 类型可传字符串以保留 64 位精度"))
            .param(FieldBuilder("data_type", FieldType::Enum)
                .defaultValue("uint16").enumValues(dataTypeEnum())
                .description("值解码/编码类型：int16/uint16(1寄存器)、int32/uint32/float32(2寄存器)、int64/uint64/float64(4寄存器)"))
            .param(FieldBuilder("byte_order", FieldType::Enum)
                .defaultValue("big_endian").enumValues(byteOrderEnum())
                .description("多寄存器字节序：big_endian(AB CD) / little_endian(DC BA) / big_endian_byte_swap(BA DC) / little_endian_byte_swap(CD AB)"))
            .example("以 float32 写入 3.14 到地址 100", QStringList{"stdio", "console"}, QJsonObject{{"port_name", "COM3"}, {"unit_id", 1}, {"address", 100}, {"value", 3.14}, {"data_type", "float32"}}))
        .command(CommandBuilder("write_holding_registers_raw")
            .description("写多个保持寄存器（功能码 0x10），直接写入 uint16 原始值数组")
            .param(serialParam("port_name"))
            .param(serialParam("baud_rate"))
            .param(serialParam("data_bits"))
            .param(serialParam("stop_bits"))
            .param(serialParam("parity"))
            .param(serialParam("unit_id"))
            .param(serialParam("timeout"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("Modbus 起始地址（0-65535）"))
            .param(FieldBuilder("values", FieldType::Array)
                .required().description("uint16 原始寄存器值数组，按顺序写入起始地址起的连续寄存器"))
            .example("向地址 0 写入 3 个原始寄存器值", QStringList{"stdio", "console"}, QJsonObject{{"port_name", "COM3"}, {"unit_id", 1}, {"address", 0}, {"values", QJsonArray{100, 200, 300}}}))
        .build();
}

