#include "handler.h"

#include <QJsonObject>

#include "stdiolink/driver/meta_builder.h"

using namespace modbus;

ModbusClient* PlcCraneHandler::getClient(const QString& host, int port,
        int timeout, IResponder& resp) {
    ConnectionKey key{host, static_cast<quint16>(port)};
    auto it = m_connections.find(key);
    if (it != m_connections.end() && it->get()->isConnected()) {
        it->get()->setTimeout(timeout);
        return it->get();
    }
    auto client = std::make_shared<ModbusClient>(timeout);
    if (!client->connectToServer(host, port)) {
        resp.error(1, QJsonObject{{"message",
            QString("Failed to connect to %1:%2").arg(host).arg(port)}});
        return nullptr;
    }
    m_connections[key] = client;
    return client.get();
}

void PlcCraneHandler::handle(const QString& cmd, const QJsonValue& data,
        IResponder& resp) {
    QJsonObject p = data.toObject();

    if (cmd == "status") {
        resp.done(0, QJsonObject{{"status", "ready"}});
        return;
    }

    QString host = p["host"].toString();
    int port = p["port"].toInt(502);
    int unitId = p["unit_id"].toInt(1);
    int timeout = p["timeout"].toInt(3000);

    // Pre-validate action/mode enums before connecting
    if (cmd == "cylinder_control") {
        QString action = p["action"].toString();
        if (action != "up" && action != "down" && action != "stop") {
            resp.error(3, QJsonObject{{"message",
                QString("Invalid action: '%1', expected: up, down, stop").arg(action)}});
            return;
        }
    }
    else if (cmd == "valve_control") {
        QString action = p["action"].toString();
        if (action != "open" && action != "close" && action != "stop") {
            resp.error(3, QJsonObject{{"message",
                QString("Invalid action: '%1', expected: open, close, stop").arg(action)}});
            return;
        }
    }
    else if (cmd == "set_mode") {
        QString mode = p["mode"].toString();
        if (mode != "manual" && mode != "auto") {
            resp.error(3, QJsonObject{{"message",
                QString("Invalid mode: '%1', expected: manual, auto").arg(mode)}});
            return;
        }
    }
    else if (cmd == "set_run") {
        QString action = p["action"].toString();
        if (action != "start" && action != "stop") {
            resp.error(3, QJsonObject{{"message",
                QString("Invalid action: '%1', expected: start, stop").arg(action)}});
            return;
        }
    }

    auto* client = getClient(host, port, timeout, resp);
    if (!client) return;
    client->setUnitId(unitId);

    if (cmd == "read_status") {
        auto result = client->readDiscreteInputs(9, 6);
        if (!result.success) {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
            return;
        }
        resp.done(0, QJsonObject{
            {"cylinder_up", result.coils[0]},
            {"cylinder_down", result.coils[1]},
            {"valve_open", result.coils[4]},
            {"valve_closed", result.coils[5]}
        });
    }
    else if (cmd == "cylinder_control") {
        QString action = p["action"].toString();
        quint16 value = 0;
        if (action == "up") value = 1;
        else if (action == "down") value = 2;

        auto result = client->writeSingleRegister(0, value);
        if (!result.success) {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
            return;
        }
        resp.done(0, QJsonObject{{"written", true}, {"action", action}});
    }
    else if (cmd == "valve_control") {
        QString action = p["action"].toString();
        quint16 value = 0;
        if (action == "open") value = 1;
        else if (action == "close") value = 2;

        auto result = client->writeSingleRegister(1, value);
        if (!result.success) {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
            return;
        }
        resp.done(0, QJsonObject{{"written", true}, {"action", action}});
    }
    else if (cmd == "set_run") {
        QString action = p["action"].toString();
        quint16 value = (action == "start") ? 1 : 0;

        auto result = client->writeSingleRegister(2, value);
        if (!result.success) {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
            return;
        }
        resp.done(0, QJsonObject{{"written", true}, {"action", action}});
    }
    else if (cmd == "set_mode") {
        QString mode = p["mode"].toString();
        quint16 value = (mode == "auto") ? 1 : 0;

        auto result = client->writeSingleRegister(3, value);
        if (!result.success) {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
            return;
        }
        resp.done(0, QJsonObject{{"written", true}, {"mode", mode}});
    }
    else {
        resp.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
    }
}

static FieldBuilder connectionParam(const QString& name) {
    if (name == "host") {
        return FieldBuilder("host", FieldType::String)
            .required()
            .description("PLC IP 地址")
            .placeholder("192.168.1.1");
    }
    if (name == "port") {
        return FieldBuilder("port", FieldType::Int)
            .defaultValue(502)
            .range(1, 65535)
            .description("Modbus TCP 端口");
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

void PlcCraneHandler::buildMeta() {
    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("plc.crane", "PLC Crane Controller", "1.0.0",
              "PLC 升降装置 Modbus TCP 驱动，将寄存器地址映射为语义化命令")
        .vendor("stdiolink")
        .command(CommandBuilder("status")
            .description("获取驱动状态"))
        .command(CommandBuilder("read_status")
            .description("读取气缸和阀门状态")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout")))
        .command(CommandBuilder("cylinder_control")
            .description("气缸升降控制")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("action", FieldType::Enum)
                .required()
                .enumValues(QStringList{"up", "down", "stop"})
                .description("动作: up, down, stop")))
        .command(CommandBuilder("valve_control")
            .description("阀门开关控制")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("action", FieldType::Enum)
                .required()
                .enumValues(QStringList{"open", "close", "stop"})
                .description("动作: open, close, stop")))
        .command(CommandBuilder("set_run")
            .description("启停控制")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("action", FieldType::Enum)
                .required()
                .enumValues(QStringList{"start", "stop"})
                .description("动作: start, stop")))
        .command(CommandBuilder("set_mode")
            .description("模式切换")
            .param(connectionParam("host"))
            .param(connectionParam("port"))
            .param(connectionParam("unit_id"))
            .param(connectionParam("timeout"))
            .param(FieldBuilder("mode", FieldType::Enum)
                .required()
                .enumValues(QStringList{"manual", "auto"})
                .description("模式: manual, auto")))
        .build();
}
