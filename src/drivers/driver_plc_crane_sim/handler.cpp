#include "handler.h"

#include <QJsonObject>

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
                 .info("plc.crane.sim", "PLC汽动升降装置仿真", "1.0.0",
                       "PLC 升降装置仿真驱动，启动后模拟 Modbus TCP 从站，"
                       "自动维护气缸/阀门寄存器状态和到位延迟")
                 .vendor("stdiolink")
                 .profile("keepalive")
                 .command(CommandBuilder("run")
                              .description("启动仿真 Modbus TCP 从站服务，"
                                           "启动后持续运行并通过事件推送状态变化")
                              .param(FieldBuilder("listen_address", FieldType::String)
                                         .defaultValue("")
                                         .description("监听地址，空字符串表示所有网络接口（0.0.0.0）"))
                              .param(FieldBuilder("listen_port", FieldType::Int)
                                         .defaultValue(502)
                                         .range(1, 65535)
                                         .description("Modbus TCP 监听端口，默认 502"))
                              .param(FieldBuilder("unit_id", FieldType::Int)
                                         .defaultValue(1)
                                         .range(1, 247)
                                         .description("仿真从站地址（1-247），"
                                                      "仅响应此站号的 Modbus 请求"))
                              .param(FieldBuilder("event_mode", FieldType::Enum)
                                         .defaultValue("write")
                                         .enumValues(QStringList{"write", "all", "read", "none"})
                                         .description("事件推送模式：write=仅推送写操作 / "
                                                      "all=读写都推送 / read=仅推送读操作 / "
                                                      "none=关闭事件推送"))
                              .param(FieldBuilder("data_area_size", FieldType::Int)
                                         .defaultValue(256)
                                         .range(32, 65536)
                                         .description("保持寄存器区总大小，默认 256 个寄存器"))
                              .param(FieldBuilder("cylinder_up_delay", FieldType::Int)
                                         .defaultValue(2500)
                                         .range(0, 30000)
                                         .unit("ms")
                                         .description("气缸从下到上的模拟运动时间（毫秒），"
                                                      "到达后寄存器 9 置 1"))
                              .param(FieldBuilder("cylinder_down_delay", FieldType::Int)
                                         .defaultValue(2000)
                                         .range(0, 30000)
                                         .unit("ms")
                                         .description("气缸从上到下的模拟运动时间（毫秒），"
                                                      "到达后寄存器 10 置 1"))
                              .param(FieldBuilder("valve_open_delay", FieldType::Int)
                                         .defaultValue(1500)
                                         .range(0, 30000)
                                         .unit("ms")
                                         .description("球阀从关到开的模拟运动时间（毫秒），"
                                                      "到达后寄存器 13 置 1"))
                              .param(FieldBuilder("valve_close_delay", FieldType::Int)
                                         .defaultValue(1200)
                                         .range(0, 30000)
                                         .unit("ms")
                                         .description("球阀从开到关的模拟运动时间（毫秒），"
                                                      "到达后寄存器 14 置 1"))
                              .param(FieldBuilder("heartbeat_ms", FieldType::Int)
                                         .defaultValue(0)
                                         .range(0, 10000)
                                         .unit("ms")
                                         .description("运行心跳事件周期（毫秒），"
                                                      "0=关闭心跳推送"))
                              .event("started", "仿真启动完成，返回实际监听地址和端口")
                              .event("sim_heartbeat", "运行心跳事件，含 uptime_s")
                              .example("启动仿真从站", QStringList{"stdio", "console"},
                                       QJsonObject{{"listen_port", 502},
                                                   {"unit_id", 1},
                                                   {"event_mode", "write"}}))
                 .build();
}

void SimPlcCraneHandler::handleRun(const QJsonObject& data, stdiolink::IResponder& resp) {
    if (m_running || m_server.isRunning()) {
        resp.error(3, QJsonObject{{"message", "Server already running"}});
        return;
    }

    SimRunConfig cfg;
    cfg.listenAddress = data.value("listen_address").toString();
    cfg.listenPort = data.value("listen_port").toInt(502);
    cfg.unitId = static_cast<quint8>(data.value("unit_id").toInt(1));
    cfg.dataAreaSize = data.value("data_area_size").toInt(256);
    cfg.cylinderUpDelayMs = data.value("cylinder_up_delay").toInt(2500);
    cfg.cylinderDownDelayMs = data.value("cylinder_down_delay").toInt(2000);
    cfg.valveOpenDelayMs = data.value("valve_open_delay").toInt(1500);
    cfg.valveCloseDelayMs = data.value("valve_close_delay").toInt(1200);
    cfg.heartbeatMs = data.value("heartbeat_ms").toInt(0);
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
    if (m_cfg.heartbeatMs > 0) {
        m_heartbeatTimer.start();
    } else {
        m_heartbeatTimer.stop();
    }

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
#endif
