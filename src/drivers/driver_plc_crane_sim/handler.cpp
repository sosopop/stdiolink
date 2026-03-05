#include "handler.h"

#include <QJsonObject>
#include <QSet>

#include "stdiolink/driver/example_auto_fill.h"
#include "stdiolink/driver/meta_builder.h"

namespace {

using namespace stdiolink::meta;

constexpr quint16 kHrCylinder = 0;
constexpr quint16 kHrValve = 1;
constexpr quint16 kHrRun = 2;
constexpr quint16 kHrMode = 3;
constexpr quint16 kHrMaxAddress = kHrMode;

constexpr quint16 kDiCylinderUp = 9;
constexpr quint16 kDiCylinderDown = 10;
constexpr quint16 kDiValveOpen = 13;
constexpr quint16 kDiValveClosed = 14;

bool parseIntInRange(const QString& raw, int minValue, int maxValue,
                     const QString& key, int& outValue, QString* err) {
    bool ok = false;
    const int value = raw.toInt(&ok);
    if (!ok) {
        if (err != nullptr) {
            *err = QString("Invalid integer for --%1: %2").arg(key, raw);
        }
        return false;
    }
    if (value < minValue || value > maxValue) {
        if (err != nullptr) {
            *err = QString("--%1 out of range [%2,%3]: %4")
                       .arg(key)
                       .arg(minValue)
                       .arg(maxValue)
                       .arg(value);
        }
        return false;
    }
    outValue = value;
    return true;
}

SimPlcCraneDevice::Config toDeviceConfig(const SimDriverConfig& cfg) {
    SimPlcCraneDevice::Config dc;
    dc.cylinderUpDelayMs = cfg.cylinderUpDelayMs;
    dc.cylinderDownDelayMs = cfg.cylinderDownDelayMs;
    dc.valveOpenDelayMs = cfg.valveOpenDelayMs;
    dc.valveCloseDelayMs = cfg.valveCloseDelayMs;
    return dc;
}

} // namespace

bool parseSimDriverConfigArgs(int argc, char* argv[],
                              SimDriverConfig& cfg,
                              QString* errorMessage,
                              QStringList* passthroughArgs) {
    QStringList passthrough;
    if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
        passthrough.append(QString::fromUtf8(argv[0]));
    }

    enum class ArgMatchState {
        NotMatched,
        Matched,
        Error
    };

    auto takeValue = [&](const QString& arg, const QString& key, int& index, QString& out) -> ArgMatchState {
        const QString eqPrefix = "--" + key + "=";
        if (arg.startsWith(eqPrefix)) {
            out = arg.mid(eqPrefix.size());
            return ArgMatchState::Matched;
        }
        if (arg == "--" + key) {
            if (index + 1 >= argc) {
                if (errorMessage != nullptr) {
                    *errorMessage = QString("Missing value for argument: --%1").arg(key);
                }
                return ArgMatchState::Error;
            }
            ++index;
            out = QString::fromUtf8(argv[index]);
            return ArgMatchState::Matched;
        }
        return ArgMatchState::NotMatched;
    };

    for (int i = 1; i < argc; ++i) {
        const QString rawArg = QString::fromUtf8(argv[i]);
        if (!rawArg.startsWith("--")) {
            passthrough.append(rawArg);
            continue;
        }

        QString arg = rawArg;
        if (rawArg.startsWith("--arg-")) {
            arg = "--" + rawArg.mid(6);
        }

        QString value;
        const auto listenAddressMatch = takeValue(arg, "listen-address", i, value);
        if (listenAddressMatch == ArgMatchState::Error) {
            return false;
        }
        if (listenAddressMatch == ArgMatchState::Matched) {
            if (value.isEmpty()) {
                if (errorMessage != nullptr) {
                    *errorMessage = "--listen-address cannot be empty";
                }
                return false;
            }
            cfg.listenAddress = value;
            continue;
        }
        const auto listenPortMatch = takeValue(arg, "listen-port", i, value);
        if (listenPortMatch == ArgMatchState::Error) {
            return false;
        }
        if (listenPortMatch == ArgMatchState::Matched) {
            if (!parseIntInRange(value, 1, 65535, "listen-port", cfg.listenPort, errorMessage)) {
                return false;
            }
            continue;
        }
        const auto unitIdMatch = takeValue(arg, "unit-id", i, value);
        if (unitIdMatch == ArgMatchState::Error) {
            return false;
        }
        if (unitIdMatch == ArgMatchState::Matched) {
            int parsedUnitId = static_cast<int>(cfg.unitId);
            if (!parseIntInRange(value, 1, 247, "unit-id", parsedUnitId, errorMessage)) {
                return false;
            }
            cfg.unitId = static_cast<quint8>(parsedUnitId);
            continue;
        }
        const auto dataAreaMatch = takeValue(arg, "data-area-size", i, value);
        if (dataAreaMatch == ArgMatchState::Error) {
            return false;
        }
        if (dataAreaMatch == ArgMatchState::Matched) {
            if (!parseIntInRange(value, 32, 65536, "data-area-size", cfg.dataAreaSize, errorMessage)) {
                return false;
            }
            continue;
        }
        const auto cylinderUpMatch = takeValue(arg, "cylinder-up-delay", i, value);
        if (cylinderUpMatch == ArgMatchState::Error) {
            return false;
        }
        if (cylinderUpMatch == ArgMatchState::Matched) {
            if (!parseIntInRange(
                    value, 0, 30000, "cylinder-up-delay", cfg.cylinderUpDelayMs, errorMessage)) {
                return false;
            }
            continue;
        }
        const auto cylinderDownMatch = takeValue(arg, "cylinder-down-delay", i, value);
        if (cylinderDownMatch == ArgMatchState::Error) {
            return false;
        }
        if (cylinderDownMatch == ArgMatchState::Matched) {
            if (!parseIntInRange(value, 0, 30000, "cylinder-down-delay",
                                 cfg.cylinderDownDelayMs, errorMessage)) {
                return false;
            }
            continue;
        }
        const auto valveOpenMatch = takeValue(arg, "valve-open-delay", i, value);
        if (valveOpenMatch == ArgMatchState::Error) {
            return false;
        }
        if (valveOpenMatch == ArgMatchState::Matched) {
            if (!parseIntInRange(value, 0, 30000, "valve-open-delay",
                                 cfg.valveOpenDelayMs, errorMessage)) {
                return false;
            }
            continue;
        }
        const auto valveCloseMatch = takeValue(arg, "valve-close-delay", i, value);
        if (valveCloseMatch == ArgMatchState::Error) {
            return false;
        }
        if (valveCloseMatch == ArgMatchState::Matched) {
            if (!parseIntInRange(value, 0, 30000, "valve-close-delay",
                                 cfg.valveCloseDelayMs, errorMessage)) {
                return false;
            }
            continue;
        }
        const auto tickMatch = takeValue(arg, "tick-ms", i, value);
        if (tickMatch == ArgMatchState::Error) {
            return false;
        }
        if (tickMatch == ArgMatchState::Matched) {
            if (!parseIntInRange(value, 10, 1000, "tick-ms", cfg.tickMs, errorMessage)) {
                return false;
            }
            continue;
        }
        const auto heartbeatMatch = takeValue(arg, "heartbeat-ms", i, value);
        if (heartbeatMatch == ArgMatchState::Error) {
            return false;
        }
        if (heartbeatMatch == ArgMatchState::Matched) {
            if (!parseIntInRange(value, 100, 10000, "heartbeat-ms", cfg.heartbeatMs, errorMessage)) {
                return false;
            }
            continue;
        }
        const auto eventModeMatch = takeValue(arg, "event-mode", i, value);
        if (eventModeMatch == ArgMatchState::Error) {
            return false;
        }
        if (eventModeMatch == ArgMatchState::Matched) {
            cfg.eventMode = value.trimmed();
            continue;
        }

        passthrough.append(rawArg);
    }

    static const QSet<QString> validModes = {"write", "read", "all", "none"};
    if (!validModes.contains(cfg.eventMode)) {
        if (errorMessage != nullptr) {
            *errorMessage = QString(
                "Invalid --event-mode: %1, expected write/read/all/none").arg(cfg.eventMode);
        }
        return false;
    }

    if (passthroughArgs != nullptr) {
        *passthroughArgs = passthrough;
    }
    return true;
}

SimPlcCraneHandler::SimPlcCraneHandler(const SimDriverConfig& cfg)
    : m_cfg(cfg), m_device(toDeviceConfig(cfg)) {
    buildMeta();

    m_refreshTimer.setInterval(m_cfg.tickMs);
    QObject::connect(&m_refreshTimer, &QTimer::timeout, [this]() {
        syncDataAreaFromDevice();
    });

    m_heartbeatTimer.setInterval(m_cfg.heartbeatMs);
    QObject::connect(&m_heartbeatTimer, &QTimer::timeout, [this]() {
        const int uptimeSeconds = this->uptimeSeconds();
        m_eventResponder.event("sim_heartbeat", 0, QJsonObject{
            {"uptime_s", uptimeSeconds}
        });
    });

    QObject::connect(&m_server, &ModbusTcpServer::clientConnected,
                     [this](const QString& address, quint16 port) {
        m_eventResponder.event("client_connected", 0, QJsonObject{
            {"address", address},
            {"port", static_cast<int>(port)}
        });
    });

    QObject::connect(&m_server, &ModbusTcpServer::clientDisconnected,
                     [this](const QString& address, quint16 port) {
        m_eventResponder.event("client_disconnected", 0, QJsonObject{
            {"address", address},
            {"port", static_cast<int>(port)}
        });
    });

    QObject::connect(&m_server, &ModbusTcpServer::dataRead,
                     [this](quint8 unitId, quint8 functionCode, quint16 address, quint16 quantity) {
        if (!eventReadEnabled()) {
            return;
        }
        m_eventResponder.event("data_read", 0, QJsonObject{
            {"unit_id", static_cast<int>(unitId)},
            {"function_code", static_cast<int>(functionCode)},
            {"address", static_cast<int>(address)},
            {"quantity", static_cast<int>(quantity)}
        });
    });

    QObject::connect(&m_server, &ModbusTcpServer::dataWritten,
                     [this](quint8 unitId, quint8 functionCode, quint16 address, quint16 quantity) {
        onDataWritten(unitId, functionCode, address, quantity);
        if (!eventWriteEnabled()) {
            return;
        }
        m_eventResponder.event("data_written", 0, QJsonObject{
            {"unit_id", static_cast<int>(unitId)},
            {"function_code", static_cast<int>(functionCode)},
            {"address", static_cast<int>(address)},
            {"quantity", static_cast<int>(quantity)}
        });
    });
}

SimPlcCraneHandler::~SimPlcCraneHandler() {
    m_refreshTimer.stop();
    m_heartbeatTimer.stop();
    m_server.stopServer();
}

void SimPlcCraneHandler::handle(const QString& cmd, const QJsonValue& data, stdiolink::IResponder& resp) {
    Q_UNUSED(data);
    if (cmd != "run") {
        resp.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
        return;
    }
    handleRun(resp);
}

void SimPlcCraneHandler::buildMeta() {
    m_meta = DriverMetaBuilder()
                 .schemaVersion("1.0")
                 .info("plc.crane.sim", "PLC Crane Simulator", "1.0.0",
                       "PLC 升降装置仿真驱动，仅提供 run 命令，启动后通过 ModbusTCP 通讯")
                 .vendor("stdiolink")
                 .command(CommandBuilder("run")
                              .description("启动仿真 ModbusTCP 服务并进入持续事件输出")
                              .event("started", "启动完成事件")
                              .event("sim_heartbeat", "运行心跳事件"))
                 .build();
    ensureCommandExamples(m_meta);
}

void SimPlcCraneHandler::handleRun(stdiolink::IResponder& resp) {
    if (m_running || m_server.isRunning()) {
        resp.error(3, QJsonObject{{"message", "Server already running"}});
        return;
    }

    if (!m_server.startServer(static_cast<quint16>(m_cfg.listenPort), m_cfg.listenAddress)) {
        resp.error(1, QJsonObject{{"message",
            QString("Failed to listen on %1:%2: %3")
                .arg(m_cfg.listenAddress)
                .arg(m_cfg.listenPort)
                .arg(m_server.errorString())}});
        return;
    }

    if (!m_server.addUnit(m_cfg.unitId, m_cfg.dataAreaSize)) {
        m_server.stopServer();
        resp.error(1, QJsonObject{{"message",
            QString("Failed to add unit %1").arg(static_cast<int>(m_cfg.unitId))}});
        return;
    }

    m_running = true;
    m_uptimeClock.restart();
    syncDataAreaFromDevice();
    m_refreshTimer.start();
    m_heartbeatTimer.start();

    resp.event("started", 0, QJsonObject{
        {"listen_address", m_cfg.listenAddress},
        {"listen_port", static_cast<int>(m_server.serverPort())},
        {"unit_id", static_cast<int>(m_cfg.unitId)},
        {"event_mode", m_cfg.eventMode}
    });
}

void SimPlcCraneHandler::syncDataAreaFromDevice() {
    if (!m_running) {
        return;
    }
    const quint8 unitId = m_cfg.unitId;
    const QJsonObject snap = m_device.snapshot();

    m_server.setHoldingRegister(unitId, kHrCylinder, static_cast<quint16>(snap["hr_cylinder"].toInt()));
    m_server.setHoldingRegister(unitId, kHrValve, static_cast<quint16>(snap["hr_valve"].toInt()));
    m_server.setHoldingRegister(unitId, kHrRun, static_cast<quint16>(snap["hr_run"].toInt()));
    m_server.setHoldingRegister(unitId, kHrMode, static_cast<quint16>(snap["hr_mode"].toInt()));

    m_server.setDiscreteInput(unitId, kDiCylinderUp, snap["di_cylinder_up"].toBool());
    m_server.setDiscreteInput(unitId, kDiCylinderDown, snap["di_cylinder_down"].toBool());
    m_server.setDiscreteInput(unitId, kDiValveOpen, snap["di_valve_open"].toBool());
    m_server.setDiscreteInput(unitId, kDiValveClosed, snap["di_valve_closed"].toBool());
}

void SimPlcCraneHandler::onDataWritten(quint8 unitId, quint8 functionCode,
                                       quint16 address, quint16 quantity) {
    if (!m_running || unitId != m_cfg.unitId) {
        return;
    }
    // 仅处理写保持寄存器操作（FC 0x06 / 0x10）
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
            m_eventResponder.event("sim_write_rejected", 0, QJsonObject{
                {"address", static_cast<int>(cursor)},
                {"value", static_cast<int>(value)},
                {"message", QString("Unsupported holding register address: %1").arg(cursor)}
            });
            continue;
        }

        QString err;
        if (m_device.writeHoldingRegister(cursor, value, err)) {
            continue;
        }

        const quint16 canonical = m_device.holdingRegister(cursor);
        m_server.setHoldingRegister(unitId, cursor, canonical);
        m_eventResponder.event("sim_write_rejected", 0, QJsonObject{
            {"address", static_cast<int>(cursor)},
            {"value", static_cast<int>(value)},
            {"message", err}
        });
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
