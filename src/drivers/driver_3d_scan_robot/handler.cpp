#include "driver_3d_scan_robot/handler.h"

#include <QJsonObject>
#include <QSet>
#include <QThread>
#include <functional>
#include <memory>

#include "driver_3d_scan_robot/protocol_codec.h"
#include "driver_3d_scan_robot/radar_session.h"
#include "driver_3d_scan_robot/radar_transport.h"
#include "stdiolink/driver/example_auto_fill.h"
#include "stdiolink/driver/meta_builder.h"

using namespace stdiolink;
using namespace stdiolink::meta;
using namespace scan_robot;

// ── 默认 transport 工厂 ─────────────────────────────────

namespace {

// 前置声明 RadarSerialTransport (定义在 radar_transport.cpp 中)
class RadarSerialTransport;

IRadarTransport* createSerialTransport();

} // namespace

// ── 连接参数构建辅助 ────────────────────────────────────

static FieldBuilder connectionParam(const QString& name) {
    if (name == "port") {
        return FieldBuilder("port", FieldType::String)
            .required()
            .description(QString::fromUtf8("串口名称，如 COM3"))
            .placeholder("COM3");
    }
    if (name == "addr") {
        return FieldBuilder("addr", FieldType::Int)
            .required()
            .range(0, 255)
            .description(QString::fromUtf8("设备地址"));
    }
    if (name == "baud_rate") {
        return FieldBuilder("baud_rate", FieldType::Int)
            .defaultValue(115200)
            .description(QString::fromUtf8("波特率"));
    }
    if (name == "timeout_ms") {
        return FieldBuilder("timeout_ms", FieldType::Int)
            .defaultValue(5000)
            .range(100, 300000)
            .unit("ms")
            .description(QString::fromUtf8("单次读写超时"));
    }
    if (name == "task_timeout_ms") {
        return FieldBuilder("task_timeout_ms", FieldType::Int)
            .defaultValue(-1)
            .unit("ms")
            .description(QString::fromUtf8("长任务超时（-1 使用命令默认值）"));
    }
    if (name == "query_interval_ms") {
        return FieldBuilder("query_interval_ms", FieldType::Int)
            .defaultValue(1000)
            .range(100, 60000)
            .unit("ms")
            .description(QString::fromUtf8("长任务轮询间隔"));
    }
    // inter_command_delay_ms
    return FieldBuilder("inter_command_delay_ms", FieldType::Int)
        .defaultValue(250)
        .range(0, 10000)
        .unit("ms")
        .description(QString::fromUtf8("连续命令间延时"));
}

static void addConnectionParams(CommandBuilder& cb) {
    cb.param(connectionParam("port"))
      .param(connectionParam("addr"))
      .param(connectionParam("baud_rate"))
      .param(connectionParam("timeout_ms"));
}

static void addLongTaskParams(CommandBuilder& cb) {
    addConnectionParams(cb);
    cb.param(connectionParam("task_timeout_ms"))
      .param(connectionParam("query_interval_ms"))
      .param(connectionParam("inter_command_delay_ms"));
}

// ── 参数解析辅助 ────────────────────────────────────────

static RadarTransportParams parseTransportParams(const QJsonObject& p) {
    RadarTransportParams tp;
    tp.port            = p["port"].toString();
    tp.addr            = static_cast<quint8>(p["addr"].toInt(1));
    tp.baudRate        = p["baud_rate"].toInt(115200);
    tp.timeoutMs       = p["timeout_ms"].toInt(5000);
    tp.taskTimeoutMs   = p["task_timeout_ms"].toInt(-1);
    tp.queryIntervalMs = p["query_interval_ms"].toInt(1000);
    tp.interCommandDelayMs = p["inter_command_delay_ms"].toInt(250);
    return tp;
}

static int resolveTaskTimeout(const RadarTransportParams& tp, const QString& cmd) {
    if (tp.taskTimeoutMs > 0) return tp.taskTimeoutMs;
    if (cmd == "scan_frame") return 1000000;
    if (cmd == "scan_line") return 100000;
    // move, get_distance_at, calib*
    return 180000;
}

static int errorCodeForSession(SessionErrorKind kind) {
    return kind == SessionErrorKind::Transport ? 1 : 2;
}

static QString modeNameFromCode(quint32 modeCode) {
    switch (modeCode) {
    case 10: return "boot";
    case 20: return "imaging";
    case 30: return "standby";
    case 40: return "sleep";
    default: return "unknown";
    }
}

static QString switchStateFromCode(quint32 code) {
    switch (code) {
    case 10: return "near";
    case 20: return "far";
    default: return "unknown";
    }
}

static QString calibStateFromCode(quint32 code) {
    switch (code) {
    case 10: return "calibrated";
    case 20: return "uncalibrated";
    default: return "unknown";
    }
}

static QJsonObject stateFlagsFromRaw(quint32 rawState) {
    return QJsonObject{
        {"clock_error", static_cast<bool>(rawState & (1u << 0))},
        {"flash_error", static_cast<bool>(rawState & (1u << 1))},
        {"x_motor_driver_alarm", static_cast<bool>(rawState & (1u << 2))},
        {"y_motor_driver_alarm", static_cast<bool>(rawState & (1u << 3))},
        {"x_calibration_error", static_cast<bool>(rawState & (1u << 4))},
        {"y_calibration_error", static_cast<bool>(rawState & (1u << 5))},
        {"parameter_error", static_cast<bool>(rawState & (1u << 6))},
        {"radar_communication_error", static_cast<bool>(rawState & (1u << 7))},
        {"device_reset_failed", static_cast<bool>(rawState & (1u << 8))},
        {"program_jump_failed", static_cast<bool>(rawState & (1u << 9))}
    };
}

// ── Handler 实现 ────────────────────────────────────────

ThreeDScanRobotHandler::ThreeDScanRobotHandler() {
    buildMeta();
}

void ThreeDScanRobotHandler::setTransportFactory(
    std::function<IRadarTransport*()> factory) {
    m_transportFactory = std::move(factory);
}

void ThreeDScanRobotHandler::handle(const QString& cmd, const QJsonValue& data,
                                     IResponder& responder) {
    // ── 本地虚拟命令 ────────────────────────────────────
    if (cmd == "status") {
        responder.done(0, QJsonObject{{"status", "ready"}});
        return;
    }

    // ── 未知命令早期检测 ────────────────────────────────
    static const QSet<QString> knownCommands = {
        "test", "get_addr", "set_addr", "get_mode", "set_mode",
        "get_temp", "get_state", "get_version", "get_angles",
        "get_switch_x", "get_switch_y", "get_calib_x", "get_calib_y",
        "calib", "calib_x", "calib_y", "move", "get_distance_at",
        "get_distance", "get_reg", "set_reg",
        "scan_line", "scan_frame", "get_data",
        "query",
        "interrupt_test", "scan_progress", "scan_cancel",
    };
    if (!knownCommands.contains(cmd)) {
        responder.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
        return;
    }

    QJsonObject p = data.toObject();
    RadarTransportParams tp = parseTransportParams(p);

    // Create transport
    std::unique_ptr<IRadarTransport> transport;
    if (m_transportFactory) {
        transport.reset(m_transportFactory());
    } else {
        transport.reset(createSerialTransport());
    }

    // Open connection
    RadarSession session(transport.get());
    QString err;
    auto respondSessionError = [&](SessionErrorKind kind, const QString& message) {
        responder.error(errorCodeForSession(kind), QJsonObject{{"message", message}});
    };
    if (!session.open(tp, &err)) {
        responder.error(1, QJsonObject{{"message", err}});
        return;
    }

    // ── test ────────────────────────────────────────────
    if (cmd == "test") {
        quint32 value = static_cast<quint32>(p["value"].toInt(1000));
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::TestCom, makeU32Payload(value), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        if (resp.payload.size() < 4) {
            responder.error(2, QJsonObject{{"message", "Invalid test response"}});
            return;
        }
        quint32 fb = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(resp.payload.constData()));
        responder.done(0, QJsonObject{{"echo", static_cast<qint64>(fb)}});
        return;
    }

    // ── get_addr ────────────────────────────────────────
    if (cmd == "get_addr") {
        // Send test to broadcast addr 255 to discover actual address
        quint32 value = static_cast<quint32>(p["value"].toInt(1000));
        quint8 ctr = session.nextCounter();
        QByteArray frame = encodeFrame(ctr, 255, CmdId::TestCom, makeU32Payload(value));
        if (!transport->writeFrame(frame, tp.timeoutMs, &err)) {
            responder.error(1, QJsonObject{{"message", err}});
            return;
        }
        QByteArray chunk;
        if (!transport->readSome(chunk, tp.timeoutMs, &err)) {
            responder.error(1, QJsonObject{{"message", err}});
            return;
        }
        RadarFrame resp;
        DecodeStatus ds = tryDecodeFrame(chunk, 0, CmdId::TestCom, &resp, &err, FrameChannel::Main, nullptr, true);
        if (ds != DecodeStatus::Ok) {
            responder.error(2, QJsonObject{{"message", err}});
            return;
        }
        if (resp.payload.size() < 4) {
            responder.error(2, QJsonObject{{"message", "Invalid get_addr response"}});
            return;
        }
        const quint32 echo = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(resp.payload.constData()));
        if (echo != ~value) {
            responder.error(2, QJsonObject{{"message", "get_addr echo mismatch"}});
            return;
        }
        responder.done(0, QJsonObject{{"addr", static_cast<int>(resp.addr)}});
        return;
    }

    // ── 寄存器读命令 ────────────────────────────────────
    auto handleRegRead = [&](quint16 regId) {
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::RegRead, makeRegPayload(regId, 100), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        quint16 regAddr;
        quint32 value;
        if (!parseRegResponse(resp.payload, &regAddr, &value) || regAddr != regId) {
            responder.error(2, QJsonObject{{"message", "Invalid register response"}});
            return;
        }
        responder.done(0, QJsonObject{{"register", regId}, {"value", static_cast<qint64>(value)}});
    };

    if (cmd == "get_mode") {
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::RegRead, makeRegPayload(RegId::WorkMode, 100), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        quint16 regAddr; quint32 value;
        if (!parseRegResponse(resp.payload, &regAddr, &value)) {
            responder.error(2, QJsonObject{{"message", "Invalid register response"}});
            return;
        }
        QString modeName = modeNameFromCode(value);
        responder.done(0, QJsonObject{{"mode", modeName}, {"mode_code", static_cast<qint64>(value)}});
        return;
    }

    if (cmd == "set_mode") {
        QString mode = p["mode"].toString();
        quint32 modeCode = 0;
        if (mode == "boot") modeCode = 10;
        else if (mode == "imaging") modeCode = 20;
        else if (mode == "standby") modeCode = 30;
        else if (mode == "sleep") modeCode = 40;
        else modeCode = static_cast<quint32>(p["mode_code"].toInt());
        if (modeCode != 10 && modeCode != 20 && modeCode != 30 && modeCode != 40) {
            responder.error(2, QJsonObject{{"message", "Invalid mode or mode_code"}});
            return;
        }

        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::RegWrite, makeRegPayload(RegId::WorkMode, modeCode), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        quint16 regAddr; quint32 value;
        if (!parseRegResponse(resp.payload, &regAddr, &value)) {
            responder.error(2, QJsonObject{{"message", "Invalid register response"}});
            return;
        }
        responder.done(0, QJsonObject{
            {"mode", modeNameFromCode(value)},
            {"mode_code", static_cast<qint64>(value)}
        });
        return;
    }

    if (cmd == "set_addr") {
        quint32 newAddr = static_cast<quint32>(p["new_addr"].toInt());
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::RegWrite, makeRegPayload(RegId::SlaveAddress, newAddr), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        quint16 regAddr; quint32 value;
        if (!parseRegResponse(resp.payload, &regAddr, &value)) {
            responder.error(2, QJsonObject{{"message", "Invalid register response"}});
            return;
        }
        responder.done(0, QJsonObject{{"new_addr", static_cast<int>(value)}});
        return;
    }

    if (cmd == "get_temp") {
        RadarFrame resp1, resp2;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::RegRead, makeRegPayload(RegId::McuTemperature, 100), &resp1, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        QThread::msleep(50);
        if (!session.sendAndReceive(CmdId::RegRead, makeRegPayload(RegId::BoardTemperature, 100), &resp2, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        quint16 r1, r2; quint32 v1, v2;
        if (!parseRegResponse(resp1.payload, &r1, &v1) || !parseRegResponse(resp2.payload, &r2, &v2)) {
            responder.error(2, QJsonObject{{"message", "Invalid register response"}});
            return;
        }
        const qint32 mcuRaw = static_cast<qint32>(v1);
        const qint32 boardRaw = static_cast<qint32>(v2);
        responder.done(0, QJsonObject{
            {"mcu_temp_raw", mcuRaw},
            {"board_temp_raw", boardRaw},
            {"mcu_temp_deg_c", static_cast<double>(mcuRaw) / 100.0},
            {"board_temp_deg_c", static_cast<double>(boardRaw) / 100.0}
        });
        return;
    }

    if (cmd == "get_state") {
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::RegRead, makeRegPayload(RegId::DeviceStatus, 100), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        quint16 regAddr;
        quint32 value;
        if (!parseRegResponse(resp.payload, &regAddr, &value)) {
            responder.error(2, QJsonObject{{"message", "Invalid register response"}});
            return;
        }
        responder.done(0, QJsonObject{
            {"raw_state", static_cast<qint64>(value)},
            {"flags", stateFlagsFromRaw(value)}
        });
        return;
    }
    if (cmd == "get_version") { handleRegRead(RegId::FirmwareVersion); return; }
    if (cmd == "get_switch_x" || cmd == "get_switch_y") {
        const quint16 regId = cmd == "get_switch_x" ? RegId::XProximitySwitch : RegId::YProximitySwitch;
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::RegRead, makeRegPayload(regId, 100), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        quint16 regAddr;
        quint32 value;
        if (!parseRegResponse(resp.payload, &regAddr, &value)) {
            responder.error(2, QJsonObject{{"message", "Invalid register response"}});
            return;
        }
        responder.done(0, QJsonObject{
            {"state", switchStateFromCode(value)},
            {"state_code", static_cast<qint64>(value)}
        });
        return;
    }
    if (cmd == "get_calib_x" || cmd == "get_calib_y") {
        const quint16 regId = cmd == "get_calib_x" ? RegId::XMotorCalib : RegId::YMotorCalib;
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::RegRead, makeRegPayload(regId, 100), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        quint16 regAddr;
        quint32 value;
        if (!parseRegResponse(resp.payload, &regAddr, &value)) {
            responder.error(2, QJsonObject{{"message", "Invalid register response"}});
            return;
        }
        responder.done(0, QJsonObject{
            {"state", calibStateFromCode(value)},
            {"state_code", static_cast<qint64>(value)}
        });
        return;
    }

    if (cmd == "get_angles") {
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::RegRead, makeRegPayload(RegId::DirectionAngle, 100), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        quint16 regAddr; quint32 value;
        if (!parseRegResponse(resp.payload, &regAddr, &value)) {
            responder.error(2, QJsonObject{{"message", "Invalid register response"}});
            return;
        }
        double xDeg = static_cast<double>(value >> 16) / 100.0;
        double yDeg = static_cast<double>(value & 0xFFFF) / 100.0;
        responder.done(0, QJsonObject{{"x_deg", xDeg}, {"y_deg", yDeg},
                                       {"raw", static_cast<qint64>(value)}});
        return;
    }

    // ── get_reg / set_reg ───────────────────────────────
    if (cmd == "get_reg") {
        quint16 regAddr = static_cast<quint16>(p["register"].toInt());
        handleRegRead(regAddr);
        return;
    }

    if (cmd == "set_reg") {
        quint16 regAddr = static_cast<quint16>(p["register"].toInt());
        quint32 value = static_cast<quint32>(p["value"].toInt());
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::RegWrite, makeRegPayload(regAddr, value), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        quint16 ra; quint32 v;
        if (!parseRegResponse(resp.payload, &ra, &v)) {
            responder.error(2, QJsonObject{{"message", "Invalid register response"}});
            return;
        }
        responder.done(0, QJsonObject{{"register", regAddr}, {"value", static_cast<qint64>(v)}});
        return;
    }

    // ── get_distance ────────────────────────────────────
    if (cmd == "get_distance") {
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::GetDistance, makeU32Payload(100), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        if (resp.payload.size() < 4) {
            responder.error(2, QJsonObject{{"message", "Invalid get_distance response"}});
            return;
        }
        quint32 distMm = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(resp.payload.constData()));
        responder.done(0, QJsonObject{{"distance_mm", static_cast<qint64>(distMm)}});
        return;
    }

    // ── 长任务辅助 Lambda ────────────────────────────────
    auto execLongTaskWithResult = [&](quint8 cmdId, const QByteArray& payload,
                                      const QString& cmdName, QueryTaskResult* out) -> bool {
        RadarFrame initResp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(cmdId, payload, &initResp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return false;
        }

        int taskTimeout = resolveTaskTimeout(tp, cmdName);
        quint8 expectedCtr = initResp.counter;

        if (!session.waitTaskCompleted(expectedCtr, cmdId, taskTimeout,
                                       tp.queryIntervalMs, tp.interCommandDelayMs,
                                       out, &err)) {
            responder.error(2, QJsonObject{{"message", err}});
            return false;
        }
        return true;
    };

    // ── calib / calib_x / calib_y ───────────────────────
    if (cmd == "calib" || cmd == "calib_x" || cmd == "calib_y") {
        quint32 calibValue = 300;
        if (cmd == "calib_x") calibValue = 100;
        else if (cmd == "calib_y") calibValue = 200;

        QueryTaskResult tr;
        if (!execLongTaskWithResult(CmdId::Calibration, makeU32Payload(calibValue), cmd, &tr))
            return;

        if (tr.resultCode == TaskResult::Success)
            responder.done(0, QJsonObject{{"result", "ok"}});
        else
            responder.error(2, QJsonObject{{"result", "error"}, {"result_code", static_cast<qint64>(tr.resultCode)}});
        return;
    }

    // ── move ────────────────────────────────────────────
    if (cmd == "move") {
        double xDeg = p["x_deg"].toDouble();
        double yDeg = p["y_deg"].toDouble();
        quint16 ax = static_cast<quint16>(qRound(xDeg * 100));
        quint16 ay = static_cast<quint16>(qRound(yDeg * 100));
        quint32 n = (static_cast<quint32>(ax) << 16) | ay;

        QueryTaskResult tr;
        if (!execLongTaskWithResult(CmdId::Move, makeU32Payload(n), cmd, &tr))
            return;

        if (tr.resultCode == TaskResult::Success)
            responder.done(0, QJsonObject{{"result", "ok"}, {"x_deg", xDeg}, {"y_deg", yDeg}});
        else
            responder.error(2, QJsonObject{{"result", "error"}, {"result_code", static_cast<qint64>(tr.resultCode)}});
        return;
    }

    // ── get_distance_at ─────────────────────────────────
    if (cmd == "get_distance_at") {
        double xDeg = p["x_deg"].toDouble();
        double yDeg = p["y_deg"].toDouble();
        quint16 ax = static_cast<quint16>(qRound(xDeg * 100));
        quint16 ay = static_cast<quint16>(qRound(yDeg * 100));
        quint32 n = (static_cast<quint32>(ax) << 16) | ay;

        QueryTaskResult tr;
        if (!execLongTaskWithResult(CmdId::MoveDist, makeU32Payload(n), cmd, &tr))
            return;

        // resultCode contains the distance in mm for get_distance_at
        responder.done(0, QJsonObject{
            {"x_deg", xDeg}, {"y_deg", yDeg},
            {"distance_mm", static_cast<qint64>(tr.resultCode)}
        });
        return;
    }

    // ── scan_line ───────────────────────────────────────
    if (cmd == "scan_line") {
        quint16 angleX = static_cast<quint16>(qRound(p["angle_x"].toDouble() * 100));
        quint16 beginY = static_cast<quint16>(qRound(p["begin_y"].toDouble() * 100));
        quint16 endY   = static_cast<quint16>(qRound(p["end_y"].toDouble() * 100));
        quint16 stepY  = static_cast<quint16>(qRound(p["step_y"].toDouble() * 100));
        quint16 speedY = static_cast<quint16>(qRound(p["speed_y"].toDouble() * 100));

        QByteArray payload = makeScanLinePayload(angleX, beginY, endY, stepY, speedY);

        RadarFrame initResp;
        if (!session.sendAndReceive(CmdId::ScanLine, payload, &initResp, &err)) {
            responder.error(1, QJsonObject{{"message", err}});
            return;
        }

        int taskTimeout = resolveTaskTimeout(tp, cmd);
        quint8 expectedCtr = initResp.counter;
        QueryTaskResult tr;
        if (!session.waitTaskCompleted(expectedCtr, CmdId::ScanLine, taskTimeout,
                                       tp.queryIntervalMs, tp.interCommandDelayMs, &tr, &err)) {
            responder.error(2, QJsonObject{{"message", err}});
            return;
        }
        if (tr.resultCode == 0 || tr.resultCode == TaskResult::Failed) {
            responder.error(2, QJsonObject{
                {"message", "Scan line failed"},
                {"result_code", static_cast<qint64>(tr.resultCode)}
            });
            return;
        }

        int totalBytes = static_cast<int>(tr.resultCode);
        ScanAggregateResult scanResult;
        scanResult.taskCounter = tr.lastCounter;
        scanResult.taskCommand = tr.lastCommand;
        scanResult.resultCode  = tr.resultCode;

        if (totalBytes > 0) {
            QThread::msleep(static_cast<unsigned long>(tp.interCommandDelayMs));
            if (!session.collectScanData(totalBytes, tp.interCommandDelayMs, &scanResult, &err)) {
                responder.error(2, QJsonObject{{"message", err}});
                return;
            }
        }

        responder.done(0, QJsonObject{
            {"task_counter", scanResult.taskCounter},
            {"task_command", "scan_line"},
            {"result_code", static_cast<qint64>(scanResult.resultCode)},
            {"segment_count", scanResult.segmentCount},
            {"byte_count", scanResult.byteCount},
            {"data_base64", QString::fromLatin1(scanResult.data.toBase64())}
        });
        return;
    }

    // ── scan_frame ──────────────────────────────────────
    if (cmd == "scan_frame") {
        quint16 beginX = static_cast<quint16>(qRound(p["begin_x"].toDouble() * 100));
        quint16 endX   = static_cast<quint16>(qRound(p["end_x"].toDouble() * 100));
        quint16 stepX  = static_cast<quint16>(qRound(p["step_x"].toDouble() * 100));
        quint16 beginY = static_cast<quint16>(qRound(p["begin_y"].toDouble() * 100));
        quint16 endY   = static_cast<quint16>(qRound(p["end_y"].toDouble() * 100));
        quint16 stepY  = static_cast<quint16>(qRound(p["step_y"].toDouble() * 100));
        quint16 speedY = static_cast<quint16>(qRound(p["speed_y"].toDouble() * 100));

        QByteArray payload = makeScanFramePayload(beginX, endX, stepX, beginY, endY, stepY, speedY);

        RadarFrame initResp;
        if (!session.sendAndReceive(CmdId::ScanFrame, payload, &initResp, &err)) {
            responder.error(1, QJsonObject{{"message", err}});
            return;
        }

        int taskTimeout = resolveTaskTimeout(tp, cmd);
        quint8 expectedCtr = initResp.counter;
        QueryTaskResult tr;
        if (!session.waitTaskCompleted(expectedCtr, CmdId::ScanFrame, taskTimeout,
                                       tp.queryIntervalMs, tp.interCommandDelayMs, &tr, &err)) {
            responder.error(2, QJsonObject{{"message", err}});
            return;
        }

        int totalBytes = static_cast<int>(tr.resultCode);
        ScanAggregateResult scanResult;
        scanResult.taskCounter = tr.lastCounter;
        scanResult.taskCommand = tr.lastCommand;
        scanResult.resultCode  = tr.resultCode;

        if (totalBytes > 0 && totalBytes < 1000000) {
            QThread::msleep(500); // MatLab uses pause(0.5) before get_data after frame
            if (!session.collectScanData(totalBytes, tp.interCommandDelayMs, &scanResult, &err)) {
                responder.error(2, QJsonObject{{"message", err}});
                return;
            }
        } else if (totalBytes <= 0) {
            responder.error(2, QJsonObject{
                {"message", "Scan frame returned no data"},
                {"result_code", static_cast<qint64>(tr.resultCode)}
            });
            return;
        }

        responder.done(0, QJsonObject{
            {"task_counter", scanResult.taskCounter},
            {"task_command", "scan_frame"},
            {"result_code", static_cast<qint64>(scanResult.resultCode)},
            {"segment_count", scanResult.segmentCount},
            {"byte_count", scanResult.byteCount},
            {"data_base64", QString::fromLatin1(scanResult.data.toBase64())}
        });
        return;
    }

    // ── get_data ────────────────────────────────────────
    if (cmd == "get_data") {
        int totalBytes = p["total_bytes"].toInt();
        if (totalBytes <= 0 || totalBytes >= 1000000) {
            responder.error(2, QJsonObject{{"message", "total_bytes out of range"}});
            return;
        }

        ScanAggregateResult scanResult;
        if (!session.collectScanData(totalBytes, tp.interCommandDelayMs, &scanResult, &err)) {
            responder.error(2, QJsonObject{{"message", err}});
            return;
        }

        responder.done(0, QJsonObject{
            {"task_counter", scanResult.taskCounter},
            {"task_command", "get_data"},
            {"result_code", static_cast<qint64>(scanResult.resultCode)},
            {"segment_count", scanResult.segmentCount},
            {"byte_count", scanResult.byteCount},
            {"data_base64", QString::fromLatin1(scanResult.data.toBase64())}
        });
        return;
    }

    // ── query ───────────────────────────────────────────
    if (cmd == "query") {
        const quint32 op = static_cast<quint32>(p["op"].toInt(100));
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::Query, makeU32Payload(op), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        quint8 lastCtr, lastCmd; quint32 resultCode;
        if (!parseQueryResponse(resp.payload, &lastCtr, &lastCmd, &resultCode)) {
            responder.error(2, QJsonObject{{"message", "Invalid query response"}});
            return;
        }
        responder.done(0, QJsonObject{
            {"counter", lastCtr}, {"command", lastCmd},
            {"result", static_cast<qint64>(resultCode)}
        });
        return;
    }

    // ── 中断式命令 ──────────────────────────────────────
    if (cmd == "interrupt_test") {
        quint32 value = static_cast<quint32>(p["value"].toInt(1000));
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(InsertCmdId::Test, makeU32Payload(value), &resp, &err, true, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        if (resp.payload.size() < 4) {
            responder.error(2, QJsonObject{{"message", "Invalid interrupt_test response"}});
            return;
        }
        quint32 fb = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(resp.payload.constData()));
        responder.done(0, QJsonObject{{"value", static_cast<qint64>(fb)}});
        return;
    }

    if (cmd == "scan_progress") {
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(InsertCmdId::ScanProgress, makeU32Payload(1000), &resp, &err, true, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        if (resp.payload.size() < 4) {
            responder.error(2, QJsonObject{{"message", "Invalid scan_progress response"}});
            return;
        }
        quint32 raw = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(resp.payload.constData()));
        quint16 currentLine = static_cast<quint16>(raw >> 16);
        quint16 totalLines  = static_cast<quint16>(raw & 0xFFFF);
        responder.done(0, QJsonObject{
            {"current_line", currentLine}, {"total_lines", totalLines},
            {"raw", static_cast<qint64>(raw)}
        });
        return;
    }

    if (cmd == "scan_cancel") {
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(InsertCmdId::ScanCancel, makeU32Payload(1000), &resp, &err, true, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        if (resp.payload.size() < 4) {
            responder.error(2, QJsonObject{{"message", "Invalid scan_cancel response"}});
            return;
        }
        quint32 fb = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(resp.payload.constData()));
        responder.done(0, QJsonObject{{"value", static_cast<qint64>(fb)}});
        return;
    }

    // ── 未知命令 ────────────────────────────────────────
    responder.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
}

// ── 元数据构建 ──────────────────────────────────────────

void ThreeDScanRobotHandler::buildMeta() {
    // status
    auto statusCmd = CommandBuilder("status")
        .description(QString::fromUtf8("返回驱动存活状态"));

    // test
    auto testCmd = CommandBuilder("test")
        .description(QString::fromUtf8("测试主协议通信"));
    addConnectionParams(testCmd);
    testCmd.param(FieldBuilder("value", FieldType::Int).defaultValue(1000)
        .description(QString::fromUtf8("测试值")));

    // get_addr
    auto getAddrCmd = CommandBuilder("get_addr")
        .description(QString::fromUtf8("读取设备地址（广播探测）"));
    addConnectionParams(getAddrCmd);
    getAddrCmd.param(FieldBuilder("value", FieldType::Int).defaultValue(1000)
        .description(QString::fromUtf8("测试值，用于校验返回地址")));

    // set_addr
    auto setAddrCmd = CommandBuilder("set_addr")
        .description(QString::fromUtf8("设置设备地址"));
    addConnectionParams(setAddrCmd);
    setAddrCmd.param(FieldBuilder("new_addr", FieldType::Int).required().range(0, 254)
        .description(QString::fromUtf8("新地址")));

    // get_mode / set_mode
    auto getModeCmd = CommandBuilder("get_mode")
        .description(QString::fromUtf8("读取工作模式"));
    addConnectionParams(getModeCmd);

    auto setModeCmd = CommandBuilder("set_mode")
        .description(QString::fromUtf8("设置工作模式"));
    addConnectionParams(setModeCmd);
    setModeCmd.param(FieldBuilder("mode", FieldType::Enum)
        .enumValues(QStringList{"boot", "imaging", "standby", "sleep"})
        .description(QString::fromUtf8("工作模式")));
    setModeCmd.param(FieldBuilder("mode_code", FieldType::Int)
        .range(10, 40)
        .description(QString::fromUtf8("工作模式代码：10=boot,20=imaging,30=standby,40=sleep")));

    // get_temp
    auto getTempCmd = CommandBuilder("get_temp")
        .description(QString::fromUtf8("返回 MCU / 板卡温度"));
    addConnectionParams(getTempCmd);

    // get_state
    auto getStateCmd = CommandBuilder("get_state")
        .description(QString::fromUtf8("返回原始状态字和已确认状态位"));
    addConnectionParams(getStateCmd);

    // get_version
    auto getVersionCmd = CommandBuilder("get_version")
        .description(QString::fromUtf8("返回固件版本"));
    addConnectionParams(getVersionCmd);

    // get_angles
    auto getAnglesCmd = CommandBuilder("get_angles")
        .description(QString::fromUtf8("返回 X/Y 当前角度"));
    addConnectionParams(getAnglesCmd);

    // get_switch_x / get_switch_y
    auto getSwitchXCmd = CommandBuilder("get_switch_x")
        .description(QString::fromUtf8("返回 X 轴接近开关语义状态"));
    addConnectionParams(getSwitchXCmd);

    auto getSwitchYCmd = CommandBuilder("get_switch_y")
        .description(QString::fromUtf8("返回 Y 轴接近开关语义状态"));
    addConnectionParams(getSwitchYCmd);

    // get_calib_x / get_calib_y
    auto getCalibXCmd = CommandBuilder("get_calib_x")
        .description(QString::fromUtf8("返回 X 轴校准语义状态"));
    addConnectionParams(getCalibXCmd);

    auto getCalibYCmd = CommandBuilder("get_calib_y")
        .description(QString::fromUtf8("返回 Y 轴校准语义状态"));
    addConnectionParams(getCalibYCmd);

    // calib / calib_x / calib_y
    auto calibCmd = CommandBuilder("calib")
        .description(QString::fromUtf8("全量校准"));
    addLongTaskParams(calibCmd);

    auto calibXCmd = CommandBuilder("calib_x")
        .description(QString::fromUtf8("X 轴校准"));
    addLongTaskParams(calibXCmd);

    auto calibYCmd = CommandBuilder("calib_y")
        .description(QString::fromUtf8("Y 轴校准"));
    addLongTaskParams(calibYCmd);

    // move
    auto moveCmd = CommandBuilder("move")
        .description(QString::fromUtf8("绝对角度移动"));
    addLongTaskParams(moveCmd);
    moveCmd.param(FieldBuilder("x_deg", FieldType::Double).required().range(0, 186)
        .description(QString::fromUtf8("X 角度（°）")));
    moveCmd.param(FieldBuilder("y_deg", FieldType::Double).required().range(0, 186)
        .description(QString::fromUtf8("Y 角度（°）")));

    // get_distance_at
    auto getDistanceAtCmd = CommandBuilder("get_distance_at")
        .description(QString::fromUtf8("获取指定角度的距离"));
    addLongTaskParams(getDistanceAtCmd);
    getDistanceAtCmd.param(FieldBuilder("x_deg", FieldType::Double).required().range(0, 186)
        .description(QString::fromUtf8("X 角度（°）")));
    getDistanceAtCmd.param(FieldBuilder("y_deg", FieldType::Double).required().range(0, 101)
        .description(QString::fromUtf8("Y 角度（°）")));

    // get_distance
    auto getDistanceCmd = CommandBuilder("get_distance")
        .description(QString::fromUtf8("单点测距"));
    addConnectionParams(getDistanceCmd);

    // get_reg / set_reg
    auto getRegCmd = CommandBuilder("get_reg")
        .description(QString::fromUtf8("原始寄存器读"));
    addConnectionParams(getRegCmd);
    getRegCmd.param(FieldBuilder("register", FieldType::Int).required().range(0, 500)
        .description(QString::fromUtf8("寄存器地址")));

    auto setRegCmd = CommandBuilder("set_reg")
        .description(QString::fromUtf8("原始寄存器写"));
    addConnectionParams(setRegCmd);
    setRegCmd.param(FieldBuilder("register", FieldType::Int).required().range(0, 500)
        .description(QString::fromUtf8("寄存器地址")));
    setRegCmd.param(FieldBuilder("value", FieldType::Int).required()
        .description(QString::fromUtf8("写入值")));

    // scan_line
    auto scanLineCmd = CommandBuilder("scan_line")
        .description(QString::fromUtf8("单线扫描（启动 + 轮询 + 分段聚合）"));
    addLongTaskParams(scanLineCmd);
    scanLineCmd.param(FieldBuilder("angle_x", FieldType::Double).required()
        .defaultValue(0).range(0, 186).description(QString::fromUtf8("X 角度，单位 deg，编码时 ×100")));
    scanLineCmd.param(FieldBuilder("begin_y", FieldType::Double).required()
        .defaultValue(1).range(1, 100).description(QString::fromUtf8("Y 起始角度，单位 deg，编码时 ×100")));
    scanLineCmd.param(FieldBuilder("end_y", FieldType::Double).required()
        .defaultValue(100).range(1, 100).description(QString::fromUtf8("Y 终止角度，单位 deg，编码时 ×100")));
    scanLineCmd.param(FieldBuilder("step_y", FieldType::Double).required()
        .defaultValue(1).range(0.25, 99).description(QString::fromUtf8("Y 角度步长，单位 deg，编码时 ×100")));
    scanLineCmd.param(FieldBuilder("speed_y", FieldType::Double).required()
        .defaultValue(10).range(0.1, 10).description(QString::fromUtf8("Y 轴旋转速度，单位 deg/s，编码时 ×100")));

    // scan_frame
    auto scanFrameCmd = CommandBuilder("scan_frame")
        .description(QString::fromUtf8("帧扫描（启动 + 轮询 + 分段聚合）"));
    addLongTaskParams(scanFrameCmd);
    scanFrameCmd.param(FieldBuilder("begin_x", FieldType::Double).required()
        .defaultValue(0).range(0, 186).description(QString::fromUtf8("X 起始角度，单位 deg，编码时 ×100")));
    scanFrameCmd.param(FieldBuilder("end_x", FieldType::Double).required()
        .defaultValue(180).range(0, 186).description(QString::fromUtf8("X 终止角度，单位 deg，编码时 ×100")));
    scanFrameCmd.param(FieldBuilder("step_x", FieldType::Double).required()
        .defaultValue(5).range(0.25, 186).description(QString::fromUtf8("X 角度步长，单位 deg，编码时 ×100")));
    scanFrameCmd.param(FieldBuilder("begin_y", FieldType::Double).required()
        .defaultValue(1).range(1, 100).description(QString::fromUtf8("Y 起始角度，单位 deg，编码时 ×100")));
    scanFrameCmd.param(FieldBuilder("end_y", FieldType::Double).required()
        .defaultValue(100).range(1, 100).description(QString::fromUtf8("Y 终止角度，单位 deg，编码时 ×100")));
    scanFrameCmd.param(FieldBuilder("step_y", FieldType::Double).required()
        .defaultValue(1).range(0.25, 99).description(QString::fromUtf8("Y 角度步长，单位 deg，编码时 ×100")));
    scanFrameCmd.param(FieldBuilder("speed_y", FieldType::Double).required()
        .defaultValue(10).range(0.1, 10).description(QString::fromUtf8("Y 轴旋转速度，单位 deg/s，编码时 ×100")));

    // get_data
    auto getDataCmd = CommandBuilder("get_data")
        .description(QString::fromUtf8("按显式 total_bytes 拉取扫描分段数据（oneshot 无状态）"));
    addConnectionParams(getDataCmd);
    getDataCmd.param(connectionParam("inter_command_delay_ms"));
    getDataCmd.param(FieldBuilder("total_bytes", FieldType::Int).required()
        .range(1, 999999).description(QString::fromUtf8("数据总字节数")));

    // query
    auto queryCmd = CommandBuilder("query")
        .description(QString::fromUtf8("查询或重置最近一次长任务结果"));
    addConnectionParams(queryCmd);
    queryCmd.param(FieldBuilder("op", FieldType::Int).defaultValue(100)
        .range(100, 200)
        .description(QString::fromUtf8("100=查询最近一次结果，200=重置最近一次结果")));

    // interrupt_test
    auto interruptTestCmd = CommandBuilder("interrupt_test")
        .description(QString::fromUtf8("测试中断式通信"));
    addConnectionParams(interruptTestCmd);
    interruptTestCmd.param(FieldBuilder("value", FieldType::Int).defaultValue(1000)
        .description(QString::fromUtf8("测试值")));

    // scan_progress
    auto scanProgressCmd = CommandBuilder("scan_progress")
        .description(QString::fromUtf8("获取帧扫描进度"));
    addConnectionParams(scanProgressCmd);

    // scan_cancel
    auto scanCancelCmd = CommandBuilder("scan_cancel")
        .description(QString::fromUtf8("停止当前帧扫描"));
    addConnectionParams(scanCancelCmd);

    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("stdio.drv.3d_scan_robot",
              QString::fromUtf8("3D 扫描机器人"),
              "1.0.0",
              QString::fromUtf8("3D 扫描机器人协议基础驱动"))
        .vendor("stdiolink")
        .command(statusCmd)
        .command(testCmd)
        .command(getAddrCmd)
        .command(setAddrCmd)
        .command(getModeCmd)
        .command(setModeCmd)
        .command(getTempCmd)
        .command(getStateCmd)
        .command(getVersionCmd)
        .command(getAnglesCmd)
        .command(getSwitchXCmd)
        .command(getSwitchYCmd)
        .command(getCalibXCmd)
        .command(getCalibYCmd)
        .command(calibCmd)
        .command(calibXCmd)
        .command(calibYCmd)
        .command(moveCmd)
        .command(getDistanceAtCmd)
        .command(getDistanceCmd)
        .command(getRegCmd)
        .command(setRegCmd)
        .command(scanLineCmd)
        .command(scanFrameCmd)
        .command(getDataCmd)
        .command(queryCmd)
        .command(interruptTestCmd)
        .command(scanProgressCmd)
        .command(scanCancelCmd)
        .build();
    stdiolink::meta::ensureCommandExamples(m_meta);
}

// ── 默认 transport 工厂（链接 radar_transport.cpp 中的实现）

extern IRadarTransport* newRadarSerialTransport();

namespace {

IRadarTransport* createSerialTransport() {
    return newRadarSerialTransport();
}

} // namespace
