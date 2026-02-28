#include "handler.h"

#include <cmath>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>

#include "stdiolink/driver/meta_builder.h"
#include "modbus_types.h"

using namespace modbus;

ModbusTcpServerHandler::ModbusTcpServerHandler() {
    buildMeta();
    connectEvents();
}

void ModbusTcpServerHandler::connectEvents() {
    QObject::connect(&m_server, &ModbusTcpServer::clientConnected,
            [this](const QString& addr, quint16 port) {
        m_eventResponder.event("client_connected", 0,
            QJsonObject{{"address", addr}, {"port", port}});
    });
    QObject::connect(&m_server, &ModbusTcpServer::clientDisconnected,
            [this](const QString& addr, quint16 port) {
        m_eventResponder.event("client_disconnected", 0,
            QJsonObject{{"address", addr}, {"port", port}});
    });
    QObject::connect(&m_server, &ModbusTcpServer::dataWritten,
            [this](quint8 unitId, quint8 fc, quint16 addr, quint16 qty) {
        if (m_eventMode == "none" || m_eventMode == "read") return;
        m_eventResponder.event("data_written", 0, QJsonObject{
            {"unit_id", unitId}, {"function_code", fc},
            {"address", addr}, {"quantity", qty}});
    });
    QObject::connect(&m_server, &ModbusTcpServer::dataRead,
            [this](quint8 unitId, quint8 fc, quint16 addr, quint16 qty) {
        if (m_eventMode == "none" || m_eventMode == "write") return;
        m_eventResponder.event("data_read", 0, QJsonObject{
            {"unit_id", unitId}, {"function_code", fc},
            {"address", addr}, {"quantity", qty}});
    });
}

void ModbusTcpServerHandler::handle(const QString& cmd, const QJsonValue& data,
        IResponder& resp) {
    QJsonObject p = data.toObject();

    if (cmd == "status") {
        QJsonArray units;
        for (auto id : m_server.getUnits()) units.append(id);
        resp.done(0, QJsonObject{
            {"status", "ready"},
            {"listening", m_server.isRunning()},
            {"port", m_server.isRunning() ? m_server.serverPort() : 0},
            {"event_mode", m_eventMode},
            {"units", units}});
        return;
    }

    if (cmd == "run") {
        if (m_server.isRunning()) {
            resp.error(3, QJsonObject{{"message", "Server already running"}});
            return;
        }
        // event_mode 显式类型校验（与 start_server 一致）
        static const QStringList validModes = {"write", "all", "read", "none"};
        QString eventMode = "write";
        if (p.contains("event_mode")) {
            if (!p["event_mode"].isString()) {
                resp.error(3, QJsonObject{{"message", "event_mode must be a string"}});
                return;
            }
            eventMode = p["event_mode"].toString();
        }
        if (!validModes.contains(eventMode)) {
            resp.error(3, QJsonObject{{"message",
                QString("Invalid event_mode: %1").arg(eventMode)}});
            return;
        }
        // listen_address 显式类型校验
        QString addr;
        if (p.contains("listen_address")) {
            if (!p["listen_address"].isString()) {
                resp.error(3, QJsonObject{{"message", "listen_address must be a string"}});
                return;
            }
            addr = p["listen_address"].toString();
        }
        int port = p["listen_port"].toInt(502);
        if (!m_server.startServer(static_cast<quint16>(port), addr)) {
            resp.error(1, QJsonObject{{"message",
                QString("Failed to listen on %1:%2: %3")
                    .arg(addr.isEmpty() ? "0.0.0.0" : addr)
                    .arg(port).arg(m_server.errorString())}});
            return;
        }
        m_eventMode = eventMode;
        // units 批量添加（带校验）
        QJsonArray unitsArr = p["units"].toArray();
        QJsonArray addedUnits;
        for (int i = 0; i < unitsArr.size(); ++i) {
            QJsonObject uo = unitsArr[i].toObject();
            if (!uo.contains("id") || !uo["id"].isDouble()) {
                m_server.stopServer();
                resp.error(3, QJsonObject{{"message",
                    QString("units[%1]: missing or invalid 'id'").arg(i)}});
                return;
            }
            double idVal = uo["id"].toDouble();
            if (idVal != std::floor(idVal)) {
                m_server.stopServer();
                resp.error(3, QJsonObject{{"message",
                    QString("units[%1]: id must be an integer").arg(i)}});
                return;
            }
            int uid = static_cast<int>(idVal);
            if (uid < 1 || uid > 247) {
                m_server.stopServer();
                resp.error(3, QJsonObject{{"message",
                    QString("units[%1]: id %2 out of range [1,247]").arg(i).arg(uid)}});
                return;
            }
            int sz = uo.value("size").toInt(10000);
            if (!m_server.addUnit(static_cast<quint8>(uid), sz)) {
                m_server.stopServer();
                resp.error(3, QJsonObject{{"message",
                    QString("units[%1]: failed to add unit %2 (duplicate?)").arg(i).arg(uid)}});
                return;
            }
            addedUnits.append(uid);
        }
        m_eventResponder.event("started", 0, QJsonObject{
            {"port", m_server.serverPort()},
            {"units", addedUnits},
            {"event_mode", m_eventMode}});
        QEventLoop().exec();
        return;
    }

    if (cmd == "start_server") {
        if (m_server.isRunning()) {
            resp.error(3, QJsonObject{{"message", "Server already running"}});
            return;
        }
        static const QStringList validModes = {"write", "all", "read", "none"};
        QString eventMode = "write";
        if (p.contains("event_mode")) {
            if (!p["event_mode"].isString()) {
                resp.error(3, QJsonObject{{"message", "event_mode must be a string"}});
                return;
            }
            eventMode = p["event_mode"].toString();
        }
        if (!validModes.contains(eventMode)) {
            resp.error(3, QJsonObject{{"message",
                QString("Invalid event_mode: %1").arg(eventMode)}});
            return;
        }
        // listen_address 显式类型校验
        QString addr;
        if (p.contains("listen_address")) {
            if (!p["listen_address"].isString()) {
                resp.error(3, QJsonObject{{"message", "listen_address must be a string"}});
                return;
            }
            addr = p["listen_address"].toString();
        }
        int port = p["listen_port"].toInt(502);
        if (!m_server.startServer(static_cast<quint16>(port), addr)) {
            resp.error(1, QJsonObject{{"message",
                QString("Failed to listen on %1:%2: %3")
                    .arg(addr.isEmpty() ? "0.0.0.0" : addr)
                    .arg(port).arg(m_server.errorString())}});
            return;
        }
        m_eventMode = eventMode;
        resp.done(0, QJsonObject{{"started", true},
            {"port", m_server.serverPort()}});
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
            QVector<quint16> r;
            switch (dt) {
            case DataType::Int16:   r = conv.fromInt16(static_cast<int16_t>(v.toDouble())); break;
            case DataType::UInt16:  r = conv.fromUInt16(static_cast<uint16_t>(v.toDouble())); break;
            case DataType::Int32:   r = conv.fromInt32(static_cast<int32_t>(v.toDouble())); break;
            case DataType::UInt32:  r = conv.fromUInt32(static_cast<uint32_t>(v.toDouble())); break;
            case DataType::Float32: r = conv.fromFloat32(static_cast<float>(v.toDouble())); break;
            case DataType::Int64: {
                qint64 i64 = v.isString() ? v.toString().toLongLong()
                                           : static_cast<qint64>(v.toDouble());
                r = conv.fromInt64(i64);
                break;
            }
            case DataType::UInt64: {
                quint64 u64 = v.isString() ? v.toString().toULongLong()
                                            : static_cast<quint64>(v.toDouble());
                r = conv.fromUInt64(u64);
                break;
            }
            case DataType::Float64: r = conv.fromFloat64(v.toDouble()); break;
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
            case DataType::UInt64:  values.append(QString::number(conv.toUInt64(raw, i))); break;
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

void ModbusTcpServerHandler::buildMeta() {
    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("modbus.tcp_server", "ModbusTCP Server", "1.0.0",
              "Modbus TCP 从站驱动，监听 TCP 端口响应主站读写请求")
        .vendor("stdiolink")
        .profile("keepalive")
        .command(CommandBuilder("run")
            .description("一键启动从站服务并进入事件循环（支持 OneShot 模式）")
            .param(FieldBuilder("listen_address", FieldType::String)
                .defaultValue("").description("监听地址（空=所有接口）"))
            .param(FieldBuilder("listen_port", FieldType::Int)
                .defaultValue(502).range(1, 65535)
                .description("监听端口"))
            .param(FieldBuilder("units", FieldType::Array)
                .required().description("从站 Unit 数组，每项 {id, size?}"))
            .param(FieldBuilder("event_mode", FieldType::Enum)
                .defaultValue("write")
                .enumValues(QStringList{"write", "all", "read", "none"})
                .description("事件推送模式")))
        .command(CommandBuilder("status")
            .description("获取驱动状态"))
        .command(CommandBuilder("start_server")
            .description("启动从站服务")
            .param(FieldBuilder("listen_address", FieldType::String)
                .defaultValue("").description("监听地址（空=所有接口）"))
            .param(FieldBuilder("listen_port", FieldType::Int)
                .defaultValue(502).range(1, 65535)
                .description("监听端口"))
            .param(FieldBuilder("event_mode", FieldType::Enum)
                .defaultValue("write")
                .enumValues(QStringList{"write", "all", "read", "none"})
                .description("事件推送模式：write=仅写, all=读写, read=仅读, none=无")))
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