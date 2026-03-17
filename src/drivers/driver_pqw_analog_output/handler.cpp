#include "driver_pqw_analog_output/handler.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <memory>

#include "driver_modbusrtu_serial/modbus_rtu_serial_client.h"
#include "stdiolink/driver/example_auto_fill.h"
#include "stdiolink/driver/meta_builder.h"

using namespace stdiolink;
using namespace stdiolink::meta;

namespace {

constexpr quint16 kRegUnitId = 0x0000;
constexpr quint16 kRegBaudRate = 0x0001;
constexpr quint16 kRegCommWatchdog = 0x0003;
constexpr quint16 kRegParity = 0x0004;
constexpr quint16 kRegStopBits = 0x0005;
constexpr quint16 kRegRestoreDefaults = 0x000D;
constexpr quint16 kRegOutputBase = 0x0064;
constexpr quint16 kRegClearOutputs = 0x0076;
constexpr int kMaxChannels = 18;

struct ConfigReadResult {
    int unitId = 1;
    int baudRate = 9600;
    QString parity = "none";
    QString stopBits = "1";
    bool commWatchdogEnabled = false;
    int commWatchdogMs = 0;
    int commWatchdogRaw = 0;
    QJsonArray rawRegisters;
};

struct SerialConnectionInfo {
    std::shared_ptr<ModbusRtuSerialClient> client;
    int baudRate = 9600;
    QString stopBits = "1";
    QString parity = "none";
};

class SerialConnectionManager {
public:
    static SerialConnectionManager& instance() {
        static SerialConnectionManager manager;
        return manager;
    }

    ModbusRtuSerialClient* getClient(const PqwAnalogOutputConnectionOptions& options, QString& errorMessage) {
        auto it = m_connections.find(options.portName);
        if (it != m_connections.end()) {
            const SerialConnectionInfo& info = it.value();
            if (info.client->isOpen()) {
                if (info.baudRate != options.baudRate
                    || info.stopBits != options.stopBits
                    || info.parity != options.parity) {
                    errorMessage = QString(
                        "Serial port %1 is already open with different parameters")
                        .arg(options.portName);
                    return nullptr;
                }
                return info.client.get();
            }
            m_connections.erase(it);
        }

        auto client = std::make_shared<ModbusRtuSerialClient>();
        if (!client->open(options.portName, options.baudRate, 8, options.stopBits, options.parity)) {
            errorMessage = QString("Failed to open serial port %1").arg(options.portName);
            return nullptr;
        }

        SerialConnectionInfo info;
        info.client = client;
        info.baudRate = options.baudRate;
        info.stopBits = options.stopBits;
        info.parity = options.parity;
        m_connections.insert(options.portName, info);
        return client.get();
    }

private:
    QHash<QString, SerialConnectionInfo> m_connections;
};

QJsonArray toIntArray(const QVector<quint16>& values) {
    QJsonArray array;
    for (quint16 value : values) {
        array.append(static_cast<int>(value));
    }
    return array;
}

QJsonArray outputArray(int startChannel, const QVector<quint16>& registers) {
    QJsonArray array;
    for (int i = 0; i < registers.size(); ++i) {
        const quint16 rawValue = registers[i];
        array.append(QJsonObject{
            {"channel", startChannel + i},
            {"raw_value", static_cast<int>(rawValue)},
            {"value", static_cast<double>(rawValue) / 1000.0}
        });
    }
    return array;
}

QJsonArray writeItemsArray(const QVector<PqwAnalogOutputConfigWrite>& items) {
    QJsonArray array;
    for (const auto& item : items) {
        array.append(QJsonObject{
            {"field", item.field},
            {"register", static_cast<int>(item.reg)},
            {"raw_value", static_cast<int>(item.rawValue)},
            {"value", item.value}
        });
    }
    return array;
}

FieldBuilder connectionParam(const QString& name) {
    if (name == "port_name") {
        return FieldBuilder("port_name", FieldType::String)
            .required()
            .description("串口名称，例如 COM3")
            .placeholder("COM3");
    }
    if (name == "baud_rate") {
        return FieldBuilder("baud_rate", FieldType::Int)
            .defaultValue(9600)
            .enumValues(QStringList{"4800", "9600", "14400", "19200", "38400", "56000", "57600", "115200"})
            .description("串口波特率");
    }
    if (name == "parity") {
        return FieldBuilder("parity", FieldType::Enum)
            .defaultValue("none")
            .enumValues(QStringList{"none", "odd", "even"})
            .description("串口校验位");
    }
    if (name == "stop_bits") {
        return FieldBuilder("stop_bits", FieldType::Enum)
            .defaultValue("1")
            .enumValues(QStringList{"1", "2"})
            .description("串口停止位");
    }
    if (name == "unit_id") {
        return FieldBuilder("unit_id", FieldType::Int)
            .defaultValue(1)
            .range(1, 255)
            .description("设备站号");
    }
    return FieldBuilder("timeout", FieldType::Int)
        .defaultValue(2000)
        .range(1, 30000)
        .unit("ms")
        .description("单次读写超时时间");
}

void addConnectionParams(CommandBuilder& command) {
    command
        .param(connectionParam("port_name"))
        .param(connectionParam("baud_rate"))
        .param(connectionParam("parity"))
        .param(connectionParam("stop_bits"))
        .param(connectionParam("unit_id"))
        .param(connectionParam("timeout"));
}

void respondInvalidParam(IResponder& responder, const QString& message) {
    responder.error(3, QJsonObject{{"message", message}});
}

void respondIoError(IResponder& responder, const QString& message) {
    responder.error(1, QJsonObject{{"message", message}});
}

void respondProtocolError(IResponder& responder, const QString& message) {
    responder.error(2, QJsonObject{{"message", message}});
}

bool parseDoubleValue(const QJsonValue& input, double& value) {
    if (input.isDouble()) {
        value = input.toDouble();
        return true;
    }
    if (input.isString()) {
        bool ok = false;
        const double parsed = input.toString().toDouble(&ok);
        if (ok) {
            value = parsed;
            return true;
        }
    }
    return false;
}

bool isAllowedBaudRate(int baudRate) {
    static const QSet<int> allowed{4800, 9600, 14400, 19200, 38400, 56000, 57600, 115200};
    return allowed.contains(baudRate);
}

bool tryGetClient(const QString& cmd,
                  const QJsonObject& params,
                  PqwAnalogOutputConnectionOptions& options,
                  ModbusRtuSerialClient*& client,
                  IResponder& responder) {
    QString errorMessage;
    if (!PqwAnalogOutputHandler::resolveConnectionOptions(cmd, params, options, &errorMessage)) {
        respondInvalidParam(responder, errorMessage);
        return false;
    }

    client = SerialConnectionManager::instance().getClient(options, errorMessage);
    if (!client) {
        respondIoError(responder, errorMessage);
        return false;
    }
    return true;
}

bool readHoldingRegisters(ModbusRtuSerialClient* client,
                          const PqwAnalogOutputConnectionOptions& options,
                          quint16 address,
                          quint16 count,
                          QVector<quint16>& registers,
                          IResponder& responder) {
    const SerialModbusResult result = client->readHoldingRegisters(
        static_cast<quint8>(options.unitId),
        address,
        count,
        options.timeoutMs);
    if (!result.success) {
        respondProtocolError(responder, result.errorMessage);
        return false;
    }
    registers = result.registers;
    return true;
}

bool writeSingleRegister(ModbusRtuSerialClient* client,
                         const PqwAnalogOutputConnectionOptions& options,
                         quint16 address,
                         quint16 value,
                         IResponder& responder) {
    const SerialModbusResult result = client->writeSingleRegister(
        static_cast<quint8>(options.unitId),
        address,
        value,
        options.timeoutMs);
    if (!result.success) {
        respondProtocolError(responder, result.errorMessage);
        return false;
    }
    return true;
}

bool writeMultipleRegisters(ModbusRtuSerialClient* client,
                            const PqwAnalogOutputConnectionOptions& options,
                            quint16 address,
                            const QVector<quint16>& values,
                            IResponder& responder) {
    const SerialModbusResult result = client->writeMultipleRegisters(
        static_cast<quint8>(options.unitId),
        address,
        values,
        options.timeoutMs);
    if (!result.success) {
        respondProtocolError(responder, result.errorMessage);
        return false;
    }
    return true;
}

int baudCodeToValue(quint16 code) {
    switch (code) {
    case 0: return 4800;
    case 1: return 9600;
    case 2: return 14400;
    case 3: return 19200;
    case 4: return 38400;
    case 5: return 56000;
    case 6: return 57600;
    case 7: return 115200;
    default: return -1;
    }
}

bool baudValueToCode(int value, quint16& code) {
    switch (value) {
    case 4800: code = 0; return true;
    case 9600: code = 1; return true;
    case 14400: code = 2; return true;
    case 19200: code = 3; return true;
    case 38400: code = 4; return true;
    case 56000: code = 5; return true;
    case 57600: code = 6; return true;
    case 115200: code = 7; return true;
    default: return false;
    }
}

QString parityCodeToValue(quint16 code) {
    switch (code) {
    case 0: return "none";
    case 1: return "odd";
    case 2: return "even";
    default: return QString();
    }
}

bool parityValueToCode(const QString& value, quint16& code) {
    if (value == "none") {
        code = 0;
        return true;
    }
    if (value == "odd") {
        code = 1;
        return true;
    }
    if (value == "even") {
        code = 2;
        return true;
    }
    return false;
}

QString stopBitsCodeToValue(quint16 code) {
    switch (code) {
    case 0: return "1";
    case 1: return "2";
    default: return QString();
    }
}

bool stopBitsValueToCode(const QString& value, quint16& code) {
    if (value == "1") {
        code = 0;
        return true;
    }
    if (value == "2") {
        code = 1;
        return true;
    }
    return false;
}

bool decodeConfigRegisters(const QVector<quint16>& registers,
                           ConfigReadResult& result,
                           QString& errorMessage) {
    if (registers.size() < 6) {
        errorMessage = "Config register count must be 6";
        return false;
    }

    const int baudRate = baudCodeToValue(registers[1]);
    if (baudRate <= 0) {
        errorMessage = QString("Unsupported baud rate code: %1").arg(registers[1]);
        return false;
    }

    const QString parity = parityCodeToValue(registers[4]);
    if (parity.isEmpty()) {
        errorMessage = QString("Unsupported parity code: %1").arg(registers[4]);
        return false;
    }

    const QString stopBits = stopBitsCodeToValue(registers[5]);
    if (stopBits.isEmpty()) {
        errorMessage = QString("Unsupported stop bits code: %1").arg(registers[5]);
        return false;
    }

    result.unitId = static_cast<int>(registers[0]);
    result.baudRate = baudRate;
    result.parity = parity;
    result.stopBits = stopBits;
    result.commWatchdogRaw = static_cast<int>(registers[3]);
    result.commWatchdogEnabled = registers[3] > 0;
    result.commWatchdogMs = PqwAnalogOutputHandler::commWatchdogRawToMs(registers[3]);
    result.rawRegisters = toIntArray(registers);
    return true;
}

} // namespace

PqwAnalogOutputHandler::PqwAnalogOutputHandler() {
    buildMeta();
}

bool PqwAnalogOutputHandler::engineeringValueToRaw(double value,
                                                   quint16& rawValue,
                                                   QString* errorMessage) {
    const qint64 encoded = qRound64(value * 1000.0);
    if (encoded < 0 || encoded > 65535) {
        if (errorMessage) {
            *errorMessage = "value must encode to raw register range 0-65535";
        }
        return false;
    }
    rawValue = static_cast<quint16>(encoded);
    return true;
}

bool PqwAnalogOutputHandler::commWatchdogMsToRaw(int watchdogMs,
                                                 quint16& rawValue,
                                                 QString* errorMessage) {
    if (watchdogMs < 0) {
        if (errorMessage) {
            *errorMessage = "comm_watchdog_ms must be >= 0";
        }
        return false;
    }
    if (watchdogMs == 0) {
        rawValue = 0;
        return true;
    }
    if (watchdogMs % 10 != 0) {
        if (errorMessage) {
            *errorMessage = "comm_watchdog_ms must be 0 or a multiple of 10 ms";
        }
        return false;
    }

    const qint64 encoded = watchdogMs / 10 + 1;
    if (encoded > 65535) {
        if (errorMessage) {
            *errorMessage = "comm_watchdog_ms exceeds register range";
        }
        return false;
    }
    rawValue = static_cast<quint16>(encoded);
    return true;
}

int PqwAnalogOutputHandler::commWatchdogRawToMs(quint16 rawValue) {
    if (rawValue == 0) {
        return 0;
    }
    return static_cast<int>(rawValue - 1) * 10;
}

bool PqwAnalogOutputHandler::resolveConnectionOptions(
    const QString& cmd,
    const QJsonObject& params,
    PqwAnalogOutputConnectionOptions& options,
    QString* errorMessage) {
    options.portName = params.value("port_name").toString().trimmed();
    if (options.portName.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "port_name is required";
        }
        return false;
    }

    const bool isSetCommConfig = (cmd == "set_comm_config");
    const QString baudKey = isSetCommConfig ? "current_baud_rate" : "baud_rate";
    const QString parityKey = isSetCommConfig ? "current_parity" : "parity";
    const QString stopBitsKey = isSetCommConfig ? "current_stop_bits" : "stop_bits";
    const QString unitIdKey = isSetCommConfig ? "current_unit_id" : "unit_id";

    options.baudRate = params.value(baudKey).toInt(9600);
    if (!isAllowedBaudRate(options.baudRate)) {
        if (errorMessage) {
            *errorMessage = baudKey + " must be one of 4800, 9600, 14400, 19200, 38400, 56000, 57600, 115200";
        }
        return false;
    }

    options.parity = params.value(parityKey).toString("none").trimmed().toLower();
    if (options.parity != "none" && options.parity != "odd" && options.parity != "even") {
        if (errorMessage) {
            *errorMessage = parityKey + " must be one of none, odd, even";
        }
        return false;
    }

    options.stopBits = params.value(stopBitsKey).toString("1").trimmed();
    if (options.stopBits != "1" && options.stopBits != "2") {
        if (errorMessage) {
            *errorMessage = stopBitsKey + " must be 1 or 2";
        }
        return false;
    }

    options.unitId = params.value(unitIdKey).toInt(1);
    if (options.unitId < 1 || options.unitId > 255) {
        if (errorMessage) {
            *errorMessage = unitIdKey + " must be 1-255";
        }
        return false;
    }

    options.timeoutMs = params.value("timeout").toInt(2000);
    if (options.timeoutMs < 1 || options.timeoutMs > 30000) {
        if (errorMessage) {
            *errorMessage = "timeout must be 1-30000 ms";
        }
        return false;
    }

    return true;
}

bool PqwAnalogOutputHandler::buildCommConfigWrites(
    const QJsonObject& params,
    QVector<PqwAnalogOutputConfigWrite>& writes,
    QString* errorMessage) {
    writes.clear();

    if (params.contains("unit_id")) {
        const int unitId = params.value("unit_id").toInt(-1);
        if (unitId < 1 || unitId > 255) {
            if (errorMessage) {
                *errorMessage = "unit_id must be 1-255";
            }
            return false;
        }
        writes.append(PqwAnalogOutputConfigWrite{
            "unit_id", kRegUnitId, static_cast<quint16>(unitId), unitId});
    }

    if (params.contains("baud_rate")) {
        const int baudRate = params.value("baud_rate").toInt(-1);
        quint16 baudCode = 0;
        if (!baudValueToCode(baudRate, baudCode)) {
            if (errorMessage) {
                *errorMessage = "baud_rate must be one of 4800, 9600, 14400, 19200, 38400, 56000, 57600, 115200";
            }
            return false;
        }
        writes.append(PqwAnalogOutputConfigWrite{"baud_rate", kRegBaudRate, baudCode, baudRate});
    }

    if (params.contains("parity")) {
        const QString parity = params.value("parity").toString().trimmed().toLower();
        quint16 parityCode = 0;
        if (!parityValueToCode(parity, parityCode)) {
            if (errorMessage) {
                *errorMessage = "parity must be one of none, odd, even";
            }
            return false;
        }
        writes.append(PqwAnalogOutputConfigWrite{"parity", kRegParity, parityCode, parity});
    }

    if (params.contains("stop_bits")) {
        const QString stopBits = params.value("stop_bits").toString().trimmed();
        quint16 stopBitsCode = 0;
        if (!stopBitsValueToCode(stopBits, stopBitsCode)) {
            if (errorMessage) {
                *errorMessage = "stop_bits must be 1 or 2";
            }
            return false;
        }
        writes.append(PqwAnalogOutputConfigWrite{"stop_bits", kRegStopBits, stopBitsCode, stopBits});
    }

    if (params.contains("comm_watchdog_ms")) {
        const int watchdogMs = params.value("comm_watchdog_ms").toInt(-1);
        quint16 rawValue = 0;
        QString watchdogError;
        if (!commWatchdogMsToRaw(watchdogMs, rawValue, &watchdogError)) {
            if (errorMessage) {
                *errorMessage = watchdogError;
            }
            return false;
        }
        writes.append(PqwAnalogOutputConfigWrite{
            "comm_watchdog_ms", kRegCommWatchdog, rawValue, watchdogMs});
    }

    if (writes.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "set_comm_config requires at least one target config field";
        }
        return false;
    }

    return true;
}

void PqwAnalogOutputHandler::handle(const QString& cmd,
                                    const QJsonValue& data,
                                    IResponder& responder) {
    if (cmd == "status") {
        responder.done(0, QJsonObject{{"status", "ready"}});
        return;
    }

    const QJsonObject params = data.toObject();
    PqwAnalogOutputConnectionOptions options;
    ModbusRtuSerialClient* client = nullptr;

    if (cmd == "get_config") {
        if (!tryGetClient(cmd, params, options, client, responder)) {
            return;
        }
        QVector<quint16> registers;
        if (!readHoldingRegisters(client, options, kRegUnitId, 6, registers, responder)) {
            return;
        }

        ConfigReadResult config;
        QString errorMessage;
        if (!decodeConfigRegisters(registers, config, errorMessage)) {
            respondProtocolError(responder, errorMessage);
            return;
        }

        responder.done(0, QJsonObject{
            {"unit_id", config.unitId},
            {"baud_rate", config.baudRate},
            {"parity", config.parity},
            {"stop_bits", config.stopBits},
            {"comm_watchdog_enabled", config.commWatchdogEnabled},
            {"comm_watchdog_ms", config.commWatchdogMs},
            {"comm_watchdog_raw", config.commWatchdogRaw},
            {"raw_registers", config.rawRegisters}
        });
        return;
    }

    if (cmd == "set_comm_config") {
        QVector<PqwAnalogOutputConfigWrite> writes;
        QString errorMessage;
        if (!buildCommConfigWrites(params, writes, &errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }

        if (!tryGetClient(cmd, params, options, client, responder)) {
            return;
        }
        for (const auto& item : writes) {
            if (!writeSingleRegister(client, options, item.reg, item.rawValue, responder)) {
                return;
            }
        }

        responder.done(0, QJsonObject{
            {"writes", writeItemsArray(writes)},
            {"reboot_required", true}
        });
        return;
    }

    if (cmd == "restore_defaults") {
        if (!tryGetClient(cmd, params, options, client, responder)) {
            return;
        }
        if (!writeSingleRegister(client, options, kRegRestoreDefaults, 1, responder)) {
            return;
        }
        responder.done(0, QJsonObject{{"reboot_required", true}});
        return;
    }

    if (cmd == "read_outputs") {
        const int startChannel = params.value("start_channel").toInt(1);
        const int count = params.value("count").toInt(kMaxChannels);
        if (startChannel < 1 || startChannel > kMaxChannels) {
            respondInvalidParam(responder, "start_channel must be 1-18");
            return;
        }
        if (count < 1) {
            respondInvalidParam(responder, "count must be >= 1");
            return;
        }
        if (startChannel + count - 1 > kMaxChannels) {
            respondInvalidParam(responder, "requested channel range exceeds 18 channels");
            return;
        }

        if (!tryGetClient(cmd, params, options, client, responder)) {
            return;
        }
        QVector<quint16> registers;
        if (!readHoldingRegisters(client,
                                  options,
                                  static_cast<quint16>(kRegOutputBase + startChannel - 1),
                                  static_cast<quint16>(count),
                                  registers,
                                  responder)) {
            return;
        }

        responder.done(0, QJsonObject{
            {"start_channel", startChannel},
            {"count", count},
            {"outputs", outputArray(startChannel, registers)}
        });
        return;
    }

    if (cmd == "write_output") {
        const int channel = params.value("channel").toInt(0);
        if (channel < 1 || channel > kMaxChannels) {
            respondInvalidParam(responder, "channel must be 1-18");
            return;
        }

        double engineeringValue = 0.0;
        if (!parseDoubleValue(params.value("value"), engineeringValue)) {
            respondInvalidParam(responder, "value must be numeric");
            return;
        }

        quint16 rawValue = 0;
        QString errorMessage;
        if (!engineeringValueToRaw(engineeringValue, rawValue, &errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }

        if (!tryGetClient(cmd, params, options, client, responder)) {
            return;
        }
        if (!writeSingleRegister(client,
                                 options,
                                 static_cast<quint16>(kRegOutputBase + channel - 1),
                                 rawValue,
                                 responder)) {
            return;
        }

        responder.done(0, QJsonObject{
            {"channel", channel},
            {"value", engineeringValue},
            {"raw_value", static_cast<int>(rawValue)}
        });
        return;
    }

    if (cmd == "write_outputs") {
        const int startChannel = params.value("start_channel").toInt(1);
        if (startChannel < 1 || startChannel > kMaxChannels) {
            respondInvalidParam(responder, "start_channel must be 1-18");
            return;
        }

        const QJsonArray values = params.value("values").toArray();
        if (values.isEmpty()) {
            respondInvalidParam(responder, "values must not be empty");
            return;
        }
        if (startChannel + values.size() - 1 > kMaxChannels) {
            respondInvalidParam(responder, "requested channel range exceeds 18 channels");
            return;
        }

        QVector<quint16> rawValues;
        QJsonArray engineeringValues;
        rawValues.reserve(values.size());
        for (int i = 0; i < values.size(); ++i) {
            double engineeringValue = 0.0;
            if (!parseDoubleValue(values[i], engineeringValue)) {
                respondInvalidParam(responder, QString("values[%1] must be numeric").arg(i));
                return;
            }

            quint16 rawValue = 0;
            QString errorMessage;
            if (!engineeringValueToRaw(engineeringValue, rawValue, &errorMessage)) {
                respondInvalidParam(responder, QString("values[%1]: %2").arg(i).arg(errorMessage));
                return;
            }
            engineeringValues.append(engineeringValue);
            rawValues.append(rawValue);
        }

        if (!tryGetClient(cmd, params, options, client, responder)) {
            return;
        }
        if (!writeMultipleRegisters(client,
                                    options,
                                    static_cast<quint16>(kRegOutputBase + startChannel - 1),
                                    rawValues,
                                    responder)) {
            return;
        }

        responder.done(0, QJsonObject{
            {"start_channel", startChannel},
            {"count", values.size()},
            {"values", engineeringValues},
            {"raw_values", toIntArray(rawValues)}
        });
        return;
    }

    if (cmd == "clear_outputs") {
        if (!tryGetClient(cmd, params, options, client, responder)) {
            return;
        }
        if (!writeSingleRegister(client, options, kRegClearOutputs, 1, responder)) {
            return;
        }
        responder.done(0, QJsonObject{{"cleared", true}});
        return;
    }

    responder.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
}

void PqwAnalogOutputHandler::buildMeta() {
    CommandBuilder getConfig("get_config");
    addConnectionParams(getConfig);
    getConfig
        .description("读取通信配置寄存器")
        .returnField(FieldBuilder("result", FieldType::Object)
            .description("当前通信配置")
            .addField(FieldBuilder("unit_id", FieldType::Int).description("当前设备站号"))
            .addField(FieldBuilder("baud_rate", FieldType::Int).description("当前波特率"))
            .addField(FieldBuilder("parity", FieldType::Enum)
                .description("当前校验位")
                .enumValues(QStringList{"none", "odd", "even"}))
            .addField(FieldBuilder("stop_bits", FieldType::Enum)
                .description("当前停止位")
                .enumValues(QStringList{"1", "2"}))
            .addField(FieldBuilder("comm_watchdog_enabled", FieldType::Bool)
                .description("通信检测时间是否启用"))
            .addField(FieldBuilder("comm_watchdog_ms", FieldType::Int)
                .description("通信检测时间，单位毫秒"))
            .addField(FieldBuilder("comm_watchdog_raw", FieldType::Int)
                .description("原始通信检测时间寄存器值"))
            .addField(FieldBuilder("raw_registers", FieldType::Array)
                .description("寄存器 0x0000..0x0005 的原始值")
                .items(FieldBuilder("register", FieldType::Int))));

    CommandBuilder setCommConfig("set_comm_config");
    setCommConfig
        .description("写入通信参数寄存器，修改后需重新上电生效")
        .param(connectionParam("port_name"))
        .param(connectionParam("timeout"))
        .param(FieldBuilder("current_unit_id", FieldType::Int)
            .defaultValue(1)
            .range(1, 255)
            .description("当前设备站号"))
        .param(FieldBuilder("current_baud_rate", FieldType::Int)
            .defaultValue(9600)
            .enumValues(QStringList{"4800", "9600", "14400", "19200", "38400", "56000", "57600", "115200"})
            .description("当前串口波特率"))
        .param(FieldBuilder("current_parity", FieldType::Enum)
            .defaultValue("none")
            .enumValues(QStringList{"none", "odd", "even"})
            .description("当前串口校验位"))
        .param(FieldBuilder("current_stop_bits", FieldType::Enum)
            .defaultValue("1")
            .enumValues(QStringList{"1", "2"})
            .description("当前串口停止位"))
        .param(FieldBuilder("unit_id", FieldType::Int)
            .range(1, 255)
            .description("写入设备的新站号"))
        .param(FieldBuilder("baud_rate", FieldType::Int)
            .enumValues(QStringList{"4800", "9600", "14400", "19200", "38400", "56000", "57600", "115200"})
            .description("写入设备的新波特率"))
        .param(FieldBuilder("parity", FieldType::Enum)
            .enumValues(QStringList{"none", "odd", "even"})
            .description("写入设备的新校验位"))
        .param(FieldBuilder("stop_bits", FieldType::Enum)
            .enumValues(QStringList{"1", "2"})
            .description("写入设备的新停止位"))
        .param(FieldBuilder("comm_watchdog_ms", FieldType::Int)
            .range(0, 655340)
            .description("通信检测时间，单位毫秒。0 表示关闭，其他值需为 10ms 的整数倍"))
        .returnField(FieldBuilder("result", FieldType::Object)
            .description("写入结果")
            .addField(FieldBuilder("writes", FieldType::Array)
                .description("逐项写入的寄存器结果")
                .items(FieldBuilder("write", FieldType::Object)
                    .addField(FieldBuilder("field", FieldType::String).description("字段名"))
                    .addField(FieldBuilder("register", FieldType::Int).description("寄存器地址"))
                    .addField(FieldBuilder("raw_value", FieldType::Int).description("写入的原始寄存器值"))
                    .addField(FieldBuilder("value", FieldType::Any).description("用户传入的业务值"))
                    .requiredKeys(QStringList{"field", "register", "raw_value", "value"})))
            .addField(FieldBuilder("reboot_required", FieldType::Bool)
                .description("固定返回 true，表示重新上电后完全生效")));

    CommandBuilder restoreDefaults("restore_defaults");
    addConnectionParams(restoreDefaults);
    restoreDefaults
        .description("恢复模块默认通信参数")
        .returnField(FieldBuilder("result", FieldType::Object)
            .description("恢复结果")
            .addField(FieldBuilder("reboot_required", FieldType::Bool)
                .description("固定返回 true，表示重新上电后完全生效")));

    CommandBuilder readOutputs("read_outputs");
    addConnectionParams(readOutputs);
    readOutputs
        .description("读取输出通道当前寄存器值")
        .param(FieldBuilder("start_channel", FieldType::Int)
            .defaultValue(1)
            .range(1, 18)
            .description("起始通道"))
        .param(FieldBuilder("count", FieldType::Int)
            .defaultValue(18)
            .range(1, 18)
            .description("读取通道数量"))
        .returnField(FieldBuilder("result", FieldType::Object)
            .description("读取结果")
            .addField(FieldBuilder("start_channel", FieldType::Int).description("起始通道"))
            .addField(FieldBuilder("count", FieldType::Int).description("读取数量"))
            .addField(FieldBuilder("outputs", FieldType::Array)
                .description("每个通道的原始寄存器值和工程量值")
                .items(FieldBuilder("output", FieldType::Object)
                    .addField(FieldBuilder("channel", FieldType::Int).description("通道号"))
                    .addField(FieldBuilder("raw_value", FieldType::Int).description("寄存器原始值"))
                    .addField(FieldBuilder("value", FieldType::Double)
                        .description("工程量值，按 3 位小数解释，单位取决于模块型号"))
                    .requiredKeys(QStringList{"channel", "raw_value", "value"}))));

    CommandBuilder writeOutput("write_output");
    addConnectionParams(writeOutput);
    writeOutput
        .description("写单个输出通道寄存器")
        .param(FieldBuilder("channel", FieldType::Int)
            .required()
            .range(1, 18)
            .description("通道号"))
        .param(FieldBuilder("value", FieldType::Double)
            .required()
            .description("工程量值，按 3 位小数编码为寄存器值"))
        .returnField(FieldBuilder("result", FieldType::Object)
            .description("写入结果")
            .addField(FieldBuilder("channel", FieldType::Int).description("通道号"))
            .addField(FieldBuilder("value", FieldType::Double).description("写入的工程量值"))
            .addField(FieldBuilder("raw_value", FieldType::Int).description("写入的寄存器原始值")));

    CommandBuilder writeOutputs("write_outputs");
    addConnectionParams(writeOutputs);
    writeOutputs
        .description("连续批量写多个输出通道寄存器")
        .param(FieldBuilder("start_channel", FieldType::Int)
            .defaultValue(1)
            .range(1, 18)
            .description("起始通道"))
        .param(FieldBuilder("values", FieldType::Array)
            .required()
            .description("连续通道的工程量值数组")
            .items(FieldBuilder("value", FieldType::Double).description("单个通道工程量值"))
            .minItems(1)
            .maxItems(18))
        .returnField(FieldBuilder("result", FieldType::Object)
            .description("写入结果")
            .addField(FieldBuilder("start_channel", FieldType::Int).description("起始通道"))
            .addField(FieldBuilder("count", FieldType::Int).description("写入数量"))
            .addField(FieldBuilder("values", FieldType::Array)
                .description("写入的工程量值数组")
                .items(FieldBuilder("value", FieldType::Double)))
            .addField(FieldBuilder("raw_values", FieldType::Array)
                .description("写入的寄存器原始值数组")
                .items(FieldBuilder("raw_value", FieldType::Int))));

    CommandBuilder clearOutputs("clear_outputs");
    addConnectionParams(clearOutputs);
    clearOutputs
        .description("清零所有输出通道")
        .returnField(FieldBuilder("result", FieldType::Object)
            .description("清零结果")
            .addField(FieldBuilder("cleared", FieldType::Bool).description("固定返回 true")));

    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("stdio.drv.pqw_analog_output",
              QString::fromUtf8("品全微模拟量模块"),
              "1.0.0",
              QString::fromUtf8("基于 Modbus RTU 串口的品全微模拟量输出模块驱动"))
        .vendor("stdiolink")
        .command(CommandBuilder("status")
            .description("获取驱动状态")
            .returnField(FieldBuilder("result", FieldType::Object)
                .description("状态信息")
                .addField(FieldBuilder("status", FieldType::String).description("固定返回 ready"))))
        .command(getConfig)
        .command(setCommConfig)
        .command(restoreDefaults)
        .command(readOutputs)
        .command(writeOutput)
        .command(writeOutputs)
        .command(clearOutputs)
        .build();
    stdiolink::meta::ensureCommandExamples(m_meta);
}
