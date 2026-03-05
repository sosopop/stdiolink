#include "handler.h"

#include <QJsonObject>

#include "stdiolink/driver/example_auto_fill.h"
#include "stdiolink/driver/meta_builder.h"

namespace {

using namespace stdiolink::meta;

constexpr quint16 kHrCylinder = 0;
constexpr quint16 kHrValve = 1;
constexpr quint16 kHrRun = 2;
constexpr quint16 kHrMode = 3;
constexpr quint16 kHrMaxAddress = kHrMode;
constexpr quint16 kHrCylinderUp = 9;
constexpr quint16 kHrCylinderDown = 10;
constexpr quint16 kHrValveOpen = 13;
constexpr quint16 kHrValveClosed = 14;

constexpr quint16 kDiCylinderUp = 9;
constexpr quint16 kDiCylinderDown = 10;
constexpr quint16 kDiValveOpen = 13;
constexpr quint16 kDiValveClosed = 14;

SimPlcCraneDevice::Config toDeviceConfig(const SimRunConfig& cfg) {
    SimPlcCraneDevice::Config dc;
    dc.cylinderUpDelayMs = cfg.cylinderUpDelayMs;
    dc.cylinderDownDelayMs = cfg.cylinderDownDelayMs;
    dc.valveOpenDelayMs = cfg.valveOpenDelayMs;
    dc.valveCloseDelayMs = cfg.valveCloseDelayMs;
    return dc;
}

} // namespace

SimPlcCraneHandler::SimPlcCraneHandler() : m_device(toDeviceConfig(m_cfg)) {
    buildMeta();

    m_refreshTimer.setInterval(m_cfg.tickMs);
    QObject::connect(&m_refreshTimer, &QTimer::timeout, [this]() { syncDataAreaFromDevice(); });

    m_heartbeatTimer.setInterval(m_cfg.heartbeatMs);
    QObject::connect(&m_heartbeatTimer, &QTimer::timeout, [this]() {
        const int uptimeSeconds = this->uptimeSeconds();
        m_eventResponder.event("sim_heartbeat", 0, QJsonObject{{"uptime_s", uptimeSeconds}});
    });

    QObject::connect(&m_server, &ModbusTcpServer::clientConnected,
                     [this](const QString& address, quint16 port) {
                         m_eventResponder.event(
                             "client_connected", 0,
                             QJsonObject{{"address", address}, {"port", static_cast<int>(port)}});
                     });

    QObject::connect(&m_server, &ModbusTcpServer::clientDisconnected,
                     [this](const QString& address, quint16 port) {
                         m_eventResponder.event(
                             "client_disconnected", 0,
                             QJsonObject{{"address", address}, {"port", static_cast<int>(port)}});
                     });

    QObject::connect(&m_server, &ModbusTcpServer::dataRead,
                     [this](quint8 unitId, quint8 functionCode, quint16 address, quint16 quantity) {
                         if (!eventReadEnabled()) {
                             return;
                         }
                         m_eventResponder.event(
                             "data_read", 0,
                             QJsonObject{{"unit_id", static_cast<int>(unitId)},
                                         {"function_code", static_cast<int>(functionCode)},
                                         {"address", static_cast<int>(address)},
                                         {"quantity", static_cast<int>(quantity)}});
                     });

    QObject::connect(&m_server, &ModbusTcpServer::dataWritten,
                     [this](quint8 unitId, quint8 functionCode, quint16 address, quint16 quantity) {
                         onDataWritten(unitId, functionCode, address, quantity);
                         if (!eventWriteEnabled()) {
                             return;
                         }
                         m_eventResponder.event(
                             "data_written", 0,
                             QJsonObject{{"unit_id", static_cast<int>(unitId)},
                                         {"function_code", static_cast<int>(functionCode)},
                                         {"address", static_cast<int>(address)},
                                         {"quantity", static_cast<int>(quantity)}});
                     });
}

SimPlcCraneHandler::~SimPlcCraneHandler() {
    m_refreshTimer.stop();
    m_heartbeatTimer.stop();
    m_server.stopServer();
}

void SimPlcCraneHandler::handle(const QString& cmd, const QJsonValue& data,
                                stdiolink::IResponder& resp) {
    QJsonObject p = data.toObject();
    if (cmd != "run") {
        resp.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
        return;
    }
    handleRun(p, resp);
}

void SimPlcCraneHandler::buildMeta() {
    m_meta = DriverMetaBuilder()
                 .schemaVersion("1.0")
                 .info("plc.crane.sim", "PLC Crane Simulator", "1.0.0",
                       "PLC 升降装置仿真驱动，仅提供 run 命令，启动后通过 ModbusTCP 通讯")
                 .vendor("stdiolink")
                 .profile("keepalive")
                 .command(CommandBuilder("run")
                              .description("一键启动从站服务（支持 OneShot 模式）")
                              .param(FieldBuilder("listen_address", FieldType::String)
                                         .defaultValue("")
                                         .description("监听地址（空=所有接口）"))
                              .param(FieldBuilder("listen_port", FieldType::Int)
                                         .defaultValue(1502)
                                         .range(1, 65535)
                                         .description("监听端口"))
                              .param(FieldBuilder("unit_id", FieldType::Int)
                                         .defaultValue(1)
                                         .range(1, 247)
                                         .description("Modbus 从站地址"))
                              .param(FieldBuilder("event_mode", FieldType::Enum)
                                         .defaultValue("write")
                                         .enumValues(QStringList{"write", "all", "read", "none"})
                                         .description("事件推送模式"))
                              .param(FieldBuilder("data_area_size", FieldType::Int)
                                         .defaultValue(256)
                                         .range(32, 65536)
                                         .description("寄存器区大小"))
                              .param(FieldBuilder("cylinder_up_delay", FieldType::Int)
                                         .defaultValue(2500)
                                         .range(0, 30000)
                                         .description("气缸上升延迟(ms)"))
                              .param(FieldBuilder("cylinder_down_delay", FieldType::Int)
                                         .defaultValue(2000)
                                         .range(0, 30000)
                                         .description("气缸下降延迟(ms)"))
                              .param(FieldBuilder("valve_open_delay", FieldType::Int)
                                         .defaultValue(1500)
                                         .range(0, 30000)
                                         .description("阀门打开延迟(ms)"))
                              .param(FieldBuilder("valve_close_delay", FieldType::Int)
                                         .defaultValue(1200)
                                         .range(0, 30000)
                                         .description("阀门关闭延迟(ms)"))
                              .param(FieldBuilder("tick_ms", FieldType::Int)
                                         .defaultValue(50)
                                         .range(10, 1000)
                                         .description("状态刷新周期(ms)"))
                              .param(FieldBuilder("heartbeat_ms", FieldType::Int)
                                         .defaultValue(1000)
                                         .range(100, 10000)
                                         .description("心跳事件周期(ms)"))
                              .event("started", "启动完成事件")
                              .event("sim_heartbeat", "运行心跳事件"))
                 .build();
    ensureCommandExamples(m_meta);
}

void SimPlcCraneHandler::handleRun(const QJsonObject& data, stdiolink::IResponder& resp) {
    if (m_running || m_server.isRunning()) {
        resp.error(3, QJsonObject{{"message", "Server already running"}});
        return;
    }

    SimRunConfig cfg;
    cfg.listenAddress = data.value("listen_address").toString();
    cfg.listenPort = data.value("listen_port").toInt(1502);
    cfg.unitId = static_cast<quint8>(data.value("unit_id").toInt(1));
    cfg.dataAreaSize = data.value("data_area_size").toInt(256);
    cfg.cylinderUpDelayMs = data.value("cylinder_up_delay").toInt(2500);
    cfg.cylinderDownDelayMs = data.value("cylinder_down_delay").toInt(2000);
    cfg.valveOpenDelayMs = data.value("valve_open_delay").toInt(1500);
    cfg.valveCloseDelayMs = data.value("valve_close_delay").toInt(1200);
    cfg.tickMs = data.value("tick_ms").toInt(50);
    cfg.heartbeatMs = data.value("heartbeat_ms").toInt(1000);
    cfg.eventMode = data.value("event_mode").toString("write").trimmed();

    m_cfg = cfg;
    m_device.setConfig(toDeviceConfig(m_cfg));
    m_refreshTimer.setInterval(m_cfg.tickMs);
    m_heartbeatTimer.setInterval(m_cfg.heartbeatMs);

    if (!m_server.startServer(static_cast<quint16>(m_cfg.listenPort), m_cfg.listenAddress)) {
        resp.error(
            1, QJsonObject{{"message", QString("Failed to listen on %1:%2: %3")
                                           .arg(m_cfg.listenAddress.isEmpty() ? "0.0.0.0"
                                                                              : m_cfg.listenAddress)
                                           .arg(m_cfg.listenPort)
                                           .arg(m_server.errorString())}});
        return;
    }

    if (!m_server.addUnit(m_cfg.unitId, m_cfg.dataAreaSize)) {
        m_server.stopServer();
        resp.error(
            1, QJsonObject{{"message",
                            QString("Failed to add unit %1").arg(static_cast<int>(m_cfg.unitId))}});
        return;
    }

    m_running = true;
    m_uptimeClock.restart();
    syncDataAreaFromDevice();
    m_refreshTimer.start();
    m_heartbeatTimer.start();

    m_eventResponder.event("started", 0,
                           QJsonObject{{"listen_address", m_cfg.listenAddress},
                                       {"listen_port", static_cast<int>(m_server.serverPort())},
                                       {"unit_id", static_cast<int>(m_cfg.unitId)},
                                       {"event_mode", m_cfg.eventMode}});
}

void SimPlcCraneHandler::syncDataAreaFromDevice() {
    if (!m_running) {
        return;
    }
    const quint8 unitId = m_cfg.unitId;
    const QJsonObject snap = m_device.snapshot();

    m_server.setHoldingRegister(unitId, kHrCylinder,
                                static_cast<quint16>(snap["hr_cylinder"].toInt()));
    m_server.setHoldingRegister(unitId, kHrValve, static_cast<quint16>(snap["hr_valve"].toInt()));
    m_server.setHoldingRegister(unitId, kHrRun, static_cast<quint16>(snap["hr_run"].toInt()));
    m_server.setHoldingRegister(unitId, kHrMode, static_cast<quint16>(snap["hr_mode"].toInt()));
    m_server.setHoldingRegister(unitId, kHrCylinderUp, snap["di_cylinder_up"].toBool() ? 1 : 0);
    m_server.setHoldingRegister(unitId, kHrCylinderDown, snap["di_cylinder_down"].toBool() ? 1 : 0);
    m_server.setHoldingRegister(unitId, kHrValveOpen, snap["di_valve_open"].toBool() ? 1 : 0);
    m_server.setHoldingRegister(unitId, kHrValveClosed, snap["di_valve_closed"].toBool() ? 1 : 0);

    m_server.setDiscreteInput(unitId, kDiCylinderUp, snap["di_cylinder_up"].toBool());
    m_server.setDiscreteInput(unitId, kDiCylinderDown, snap["di_cylinder_down"].toBool());
    m_server.setDiscreteInput(unitId, kDiValveOpen, snap["di_valve_open"].toBool());
    m_server.setDiscreteInput(unitId, kDiValveClosed, snap["di_valve_closed"].toBool());
}

void SimPlcCraneHandler::onDataWritten(quint8 unitId, quint8 functionCode, quint16 address,
                                       quint16 quantity) {
    if (!m_running || unitId != m_cfg.unitId) {
        return;
    }
    if (functionCode != 0x06 && functionCode != 0x10) {
        return;
    }

    const quint16 endAddress = static_cast<quint16>(address + quantity);
    for (quint16 cursor = address; cursor < endAddress; ++cursor) {
        quint16 value = 0;
        if (!m_server.getHoldingRegister(unitId, cursor, value)) {
            continue;
        }
        if (cursor > kHrMaxAddress) {
            m_eventResponder.event(
                "sim_write_rejected", 0,
                QJsonObject{
                    {"address", static_cast<int>(cursor)},
                    {"value", static_cast<int>(value)},
                    {"message", QString("Unsupported holding register address: %1").arg(cursor)}});
            continue;
        }

        QString err;
        if (m_device.writeHoldingRegister(cursor, value, err)) {
            continue;
        }

        const quint16 canonical = m_device.holdingRegister(cursor);
        m_server.setHoldingRegister(unitId, cursor, canonical);
        m_eventResponder.event("sim_write_rejected", 0,
                               QJsonObject{{"address", static_cast<int>(cursor)},
                                           {"value", static_cast<int>(value)},
                                           {"message", err}});
    }

    syncDataAreaFromDevice();
}

bool SimPlcCraneHandler::eventReadEnabled() const {
    return m_cfg.eventMode == "all" || m_cfg.eventMode == "read";
}

bool SimPlcCraneHandler::eventWriteEnabled() const {
    return m_cfg.eventMode == "all" || m_cfg.eventMode == "write";
}

int SimPlcCraneHandler::uptimeSeconds() const {
    if (!m_uptimeClock.isValid()) {
        return 0;
    }
    return static_cast<int>(m_uptimeClock.elapsed() / 1000);
}

#ifdef STDIOLINK_TESTING
bool SimPlcCraneHandler::writeHoldingRegisterForTest(quint16 address, quint16 value, QString& err) {
    if (!m_running) {
        err = "server not running";
        return false;
    }
    const quint8 unitId = m_cfg.unitId;
    if (!m_server.setHoldingRegister(unitId, address, value)) {
        err = QString("setHoldingRegister failed at address %1").arg(address);
        return false;
    }
    onDataWritten(unitId, 0x06, address, 1);
    return true;
}

bool SimPlcCraneHandler::readHoldingRegisterForTest(quint16 address, quint16& value) {
    return m_server.getHoldingRegister(m_cfg.unitId, address, value);
}

bool SimPlcCraneHandler::readDiscreteInputForTest(quint16 address, bool& value) {
    return m_server.getDiscreteInput(m_cfg.unitId, address, value);
}
#endif
