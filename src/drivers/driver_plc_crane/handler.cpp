#include "handler.h"

#include <QJsonObject>

#include "stdiolink/driver/meta_builder.h"

using namespace modbus;

ModbusClient* PlcCraneHandler::getClient(const QString& host, int port, int timeout,
                                         IResponder& resp) {
    ConnectionKey key{host, static_cast<quint16>(port)};
    auto it = m_connections.find(key);
    if (it != m_connections.end() && it->get()->isConnected()) {
        it->get()->setTimeout(timeout);
        return it->get();
    }
    auto client = std::make_shared<ModbusClient>(timeout);
    if (!client->connectToServer(host, port)) {
        resp.error(
            1, QJsonObject{{"message", QString("Failed to connect to %1:%2").arg(host).arg(port)}});
        return nullptr;
    }
    m_connections[key] = client;
    return client.get();
}

void PlcCraneHandler::handle(const QString& cmd, const QJsonValue& data, IResponder& resp) {
    QJsonObject p = data.toObject();

    if (cmd == "status") {
        resp.done(0, QJsonObject{{"status", "ready"}});
        return;
    }

    QString host = p["host"].toString();
    int port = p["port"].toInt(502);
    int unitId = p["unit_id"].toInt(1);
    int timeout = p["timeout"].toInt(3000);
    auto endpointError = [&](const QString& message) {
        return QString("%1 (%2:%3)").arg(message, host).arg(port);
    };

    // Pre-validate action/mode enums before connecting
    if (cmd == "cylinder_control") {
        QString action = p["action"].toString();
        if (action != "up" && action != "down" && action != "stop") {
            resp.error(
                3, QJsonObject{
                       {"message",
                        QString("Invalid action: '%1', expected: up, down, stop").arg(action)}});
            return;
        }
    } else if (cmd == "valve_control") {
        QString action = p["action"].toString();
        if (action != "open" && action != "close" && action != "stop") {
            resp.error(
                3, QJsonObject{
                       {"message",
                        QString("Invalid action: '%1', expected: open, close, stop").arg(action)}});
            return;
        }
    } else if (cmd == "set_mode") {
        QString mode = p["mode"].toString();
        if (mode != "manual" && mode != "auto") {
            resp.error(
                3, QJsonObject{{"message",
                                QString("Invalid mode: '%1', expected: manual, auto").arg(mode)}});
            return;
        }
    } else if (cmd == "set_run") {
        QString action = p["action"].toString();
        if (action != "start" && action != "stop") {
            resp.error(
                3,
                QJsonObject{{"message",
                             QString("Invalid action: '%1', expected: start, stop").arg(action)}});
            return;
        }
    }

    auto* client = getClient(host, port, timeout, resp);
    if (!client)
        return;
    client->setUnitId(unitId);

    if (cmd == "read_status") {
        auto result = client->readHoldingRegisters(9, 6);
        if (!result.success) {
            resp.error(2, QJsonObject{{"message", endpointError(result.errorMessage)}});
            return;
        }
        resp.done(0, QJsonObject{{"cylinder_up", result.registers[0] != 0},
                                 {"cylinder_down", result.registers[1] != 0},
                                 {"valve_open", result.registers[4] != 0},
                                 {"valve_closed", result.registers[5] != 0}});
    } else if (cmd == "cylinder_control") {
        QString action = p["action"].toString();
        quint16 value = 0;
        if (action == "up")
            value = 1;
        else if (action == "down")
            value = 2;

        auto result = client->writeSingleRegister(0, value);
        if (!result.success) {
            resp.error(2, QJsonObject{{"message", endpointError(result.errorMessage)}});
            return;
        }
        resp.done(0, QJsonObject{{"written", true}, {"action", action}});
    } else if (cmd == "valve_control") {
        QString action = p["action"].toString();
        quint16 value = 0;
        if (action == "open")
            value = 1;
        else if (action == "close")
            value = 2;

        auto result = client->writeSingleRegister(1, value);
        if (!result.success) {
            resp.error(2, QJsonObject{{"message", endpointError(result.errorMessage)}});
            return;
        }
        resp.done(0, QJsonObject{{"written", true}, {"action", action}});
    } else if (cmd == "set_run") {
        QString action = p["action"].toString();
        quint16 value = (action == "start") ? 1 : 0;

        auto result = client->writeSingleRegister(2, value);
        if (!result.success) {
            resp.error(2, QJsonObject{{"message", endpointError(result.errorMessage)}});
            return;
        }
        resp.done(0, QJsonObject{{"written", true}, {"action", action}});
    } else if (cmd == "set_mode") {
        QString mode = p["mode"].toString();
        quint16 value = (mode == "auto") ? 1 : 0;

        auto result = client->writeSingleRegister(3, value);
        if (!result.success) {
            resp.error(2, QJsonObject{{"message", endpointError(result.errorMessage)}});
            return;
        }
        resp.done(0, QJsonObject{{"written", true}, {"mode", mode}});
    } else {
        resp.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
    }
}

static FieldBuilder connectionParam(const QString& name) {
    if (name == "host") {
        return FieldBuilder("host", FieldType::String)
            .required()
            .description("PLC Modbus TCP 地址，如 192.168.1.100")
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
            .description("Modbus 从站地址（1-247），对应 PLC 站号");
    }
    return FieldBuilder("timeout", FieldType::Int)
        .defaultValue(3000)
        .range(100, 30000)
        .unit("ms")
        .description("通信超时时间（毫秒），默认 3000");
}

void PlcCraneHandler::buildMeta() {
    m_meta = DriverMetaBuilder()
                 .schemaVersion("1.0")
                 .info("plc.crane", "PLC汽动升降装置", "1.0.0",
                       "高炉 PLC 升降装置 Modbus TCP 驱动，将 PLC 保持寄存器映射为语义化命令")
                 .vendor("stdiolink")
                 .command(CommandBuilder("status")
                              .description("获取驱动存活状态，固定返回 ready"))
                 .command(CommandBuilder("read_status")
                              .description("读取气缸和球阀到位状态（保持寄存器 9/10/13/14，功能码 0x03）")
                              .param(connectionParam("host"))
                              .param(connectionParam("port"))
                              .param(connectionParam("unit_id"))
                              .param(connectionParam("timeout"))
                              .returns(FieldType::Object, "包含 cylinder_up/cylinder_down/valve_open/valve_closed 布尔值")
                              .example("读取气缸和球阀状态", QStringList{"stdio", "console"},
                                       QJsonObject{{"host", "127.0.0.1"},
                                                   {"port", 502},
                                                   {"unit_id", 1}}))
                 .command(CommandBuilder("cylinder_control")
                              .description("手动模式下控制气缸升降（寄存器 0，功能码 0x06：1=上升, 2=下降, 0=停止）")
                              .param(connectionParam("host"))
                              .param(connectionParam("port"))
                              .param(connectionParam("unit_id"))
                              .param(connectionParam("timeout"))
                              .param(FieldBuilder("action", FieldType::Enum)
                                         .required()
                                         .enumValues(QStringList{"up", "down", "stop"})
                                         .description("up=气缸上升 / down=气缸下降 / stop=停止"))
                              .returns(FieldType::Object, "写入结果，包含 written 和 action")
                              .example("气缸上升", QStringList{"stdio", "console"},
                                       QJsonObject{{"host", "127.0.0.1"},
                                                   {"port", 502},
                                                   {"unit_id", 1},
                                                   {"action", "up"}}))
                 .command(CommandBuilder("valve_control")
                              .description("手动模式下控制球阀开关（寄存器 1，功能码 0x06：1=打开, 2=关闭, 0=停止）")
                              .param(connectionParam("host"))
                              .param(connectionParam("port"))
                              .param(connectionParam("unit_id"))
                              .param(connectionParam("timeout"))
                              .param(FieldBuilder("action", FieldType::Enum)
                                         .required()
                                         .enumValues(QStringList{"open", "close", "stop"})
                                         .description("open=打开球阀 / close=关闭球阀 / stop=停止"))
                              .returns(FieldType::Object, "写入结果，包含 written 和 action")
                              .example("打开球阀", QStringList{"stdio", "console"},
                                       QJsonObject{{"host", "127.0.0.1"},
                                                   {"port", 502},
                                                   {"unit_id", 1},
                                                   {"action", "open"}}))
                 .command(CommandBuilder("set_run")
                              .description("启动或停止 PLC 运行（寄存器 2，功能码 0x06：1=启动, 0=停止）")
                              .param(connectionParam("host"))
                              .param(connectionParam("port"))
                              .param(connectionParam("unit_id"))
                              .param(connectionParam("timeout"))
                              .param(FieldBuilder("action", FieldType::Enum)
                                         .required()
                                         .enumValues(QStringList{"start", "stop"})
                                         .description("start=启动运行 / stop=停止运行"))
                              .returns(FieldType::Object, "写入结果，包含 written 和 action")
                              .example("启动运行", QStringList{"stdio", "console"},
                                       QJsonObject{{"host", "127.0.0.1"},
                                                   {"port", 502},
                                                   {"unit_id", 1},
                                                   {"action", "start"}}))
                 .command(CommandBuilder("set_mode")
                              .description("切换手动/自动模式（寄存器 3，功能码 0x06：0=手动, 1=自动）")
                              .param(connectionParam("host"))
                              .param(connectionParam("port"))
                              .param(connectionParam("unit_id"))
                              .param(connectionParam("timeout"))
                              .param(FieldBuilder("mode", FieldType::Enum)
                                         .required()
                                         .enumValues(QStringList{"manual", "auto"})
                                         .description("manual=手动模式（可逐项控制气缸/阀门） / auto=自动模式"))
                              .returns(FieldType::Object, "写入结果，包含 written 和 mode")
                              .example("切换到自动模式", QStringList{"stdio", "console"},
                                       QJsonObject{{"host", "127.0.0.1"},
                                                   {"port", 502},
                                                   {"unit_id", 1},
                                                   {"mode", "auto"}}))
                 .build();
}
