#include "handler.h"

#include <QJsonArray>
#include <QJsonObject>

#include "stdiolink/driver/meta_builder.h"
#include "modbus_types.h"

using namespace modbus;

ModbusRtuSerialServerHandler::ModbusRtuSerialServerHandler() {
    buildMeta();
    connectEvents();
}

void ModbusRtuSerialServerHandler::connectEvents() {
    QObject::connect(&m_server, &ModbusRtuSerialServer::dataWritten,
            [this](quint8 unitId, quint8 fc, quint16 addr, quint16 qty) {
        m_eventResponder.event("data_written", 0, QJsonObject{
            {"unit_id", unitId}, {"function_code", fc},
            {"address", addr}, {"quantity", qty}});
    });
}

void ModbusRtuSerialServerHandler::handle(const QString& cmd, const QJsonValue& data,
        IResponder& resp) {
    QJsonObject p = data.toObject();

    if (cmd == "status") {
        QJsonArray units;
        for (auto id : m_server.getUnits()) units.append(id);
        resp.done(0, QJsonObject{
            {"status", "ready"},
            {"listening", m_server.isRunning()},
            {"port_name", m_server.portName()},
            {"units", units}});
        return;
    }

    if (cmd == "start_server") {
        if (m_server.isRunning()) {
            resp.error(3, QJsonObject{{"message", "Server already running"}});
            return;
        }
        QString portName = p["port_name"].toString();
        int baudRate = p["baud_rate"].toInt(9600);
        int dataBits = p["data_bits"].toInt(8);
        QString stopBits = p["stop_bits"].toString("1");
        QString parity = p["parity"].toString("none");
        if (!m_server.startServer(portName, baudRate, dataBits, stopBits, parity)) {
            resp.error(1, QJsonObject{{"message",
                QString("Failed to open serial port %1").arg(portName)}});
            return;
        }
        resp.done(0, QJsonObject{{"started", true},
            {"port_name", m_server.portName()}});
        return;
    }

    if (cmd == "stop_server") {
        if (!m_server.isRunning()) {
            resp.error(3, QJsonObject{{"message", "Server not running"}});
            return;
        }
        m_server.stopServer();
        resp.done(0, QJsonObject{{"stopped", true}});
        return;
    }

    if (cmd == "add_unit") {
        int unitId = p["unit_id"].toInt();
        int size = p["data_area_size"].toInt(10000);
        if (!m_server.addUnit(static_cast<quint8>(unitId), size)) {
            resp.error(3, QJsonObject{{"message",
                QString("Unit %1 already exists").arg(unitId)}});
            return;
        }
        resp.done(0, QJsonObject{{"added", true},
            {"unit_id", unitId}, {"data_area_size", size}});
        return;
    }

    if (cmd == "remove_unit") {
        int unitId = p["unit_id"].toInt();
        if (!m_server.removeUnit(static_cast<quint8>(unitId))) {
            resp.error(3, QJsonObject{{"message",
                QString("Unit %1 not found").arg(unitId)}});
            return;
        }
        resp.done(0, QJsonObject{{"removed", true}, {"unit_id", unitId}});
        return;
    }

    if (cmd == "list_units") {
        QJsonArray units;
        for (auto id : m_server.getUnits()) units.append(id);
        resp.done(0, QJsonObject{{"units", units}});
        return;
    }

    // Data access commands — extract unit_id
    int unitId = p["unit_id"].toInt();
    quint8 uid = static_cast<quint8>(unitId);

    if (!m_server.hasUnit(uid)) {
        resp.error(3, QJsonObject{{"message",
            QString("Unit %1 not found").arg(unitId)}});
        return;
    }

    int address = p["address"].toInt();
    quint16 addr = static_cast<quint16>(address);

    if (cmd == "set_coil") {
        bool value = p["value"].toBool();
        if (!m_server.setCoil(uid, addr, value)) {
            resp.error(3, QJsonObject{{"message",
                QString("Address %1 out of range").arg(address)}});
            return;
        }
        resp.done(0, QJsonObject{{"written", true}});
        return;
    }

    if (cmd == "get_coil") {
        bool value = false;
        if (!m_server.getCoil(uid, addr, value)) {
            resp.error(3, QJsonObject{{"message",
                QString("Address %1 out of range").arg(address)}});
            return;
        }
        resp.done(0, QJsonObject{{"value", value}});
        return;
    }

    if (cmd == "set_discrete_input") {
        bool value = p["value"].toBool();
        if (!m_server.setDiscreteInput(uid, addr, value)) {
            resp.error(3, QJsonObject{{"message",
                QString("Address %1 out of range").arg(address)}});
            return;
        }
        resp.done(0, QJsonObject{{"written", true}});
        return;
    }

    if (cmd == "get_discrete_input") {
        bool value = false;
        if (!m_server.getDiscreteInput(uid, addr, value)) {
            resp.error(3, QJsonObject{{"message",
                QString("Address %1 out of range").arg(address)}});
            return;
        }
        resp.done(0, QJsonObject{{"value", value}});
        return;
    }

    if (cmd == "set_holding_register") {
        int value = p["value"].toInt();
        if (!m_server.setHoldingRegister(uid, addr, static_cast<quint16>(value))) {
            resp.error(3, QJsonObject{{"message",
                QString("Address %1 out of range").arg(address)}});
            return;
        }
        resp.done(0, QJsonObject{{"written", true}});
        return;
    }

    if (cmd == "get_holding_register") {
        quint16 value = 0;
        if (!m_server.getHoldingRegister(uid, addr, value)) {
            resp.error(3, QJsonObject{{"message",
                QString("Address %1 out of range").arg(address)}});
            return;
        }
        resp.done(0, QJsonObject{{"value", value}});
        return;
    }

    if (cmd == "set_input_register") {
        int value = p["value"].toInt();
        if (!m_server.setInputRegister(uid, addr, static_cast<quint16>(value))) {
            resp.error(3, QJsonObject{{"message",
                QString("Address %1 out of range").arg(address)}});
            return;
        }
        resp.done(0, QJsonObject{{"written", true}});
        return;
    }

    if (cmd == "get_input_register") {
        quint16 value = 0;
        if (!m_server.getInputRegister(uid, addr, value)) {
            resp.error(3, QJsonObject{{"message",
                QString("Address %1 out of range").arg(address)}});
            return;
        }
        resp.done(0, QJsonObject{{"value", value}});
        return;
    }

    if (cmd == "set_registers_batch") {
        QString area = p["area"].toString("holding");
        QString dataType = p["data_type"].toString("uint16");
        QString byteOrder = p["byte_order"].toString("big_endian");
        QJsonArray valuesArr = p["values"].toArray();

        ByteOrderConverter conv(parseByteOrder(byteOrder));
        DataType dt = parseDataType(dataType);
        int step = registersPerType(dt);

        QVector<quint16> regs;
        for (const auto& v : valuesArr) {
            double val = v.toDouble();
            QVector<quint16> r;
            switch (dt) {
            case DataType::Int16:   r = conv.fromInt16(static_cast<int16_t>(val)); break;
            case DataType::UInt16:  r = conv.fromUInt16(static_cast<uint16_t>(val)); break;
            case DataType::Int32:   r = conv.fromInt32(static_cast<int32_t>(val)); break;
            case DataType::UInt32:  r = conv.fromUInt32(static_cast<uint32_t>(val)); break;
            case DataType::Float32: r = conv.fromFloat32(static_cast<float>(val)); break;
            case DataType::Int64:   r = conv.fromInt64(static_cast<int64_t>(val)); break;
            case DataType::UInt64:  r = conv.fromUInt64(static_cast<uint64_t>(val)); break;
            case DataType::Float64: r = conv.fromFloat64(val); break;
            }
            regs.append(r);
        }

        bool ok = true;
        for (int i = 0; i < regs.size(); i++) {
            if (area == "input") {
                ok = m_server.setInputRegister(uid, addr + i, regs[i]);
            } else {
                ok = m_server.setHoldingRegister(uid, addr + i, regs[i]);
            }
            if (!ok) {
                resp.error(3, QJsonObject{{"message",
                    QString("Address %1 out of range").arg(addr + i)}});
                return;
            }
        }
        resp.done(0, QJsonObject{{"written", regs.size()}});
        return;
    }

    if (cmd == "get_registers_batch") {
        QString area = p["area"].toString("holding");
        int count = p["count"].toInt();
        QString dataType = p["data_type"].toString("uint16");
        QString byteOrder = p["byte_order"].toString("big_endian");

        DataType dt = parseDataType(dataType);
        int step = registersPerType(dt);

        if (count % step != 0) {
            resp.error(3, QJsonObject{{"message",
                QString("count %1 is not a multiple of %2 registers per %3")
                    .arg(count).arg(step).arg(dataType)}});
            return;
        }

        QVector<quint16> raw;
        for (int i = 0; i < count; i++) {
            quint16 val = 0;
            bool ok;
            if (area == "input") {
                ok = m_server.getInputRegister(uid, addr + i, val);
            } else {
                ok = m_server.getHoldingRegister(uid, addr + i, val);
            }
            if (!ok) {
                resp.error(3, QJsonObject{{"message",
                    QString("Address %1 out of range").arg(addr + i)}});
                return;
            }
            raw.append(val);
        }

        ByteOrderConverter conv(parseByteOrder(byteOrder));
        QJsonArray values;
        for (int i = 0; i + step <= raw.size(); i += step) {
            switch (dt) {
            case DataType::Int16:   values.append(conv.toInt16(raw, i)); break;
            case DataType::UInt16:  values.append(conv.toUInt16(raw, i)); break;
            case DataType::Int32:   values.append(conv.toInt32(raw, i)); break;
            case DataType::UInt32:  values.append(static_cast<qint64>(conv.toUInt32(raw, i))); break;
            case DataType::Float32: values.append(conv.toFloat32(raw, i)); break;
            case DataType::Int64:   values.append(conv.toInt64(raw, i)); break;
            case DataType::UInt64:  values.append(static_cast<qint64>(conv.toUInt64(raw, i))); break;
            case DataType::Float64: values.append(conv.toFloat64(raw, i)); break;
            }
        }

        QJsonArray rawArr;
        for (auto v : raw) rawArr.append(v);

        resp.done(0, QJsonObject{{"values", values}, {"raw", rawArr}});
        return;
    }

    resp.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
}

static QStringList dataTypeEnum() {
    return {"int16", "uint16", "int32", "uint32", "float32", "int64", "uint64", "float64"};
}

static QStringList byteOrderEnum() {
    return {"big_endian", "little_endian", "big_endian_byte_swap", "little_endian_byte_swap"};
}

void ModbusRtuSerialServerHandler::buildMeta() {
    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("modbus.rtu_serial_server", "ModbusRTU Serial Server", "1.0.0",
              "Modbus RTU 串口从站驱动，监听串口以 RTU 帧格式响应主站请求")
        .vendor("stdiolink")
        .profile("keepalive")
        .command(CommandBuilder("status")
            .description("获取驱动状态"))
        .command(CommandBuilder("start_server")
            .description("启动从站服务（打开串口）")
            .param(FieldBuilder("port_name", FieldType::String)
                .required().description("串口名称（如 COM1、/dev/ttyUSB0）"))
            .param(FieldBuilder("baud_rate", FieldType::Int)
                .defaultValue(9600).description("波特率"))
            .param(FieldBuilder("data_bits", FieldType::Int)
                .defaultValue(8).enumValues(QStringList{"5","6","7","8"})
                .description("数据位"))
            .param(FieldBuilder("stop_bits", FieldType::String)
                .defaultValue("1").enumValues(QStringList{"1","1.5","2"})
                .description("停止位"))
            .param(FieldBuilder("parity", FieldType::String)
                .defaultValue("none").enumValues(QStringList{"none","even","odd"})
                .description("校验位")))
        .command(CommandBuilder("stop_server")
            .description("停止从站服务"))
        .command(CommandBuilder("add_unit")
            .description("添加从站 Unit")
            .param(FieldBuilder("unit_id", FieldType::Int)
                .required().range(1, 247)
                .description("从站地址"))
            .param(FieldBuilder("data_area_size", FieldType::Int)
                .defaultValue(10000).range(1, 65536)
                .description("数据区大小")))
        .command(CommandBuilder("remove_unit")
            .description("移除从站 Unit")
            .param(FieldBuilder("unit_id", FieldType::Int)
                .required().range(1, 247)
                .description("从站地址")))
        .command(CommandBuilder("list_units")
            .description("列出所有 Unit"))
        .command(CommandBuilder("set_coil")
            .description("设置线圈值")
            .param(FieldBuilder("unit_id", FieldType::Int)
                .required().range(1, 247).description("从站地址"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("线圈地址"))
            .param(FieldBuilder("value", FieldType::Bool)
                .required().description("线圈值")))
        .command(CommandBuilder("get_coil")
            .description("读取线圈值")
            .param(FieldBuilder("unit_id", FieldType::Int)
                .required().range(1, 247).description("从站地址"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("线圈地址")))
        .command(CommandBuilder("set_discrete_input")
            .description("设置离散输入值")
            .param(FieldBuilder("unit_id", FieldType::Int)
                .required().range(1, 247).description("从站地址"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("地址"))
            .param(FieldBuilder("value", FieldType::Bool)
                .required().description("值")))
        .command(CommandBuilder("get_discrete_input")
            .description("读取离散输入值")
            .param(FieldBuilder("unit_id", FieldType::Int)
                .required().range(1, 247).description("从站地址"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("地址")))
        .command(CommandBuilder("set_holding_register")
            .description("设置保持寄存器值")
            .param(FieldBuilder("unit_id", FieldType::Int)
                .required().range(1, 247).description("从站地址"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("寄存器地址"))
            .param(FieldBuilder("value", FieldType::Int)
                .required().range(0, 65535).description("寄存器值")))
        .command(CommandBuilder("get_holding_register")
            .description("读取保持寄存器值")
            .param(FieldBuilder("unit_id", FieldType::Int)
                .required().range(1, 247).description("从站地址"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("寄存器地址")))
        .command(CommandBuilder("set_input_register")
            .description("设置输入寄存器值")
            .param(FieldBuilder("unit_id", FieldType::Int)
                .required().range(1, 247).description("从站地址"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("寄存器地址"))
            .param(FieldBuilder("value", FieldType::Int)
                .required().range(0, 65535).description("寄存器值")))
        .command(CommandBuilder("get_input_register")
            .description("读取输入寄存器值")
            .param(FieldBuilder("unit_id", FieldType::Int)
                .required().range(1, 247).description("从站地址"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("寄存器地址")))
        .command(CommandBuilder("set_registers_batch")
            .description("批量设置寄存器（支持类型转换）")
            .param(FieldBuilder("unit_id", FieldType::Int)
                .required().range(1, 247).description("从站地址"))
            .param(FieldBuilder("area", FieldType::Enum)
                .defaultValue("holding")
                .enumValues(QStringList{"holding", "input"})
                .description("数据区"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("起始地址"))
            .param(FieldBuilder("values", FieldType::Array)
                .required().description("值数组"))
            .param(FieldBuilder("data_type", FieldType::Enum)
                .defaultValue("uint16").enumValues(dataTypeEnum())
                .description("数据类型"))
            .param(FieldBuilder("byte_order", FieldType::Enum)
                .defaultValue("big_endian").enumValues(byteOrderEnum())
                .description("字节序")))
        .command(CommandBuilder("get_registers_batch")
            .description("批量读取寄存器（支持类型转换）")
            .param(FieldBuilder("unit_id", FieldType::Int)
                .required().range(1, 247).description("从站地址"))
            .param(FieldBuilder("area", FieldType::Enum)
                .defaultValue("holding")
                .enumValues(QStringList{"holding", "input"})
                .description("数据区"))
            .param(FieldBuilder("address", FieldType::Int)
                .required().range(0, 65535).description("起始地址"))
            .param(FieldBuilder("count", FieldType::Int)
                .required().range(1, 125).description("寄存器数量"))
            .param(FieldBuilder("data_type", FieldType::Enum)
                .defaultValue("uint16").enumValues(dataTypeEnum())
                .description("数据类型"))
            .param(FieldBuilder("byte_order", FieldType::Enum)
                .defaultValue("big_endian").enumValues(byteOrderEnum())
                .description("字节序")))
        .build();
}