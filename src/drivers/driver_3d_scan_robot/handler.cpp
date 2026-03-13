#include "driver_3d_scan_robot/handler.h"

#include <QElapsedTimer>
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
    if (cmd == "get_frame") return 1000000;
    if (cmd == "get_line") return 100000;
    // move, move_dist, calib*, wait, res
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

    // ── 别名映射 ────────────────────────────────────────
    QString resolvedCmd = cmd;
    if (cmd == "test") resolvedCmd = "test_com";
    else if (cmd == "dist") resolvedCmd = "get_dist";
    else if (cmd == "state") resolvedCmd = "get_state";
    else if (cmd == "get_ver") resolvedCmd = "get_fw_ver";
    else if (cmd == "get_dir") resolvedCmd = "get_direction";
    else if (cmd == "gr") resolvedCmd = "get_reg";
    else if (cmd == "sr") resolvedCmd = "set_reg";
    else if (cmd == "rgrt") resolvedCmd = "radar_get_response_time";

    // ── 未知命令早期检测 ────────────────────────────────
    static const QSet<QString> knownCommands = {
        "test_com", "get_addr", "set_addr", "get_mode", "set_mode",
        "get_temp", "get_state", "get_fw_ver", "get_direction",
        "get_sw0", "get_sw1", "get_calib0", "get_calib1",
        "calib", "calib0", "calib1", "move", "move_dist",
        "get_dist", "get_reg", "set_reg",
        "get_line", "get_frame", "get_data",
        "res", "wait",
        "insert_test", "insert_state", "insert_stop",
        "radar_get_response_time",
    };
    if (!knownCommands.contains(resolvedCmd)) {
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

    // ── test_com ────────────────────────────────────────
    if (resolvedCmd == "test_com") {
        quint32 value = static_cast<quint32>(p["value"].toInt(1000));
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::TestCom, makeU32Payload(value), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        if (resp.payload.size() < 4) {
            responder.error(2, QJsonObject{{"message", "Invalid test_com response"}});
            return;
        }
        quint32 fb = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(resp.payload.constData()));
        responder.done(0, QJsonObject{{"echo", static_cast<qint64>(fb)}});
        return;
    }

    // ── get_addr ────────────────────────────────────────
    if (resolvedCmd == "get_addr") {
        // Send test_com to broadcast addr 255 to discover actual address
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

    if (resolvedCmd == "get_mode") {
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

    if (resolvedCmd == "set_mode") {
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

    if (resolvedCmd == "set_addr") {
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

    if (resolvedCmd == "get_temp") {
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

    if (resolvedCmd == "get_state") {
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
    if (resolvedCmd == "get_fw_ver") { handleRegRead(RegId::FirmwareVersion); return; }
    if (resolvedCmd == "get_sw0" || resolvedCmd == "get_sw1") {
        const quint16 regId = resolvedCmd == "get_sw0" ? RegId::XProximitySwitch : RegId::YProximitySwitch;
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
    if (resolvedCmd == "get_calib0" || resolvedCmd == "get_calib1") {
        const quint16 regId = resolvedCmd == "get_calib0" ? RegId::XMotorCalib : RegId::YMotorCalib;
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

    if (resolvedCmd == "get_direction") {
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
    if (resolvedCmd == "get_reg") {
        quint16 regAddr = static_cast<quint16>(p["register"].toInt());
        handleRegRead(regAddr);
        return;
    }

    if (resolvedCmd == "set_reg") {
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

    // ── get_dist ────────────────────────────────────────
    if (resolvedCmd == "get_dist") {
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::GetDistance, makeU32Payload(100), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        if (resp.payload.size() < 4) {
            responder.error(2, QJsonObject{{"message", "Invalid get_dist response"}});
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

    // ── calib / calib0 / calib1 ─────────────────────────
    if (resolvedCmd == "calib" || resolvedCmd == "calib0" || resolvedCmd == "calib1") {
        quint32 calibValue = 300;
        if (resolvedCmd == "calib0") calibValue = 100;
        else if (resolvedCmd == "calib1") calibValue = 200;

        QueryTaskResult tr;
        if (!execLongTaskWithResult(CmdId::Calibration, makeU32Payload(calibValue), resolvedCmd, &tr))
            return;

        if (tr.resultCode == TaskResult::Success)
            responder.done(0, QJsonObject{{"result", "ok"}});
        else
            responder.error(2, QJsonObject{{"result", "error"}, {"result_code", static_cast<qint64>(tr.resultCode)}});
        return;
    }

    // ── move ────────────────────────────────────────────
    if (resolvedCmd == "move") {
        double xDeg = p["x_deg"].toDouble();
        double yDeg = p["y_deg"].toDouble();
        quint16 ax = static_cast<quint16>(qRound(xDeg * 100));
        quint16 ay = static_cast<quint16>(qRound(yDeg * 100));
        quint32 n = (static_cast<quint32>(ax) << 16) | ay;

        QueryTaskResult tr;
        if (!execLongTaskWithResult(CmdId::Move, makeU32Payload(n), resolvedCmd, &tr))
            return;

        if (tr.resultCode == TaskResult::Success)
            responder.done(0, QJsonObject{{"result", "ok"}, {"x_deg", xDeg}, {"y_deg", yDeg}});
        else
            responder.error(2, QJsonObject{{"result", "error"}, {"result_code", static_cast<qint64>(tr.resultCode)}});
        return;
    }

    // ── move_dist ───────────────────────────────────────
    if (resolvedCmd == "move_dist") {
        double xDeg = p["x_deg"].toDouble();
        double yDeg = p["y_deg"].toDouble();
        quint16 ax = static_cast<quint16>(qRound(xDeg * 100));
        quint16 ay = static_cast<quint16>(qRound(yDeg * 100));
        quint32 n = (static_cast<quint32>(ax) << 16) | ay;

        QueryTaskResult tr;
        if (!execLongTaskWithResult(CmdId::MoveDist, makeU32Payload(n), resolvedCmd, &tr))
            return;

        // resultCode contains the distance in mm for move_dist
        responder.done(0, QJsonObject{
            {"x_deg", xDeg}, {"y_deg", yDeg},
            {"distance_mm", static_cast<qint64>(tr.resultCode)}
        });
        return;
    }

    // ── get_line ────────────────────────────────────────
    if (resolvedCmd == "get_line") {
        quint16 angleX     = static_cast<quint16>(qRound(p["angle_x_deg"].toDouble() * 100));
        quint16 beginY     = static_cast<quint16>(qRound(p["begin_y_mm"].toDouble() * 100));
        quint16 endY       = static_cast<quint16>(qRound(p["end_y_mm"].toDouble() * 100));
        quint16 stepY      = static_cast<quint16>(qRound(p["step_y_mm"].toDouble() * 100));
        quint16 sampleCount = static_cast<quint16>(qRound(p["sample_count"].toDouble() * 100));

        QByteArray payload = makeScanLinePayload(angleX, beginY, endY, stepY, sampleCount);

        RadarFrame initResp;
        if (!session.sendAndReceive(CmdId::ScanLine, payload, &initResp, &err)) {
            responder.error(1, QJsonObject{{"message", err}});
            return;
        }

        int taskTimeout = resolveTaskTimeout(tp, resolvedCmd);
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
            {"task_command", "get_line"},
            {"result_code", static_cast<qint64>(scanResult.resultCode)},
            {"segment_count", scanResult.segmentCount},
            {"byte_count", scanResult.byteCount},
            {"data_base64", QString::fromLatin1(scanResult.data.toBase64())}
        });
        return;
    }

    // ── get_frame ───────────────────────────────────────
    if (resolvedCmd == "get_frame") {
        quint16 beginX      = static_cast<quint16>(qRound(p["begin_x_deg"].toDouble() * 100));
        quint16 endX        = static_cast<quint16>(qRound(p["end_x_deg"].toDouble() * 100));
        quint16 stepX       = static_cast<quint16>(qRound(p["step_x_deg"].toDouble() * 100));
        quint16 beginY      = static_cast<quint16>(qRound(p["begin_y_mm"].toDouble() * 100));
        quint16 endY        = static_cast<quint16>(qRound(p["end_y_mm"].toDouble() * 100));
        quint16 stepY       = static_cast<quint16>(qRound(p["step_y_mm"].toDouble() * 100));
        quint16 sampleCount = static_cast<quint16>(qRound(p["sample_count"].toDouble() * 100));

        QByteArray payload = makeScanFramePayload(beginX, endX, stepX, beginY, endY, stepY, sampleCount);

        RadarFrame initResp;
        if (!session.sendAndReceive(CmdId::ScanFrame, payload, &initResp, &err)) {
            responder.error(1, QJsonObject{{"message", err}});
            return;
        }

        int taskTimeout = resolveTaskTimeout(tp, resolvedCmd);
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
            {"task_command", "get_frame"},
            {"result_code", static_cast<qint64>(scanResult.resultCode)},
            {"segment_count", scanResult.segmentCount},
            {"byte_count", scanResult.byteCount},
            {"data_base64", QString::fromLatin1(scanResult.data.toBase64())}
        });
        return;
    }

    // ── get_data ────────────────────────────────────────
    if (resolvedCmd == "get_data") {
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

    // ── res / wait ──────────────────────────────────────
        if (resolvedCmd == "res") {
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::Query, makeU32Payload(100), &resp, &err, false, &errorKind)) {
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

    if (resolvedCmd == "wait") {
        int taskTimeout = resolveTaskTimeout(tp, resolvedCmd);
        // Poll for any completed task
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < taskTimeout) {
            QThread::msleep(static_cast<unsigned long>(tp.queryIntervalMs));
            RadarFrame resp;
            SessionErrorKind errorKind = SessionErrorKind::None;
            if (!session.sendAndReceive(CmdId::Query, makeU32Payload(100), &resp, &err, false, &errorKind))
                continue;
            quint8 lastCtr, lastCmd; quint32 resultCode;
            if (!parseQueryResponse(resp.payload, &lastCtr, &lastCmd, &resultCode))
                continue;
            if (resultCode != TaskResult::StillRunning) {
                responder.done(0, QJsonObject{
                    {"counter", lastCtr}, {"command", lastCmd},
                    {"result", static_cast<qint64>(resultCode)}
                });
                return;
            }
        }
        responder.error(2, QJsonObject{{"message", "Wait timeout"}});
        return;
    }

    // ── 中断式命令 ──────────────────────────────────────
    if (resolvedCmd == "insert_test") {
        quint32 value = static_cast<quint32>(p["value"].toInt(1000));
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(InsertCmdId::Test, makeU32Payload(value), &resp, &err, true, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        if (resp.payload.size() < 4) {
            responder.error(2, QJsonObject{{"message", "Invalid insert_test response"}});
            return;
        }
        quint32 fb = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(resp.payload.constData()));
        responder.done(0, QJsonObject{{"value", static_cast<qint64>(fb)}});
        return;
    }

    if (resolvedCmd == "insert_state") {
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(InsertCmdId::ScanProgress, makeU32Payload(1000), &resp, &err, true, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        if (resp.payload.size() < 4) {
            responder.error(2, QJsonObject{{"message", "Invalid insert_state response"}});
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

    if (resolvedCmd == "insert_stop") {
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(InsertCmdId::ScanCancel, makeU32Payload(1000), &resp, &err, true, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        if (resp.payload.size() < 4) {
            responder.error(2, QJsonObject{{"message", "Invalid insert_stop response"}});
            return;
        }
        quint32 fb = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(resp.payload.constData()));
        responder.done(0, QJsonObject{{"value", static_cast<qint64>(fb)}});
        return;
    }

    // ── radar_get_response_time ─────────────────────────
    if (resolvedCmd == "radar_get_response_time") {
        RadarFrame resp;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session.sendAndReceive(CmdId::RadarGetRespTime, makeU32Payload(100), &resp, &err, false, &errorKind)) {
            respondSessionError(errorKind, err);
            return;
        }
        if (resp.payload.size() < 8) {
            responder.error(2, QJsonObject{{"message", "Invalid radar_get_response_time response"}});
            return;
        }
        const auto* d = reinterpret_cast<const uchar*>(resp.payload.constData());
        quint16 tMin  = qFromBigEndian<quint16>(d);
        quint16 tMax  = qFromBigEndian<quint16>(d + 2);
        quint16 tAve  = qFromBigEndian<quint16>(d + 4);
        quint16 goodCtr = qFromBigEndian<quint16>(d + 6);
        responder.done(0, QJsonObject{
            {"t_min", tMin}, {"t_max", tMax}, {"t_ave", tAve}, {"good_counter", goodCtr}
        });
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

    // test_com / test
    auto testComCmd = CommandBuilder("test_com")
        .description(QString::fromUtf8("测试主协议通信"));
    addConnectionParams(testComCmd);
    testComCmd.param(FieldBuilder("value", FieldType::Int).defaultValue(1000)
        .description(QString::fromUtf8("测试值")));

    auto testAliasCmd = CommandBuilder("test")
        .description(QString::fromUtf8("`test_com` 的兼容别名"));
    addConnectionParams(testAliasCmd);
    testAliasCmd.param(FieldBuilder("value", FieldType::Int).defaultValue(1000)
        .description(QString::fromUtf8("测试值")));

    // get_addr
    auto getAddrCmd = CommandBuilder("get_addr")
        .description(QString::fromUtf8("读取设备地址（广播探测）"));
    addConnectionParams(getAddrCmd);

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

    auto stateAliasCmd = CommandBuilder("state")
        .description(QString::fromUtf8("`get_state` 的兼容别名"));
    addConnectionParams(stateAliasCmd);

    // get_fw_ver
    auto getFwVerCmd = CommandBuilder("get_fw_ver")
        .description(QString::fromUtf8("返回固件版本"));
    addConnectionParams(getFwVerCmd);

    auto getVerAliasCmd = CommandBuilder("get_ver")
        .description(QString::fromUtf8("`get_fw_ver` 的兼容别名"));
    addConnectionParams(getVerAliasCmd);

    // get_direction
    auto getDirectionCmd = CommandBuilder("get_direction")
        .description(QString::fromUtf8("返回 X/Y 当前角度"));
    addConnectionParams(getDirectionCmd);

    auto getDirAliasCmd = CommandBuilder("get_dir")
        .description(QString::fromUtf8("`get_direction` 的兼容别名"));
    addConnectionParams(getDirAliasCmd);

    // get_sw0 / get_sw1
    auto getSw0Cmd = CommandBuilder("get_sw0")
        .description(QString::fromUtf8("返回 X 轴接近开关语义状态"));
    addConnectionParams(getSw0Cmd);

    auto getSw1Cmd = CommandBuilder("get_sw1")
        .description(QString::fromUtf8("返回 Y 轴接近开关语义状态"));
    addConnectionParams(getSw1Cmd);

    // get_calib0 / get_calib1
    auto getCalib0Cmd = CommandBuilder("get_calib0")
        .description(QString::fromUtf8("返回 X 轴校准语义状态"));
    addConnectionParams(getCalib0Cmd);

    auto getCalib1Cmd = CommandBuilder("get_calib1")
        .description(QString::fromUtf8("返回 Y 轴校准语义状态"));
    addConnectionParams(getCalib1Cmd);

    // calib / calib0 / calib1
    auto calibCmd = CommandBuilder("calib")
        .description(QString::fromUtf8("全量校准"));
    addLongTaskParams(calibCmd);

    auto calib0Cmd = CommandBuilder("calib0")
        .description(QString::fromUtf8("X 轴校准"));
    addLongTaskParams(calib0Cmd);

    auto calib1Cmd = CommandBuilder("calib1")
        .description(QString::fromUtf8("Y 轴校准"));
    addLongTaskParams(calib1Cmd);

    // move
    auto moveCmd = CommandBuilder("move")
        .description(QString::fromUtf8("绝对角度移动"));
    addLongTaskParams(moveCmd);
    moveCmd.param(FieldBuilder("x_deg", FieldType::Double).required().range(0, 186)
        .description(QString::fromUtf8("X 角度（°）")));
    moveCmd.param(FieldBuilder("y_deg", FieldType::Double).required().range(0, 186)
        .description(QString::fromUtf8("Y 角度（°）")));

    // move_dist
    auto moveDistCmd = CommandBuilder("move_dist")
        .description(QString::fromUtf8("移动到指定角度并返回该点测距结果"));
    addLongTaskParams(moveDistCmd);
    moveDistCmd.param(FieldBuilder("x_deg", FieldType::Double).required().range(0, 186)
        .description(QString::fromUtf8("X 角度（°）")));
    moveDistCmd.param(FieldBuilder("y_deg", FieldType::Double).required().range(0, 101)
        .description(QString::fromUtf8("Y 角度（°）")));

    // get_dist
    auto getDistCmd = CommandBuilder("get_dist")
        .description(QString::fromUtf8("单点测距"));
    addConnectionParams(getDistCmd);

    auto distAliasCmd = CommandBuilder("dist")
        .description(QString::fromUtf8("`get_dist` 的兼容别名"));
    addConnectionParams(distAliasCmd);

    // get_reg / set_reg
    auto getRegCmd = CommandBuilder("get_reg")
        .description(QString::fromUtf8("原始寄存器读"));
    addConnectionParams(getRegCmd);
    getRegCmd.param(FieldBuilder("register", FieldType::Int).required().range(0, 500)
        .description(QString::fromUtf8("寄存器地址")));

    auto grAliasCmd = CommandBuilder("gr")
        .description(QString::fromUtf8("`get_reg` 的兼容别名"));
    addConnectionParams(grAliasCmd);
    grAliasCmd.param(FieldBuilder("register", FieldType::Int).required().range(0, 500)
        .description(QString::fromUtf8("寄存器地址")));

    auto setRegCmd = CommandBuilder("set_reg")
        .description(QString::fromUtf8("原始寄存器写"));
    addConnectionParams(setRegCmd);
    setRegCmd.param(FieldBuilder("register", FieldType::Int).required().range(0, 500)
        .description(QString::fromUtf8("寄存器地址")));
    setRegCmd.param(FieldBuilder("value", FieldType::Int).required()
        .description(QString::fromUtf8("写入值")));

    auto srAliasCmd = CommandBuilder("sr")
        .description(QString::fromUtf8("`set_reg` 的兼容别名"));
    addConnectionParams(srAliasCmd);
    srAliasCmd.param(FieldBuilder("register", FieldType::Int).required().range(0, 500)
        .description(QString::fromUtf8("寄存器地址")));
    srAliasCmd.param(FieldBuilder("value", FieldType::Int).required()
        .description(QString::fromUtf8("写入值")));

    // get_line
    auto getLineCmd = CommandBuilder("get_line")
        .description(QString::fromUtf8("单线扫描（启动 + 轮询 + 分段聚合）"));
    addLongTaskParams(getLineCmd);
    getLineCmd.param(FieldBuilder("angle_x_deg", FieldType::Double).required()
        .range(0, 186).description(QString::fromUtf8("X 角度（°）")));
    getLineCmd.param(FieldBuilder("begin_y_mm", FieldType::Double).required()
        .range(1, 100).description(QString::fromUtf8("Y 起始（mm）")));
    getLineCmd.param(FieldBuilder("end_y_mm", FieldType::Double).required()
        .range(1, 100).description(QString::fromUtf8("Y 终止（mm）")));
    getLineCmd.param(FieldBuilder("step_y_mm", FieldType::Double).required()
        .range(0.25, 99).description(QString::fromUtf8("Y 步长（mm）")));
    getLineCmd.param(FieldBuilder("sample_count", FieldType::Double).required()
        .range(0.1, 10).description(QString::fromUtf8("采样计数")));

    // get_frame
    auto getFrameCmd = CommandBuilder("get_frame")
        .description(QString::fromUtf8("帧扫描（启动 + 轮询 + 分段聚合）"));
    addLongTaskParams(getFrameCmd);
    getFrameCmd.param(FieldBuilder("begin_x_deg", FieldType::Double).required()
        .range(0, 186).description(QString::fromUtf8("X 起始角度（°）")));
    getFrameCmd.param(FieldBuilder("end_x_deg", FieldType::Double).required()
        .range(0, 186).description(QString::fromUtf8("X 终止角度（°）")));
    getFrameCmd.param(FieldBuilder("step_x_deg", FieldType::Double).required()
        .range(0.01, 186).description(QString::fromUtf8("X 步长（°）")));
    getFrameCmd.param(FieldBuilder("begin_y_mm", FieldType::Double).required()
        .range(1, 151).description(QString::fromUtf8("Y 起始（mm）")));
    getFrameCmd.param(FieldBuilder("end_y_mm", FieldType::Double).required()
        .range(1, 151).description(QString::fromUtf8("Y 终止（mm）")));
    getFrameCmd.param(FieldBuilder("step_y_mm", FieldType::Double).required()
        .range(0.25, 150).description(QString::fromUtf8("Y 步长（mm）")));
    getFrameCmd.param(FieldBuilder("sample_count", FieldType::Double).required()
        .range(0.01, 10).description(QString::fromUtf8("采样计数")));

    // get_data
    auto getDataCmd = CommandBuilder("get_data")
        .description(QString::fromUtf8("按显式 total_bytes 拉取扫描分段数据（oneshot 无状态）"));
    addConnectionParams(getDataCmd);
    getDataCmd.param(connectionParam("inter_command_delay_ms"));
    getDataCmd.param(FieldBuilder("total_bytes", FieldType::Int).required()
        .range(1, 999999).description(QString::fromUtf8("数据总字节数")));

    // res
    auto resCmd = CommandBuilder("res")
        .description(QString::fromUtf8("返回最近一次长任务的执行结果摘要"));
    addConnectionParams(resCmd);

    // wait
    auto waitCmd = CommandBuilder("wait")
        .description(QString::fromUtf8("轮询等待最近一次长任务完成"));
    addLongTaskParams(waitCmd);

    // insert_test
    auto insertTestCmd = CommandBuilder("insert_test")
        .description(QString::fromUtf8("测试中断式通信"));
    addConnectionParams(insertTestCmd);
    insertTestCmd.param(FieldBuilder("value", FieldType::Int).defaultValue(1000)
        .description(QString::fromUtf8("测试值")));

    // insert_state
    auto insertStateCmd = CommandBuilder("insert_state")
        .description(QString::fromUtf8("获取帧扫描状态"));
    addConnectionParams(insertStateCmd);

    // insert_stop
    auto insertStopCmd = CommandBuilder("insert_stop")
        .description(QString::fromUtf8("停止当前帧扫描"));
    addConnectionParams(insertStopCmd);

    // radar_get_response_time
    auto rgrtCmd = CommandBuilder("radar_get_response_time")
        .description(QString::fromUtf8("获取雷达响应时间统计"));
    addConnectionParams(rgrtCmd);

    auto rgrtAliasCmd = CommandBuilder("rgrt")
        .description(QString::fromUtf8("`radar_get_response_time` 的兼容别名"));
    addConnectionParams(rgrtAliasCmd);

    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("stdio.drv.3d_scan_robot",
              QString::fromUtf8("3D 扫描机器人"),
              "1.0.0",
              QString::fromUtf8("3D 扫描机器人协议基础驱动"))
        .vendor("stdiolink")
        .command(statusCmd)
        .command(testComCmd)
        .command(testAliasCmd)
        .command(getAddrCmd)
        .command(setAddrCmd)
        .command(getModeCmd)
        .command(setModeCmd)
        .command(getTempCmd)
        .command(getStateCmd)
        .command(stateAliasCmd)
        .command(getFwVerCmd)
        .command(getVerAliasCmd)
        .command(getDirectionCmd)
        .command(getDirAliasCmd)
        .command(getSw0Cmd)
        .command(getSw1Cmd)
        .command(getCalib0Cmd)
        .command(getCalib1Cmd)
        .command(calibCmd)
        .command(calib0Cmd)
        .command(calib1Cmd)
        .command(moveCmd)
        .command(moveDistCmd)
        .command(getDistCmd)
        .command(distAliasCmd)
        .command(getRegCmd)
        .command(grAliasCmd)
        .command(setRegCmd)
        .command(srAliasCmd)
        .command(getLineCmd)
        .command(getFrameCmd)
        .command(getDataCmd)
        .command(resCmd)
        .command(waitCmd)
        .command(insertTestCmd)
        .command(insertStateCmd)
        .command(insertStopCmd)
        .command(rgrtCmd)
        .command(rgrtAliasCmd)
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
