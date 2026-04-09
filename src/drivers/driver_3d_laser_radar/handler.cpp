#include "driver_3d_laser_radar/handler.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSet>

#include <cmath>
#include <memory>

#include "driver_3d_laser_radar/laser_session.h"
#include "driver_3d_laser_radar/laser_transport.h"
#include "driver_3d_laser_radar/protocol_codec.h"
#include "stdiolink/driver/meta_builder.h"

using namespace stdiolink;
using namespace stdiolink::meta;
using namespace laser_radar;

namespace {

constexpr int kErrorTransport = 1;
constexpr int kErrorProtocol = 2;
constexpr int kErrorParam = 3;

void respondInvalidParam(IResponder& responder, const QString& message) {
    responder.error(kErrorParam, QJsonObject{{"message", message}});
}

void respondTransportError(IResponder& responder, const QString& message) {
    responder.error(kErrorTransport, QJsonObject{{"message", message}});
}

void respondProtocolError(IResponder& responder, const QString& message) {
    responder.error(kErrorProtocol, QJsonObject{{"message", message}});
}

int errorCodeForSession(SessionErrorKind kind) {
    return kind == SessionErrorKind::Transport ? kErrorTransport : kErrorProtocol;
}

FieldBuilder hostParam() {
    return FieldBuilder("host", FieldType::String)
        .defaultValue("127.0.0.1")
        .description(QString::fromUtf8("设备 IPv4 地址或主机名"))
        .placeholder("127.0.0.1");
}

FieldBuilder portParam() {
    return FieldBuilder("port", FieldType::Int)
        .defaultValue(23)
        .range(1, 65535)
        .description(QString::fromUtf8("设备 TCP 端口，默认 23"));
}

FieldBuilder timeoutParam() {
    return FieldBuilder("timeout_ms", FieldType::Int)
        .defaultValue(5000)
        .range(100, 300000)
        .unit("ms")
        .description(QString::fromUtf8("单次收发超时（毫秒）"));
}

FieldBuilder taskTimeoutParam() {
    return FieldBuilder("task_timeout_ms", FieldType::Int)
        .defaultValue(-1)
        .unit("ms")
        .description(QString::fromUtf8("长任务总超时，-1 表示使用命令默认值"));
}

FieldBuilder queryIntervalParam() {
    return FieldBuilder("query_interval_ms", FieldType::Int)
        .defaultValue(1000)
        .range(1, 60000)
        .unit("ms")
        .description(QString::fromUtf8("长任务轮询间隔（毫秒）"));
}

FieldBuilder outputParam() {
    return FieldBuilder("output", FieldType::String)
        .required()
        .description(QString::fromUtf8("输出文件路径，驱动会保存原始二进制数据"));
}

void addConnectionParams(CommandBuilder& command) {
    command.param(hostParam())
        .param(portParam())
        .param(timeoutParam());
}

void addLongTaskParams(CommandBuilder& command) {
    addConnectionParams(command);
    command.param(taskTimeoutParam())
        .param(queryIntervalParam());
}

QJsonObject connectionExample(std::initializer_list<QPair<QString, QJsonValue>> extra = {}) {
    QJsonObject params{
        {"host", "127.0.0.1"},
        {"port", 23}
    };
    for (const auto& item : extra) {
        params.insert(item.first, item.second);
    }
    return params;
}

bool ensureOutputDirectory(const QString& outputPath, QString* errorMessage) {
    const QFileInfo info(outputPath);
    const QDir dir = info.dir();
    if (dir.exists()) {
        return true;
    }
    if (QDir().mkpath(dir.absolutePath())) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("Failed to create output directory: %1").arg(dir.absolutePath());
    }
    return false;
}

bool writeBinaryFile(const QString& outputPath, const QByteArray& data, QString* errorMessage) {
    if (!ensureOutputDirectory(outputPath, errorMessage)) {
        return false;
    }
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open output file: %1").arg(outputPath);
        }
        return false;
    }
    if (file.write(data) != data.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to write output file");
        }
        return false;
    }
    return true;
}

bool parseUnsignedParam(const QJsonObject& params, const QString& name, quint32 minValue,
                        quint32 maxValue, quint32* value, QString* errorMessage,
                        bool required = true, quint32 defaultValue = 0) {
    const QJsonValue rawValue = params.value(name);
    if (rawValue.isUndefined()) {
        if (!required) {
            *value = defaultValue;
            return true;
        }
        if (errorMessage) {
            *errorMessage = name + " is required";
        }
        return false;
    }
    if (!rawValue.isDouble()) {
        if (errorMessage) {
            *errorMessage = name + " must be a number";
        }
        return false;
    }
    const double numeric = rawValue.toDouble();
    if (!std::isfinite(numeric) || numeric < static_cast<double>(minValue)
        || numeric > static_cast<double>(maxValue)
        || std::floor(numeric) != numeric) {
        if (errorMessage) {
            *errorMessage = QString("%1 must be an integer in [%2, %3]")
                                .arg(name)
                                .arg(minValue)
                                .arg(maxValue);
        }
        return false;
    }
    *value = static_cast<quint32>(numeric);
    return true;
}

bool parseDoubleParam(const QJsonObject& params, const QString& name, double minValue,
                      double maxValue, double* value, QString* errorMessage,
                      bool required = true, double defaultValue = 0.0) {
    const QJsonValue rawValue = params.value(name);
    if (rawValue.isUndefined()) {
        if (!required) {
            *value = defaultValue;
            return true;
        }
        if (errorMessage) {
            *errorMessage = name + " is required";
        }
        return false;
    }
    if (!rawValue.isDouble()) {
        if (errorMessage) {
            *errorMessage = name + " must be a number";
        }
        return false;
    }
    const double numeric = rawValue.toDouble();
    if (!std::isfinite(numeric) || numeric < minValue || numeric > maxValue) {
        if (errorMessage) {
            *errorMessage = QString("%1 must be in [%2, %3]")
                                .arg(name)
                                .arg(minValue)
                                .arg(maxValue);
        }
        return false;
    }
    *value = numeric;
    return true;
}

qint32 encodeMilliDegrees(double degrees) {
    return static_cast<qint32>(std::llround(degrees * 1000.0));
}

qint32 encodeMicroDegrees(double degrees) {
    return static_cast<qint32>(std::llround(degrees * 1000000.0));
}

QString cancelFeedbackName(quint8 resultCode) {
    switch (resultCode) {
    case CancelFeedback::CanStop: return QStringLiteral("can_stop");
    case CancelFeedback::CannotStop: return QStringLiteral("cannot_stop");
    case CancelFeedback::AlreadyStopped: return QStringLiteral("already_stopped");
    default: return QStringLiteral("unknown");
    }
}

QString queryStateName(quint32 resultA) {
    if (resultA == TaskResult::Idle) {
        return QStringLiteral("idle");
    }
    if (resultA == TaskResult::Running) {
        return QStringLiteral("running");
    }
    if (resultA >= TaskResult::FailedBase && resultA < 3000) {
        return QStringLiteral("failed");
    }
    if (resultA == TaskResult::Cancelled) {
        return QStringLiteral("cancelled");
    }
    if (resultA == TaskResult::Success) {
        return QStringLiteral("success");
    }
    if (resultA == TaskResult::SuccessWithBlankScanline) {
        return QStringLiteral("success_with_blank_scanlines");
    }
    return QStringLiteral("unknown");
}

QJsonObject stateFlagsFromRaw(quint32 rawState) {
    return QJsonObject{
        {"mcu_error", static_cast<bool>(rawState & (1u << 0))},
        {"parameter_storage_error", static_cast<bool>(rawState & (1u << 1))},
        {"ram_error", static_cast<bool>(rawState & (1u << 2))},
        {"flash_error", static_cast<bool>(rawState & (1u << 3))},
        {"x_motor_uncalibrated", static_cast<bool>(rawState & (1u << 5))},
        {"lidar_uncalibrated", static_cast<bool>(rawState & (1u << 6))}
    };
}

bool resolveConnectionParams(const QJsonObject& params, LaserTransportParams& result,
                             QString* errorMessage) {
    result.host = params.value("host").toString("127.0.0.1").trimmed();
    if (result.host.isEmpty()) {
        result.host = QStringLiteral("127.0.0.1");
    }

    quint32 port = 23;
    if (!parseUnsignedParam(params, "port", 1, 65535, &port, errorMessage, false, 23)) {
        return false;
    }
    result.port = static_cast<int>(port);

    quint32 timeoutMs = 5000;
    if (!parseUnsignedParam(params, "timeout_ms", 100, 300000, &timeoutMs, errorMessage, false,
                            5000)) {
        return false;
    }
    result.timeoutMs = static_cast<int>(timeoutMs);

    const QJsonValue taskTimeoutValue = params.value("task_timeout_ms");
    if (taskTimeoutValue.isUndefined()) {
        result.taskTimeoutMs = -1;
    } else if (!taskTimeoutValue.isDouble()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("task_timeout_ms must be a number");
        }
        return false;
    } else {
        const double numeric = taskTimeoutValue.toDouble();
        if (!std::isfinite(numeric) || numeric < -1.0 || numeric > 3600000.0
            || std::floor(numeric) != numeric) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("task_timeout_ms must be an integer in [-1, 3600000]");
            }
            return false;
        }
        result.taskTimeoutMs = static_cast<int>(numeric);
    }

    quint32 queryIntervalMs = 1000;
    if (!parseUnsignedParam(params, "query_interval_ms", 1, 60000, &queryIntervalMs, errorMessage,
                            false, 1000)) {
        return false;
    }
    result.queryIntervalMs = static_cast<int>(queryIntervalMs);
    return true;
}

int resolveTaskTimeout(const LaserTransportParams& params, const QString& commandName) {
    if (params.taskTimeoutMs > 0) {
        return params.taskTimeoutMs;
    }
    if (commandName == "calib_x") {
        return 300000;
    }
    if (commandName == "calib_lidar") {
        return 60000;
    }
    if (commandName == "move_x") {
        return 180000;
    }
    if (commandName == "scan_field") {
        return 600000;
    }
    return 60000;
}

bool resetQueryState(LaserSession& session, IResponder& responder) {
    QueryTaskResult resetResult;
    QString errorMessage;
    SessionErrorKind errorKind = SessionErrorKind::None;
    if (!session.query(QueryOp::Reset, &resetResult, &errorMessage, &errorKind)) {
        responder.error(errorCodeForSession(errorKind), QJsonObject{{"message", errorMessage}});
        return false;
    }
    if (resetResult.lastCounter != 0 || resetResult.lastCommand != 0 || resetResult.resultA != 0
        || resetResult.resultB != 0) {
        responder.error(kErrorProtocol, QJsonObject{{"message", "query reset did not clear state"}});
        return false;
    }
    return true;
}

} // namespace

ThreeDLaserRadarHandler::ThreeDLaserRadarHandler() {
    buildMeta();
    m_transportFactory = []() { return laser_radar::newLaserTransport(); };
}

void ThreeDLaserRadarHandler::setTransportFactory(
    std::function<laser_radar::ILaserTransport*()> factory) {
    m_transportFactory = std::move(factory);
}

void ThreeDLaserRadarHandler::handle(const QString& cmd, const QJsonValue& data,
                                     IResponder& responder) {
    if (cmd == "status") {
        responder.done(0, QJsonObject{{"status", "ready"}});
        return;
    }

    static const QSet<QString> knownCommands = {
        "test", "get_reg", "set_reg", "query", "cancel", "set_imaging_mode", "reboot",
        "get_data", "scan_field", "get_work_mode", "get_device_status", "get_device_code",
        "get_lidar_model_code", "get_distance_unit", "get_uptime_ms", "get_firmware_version",
        "get_data_block_size", "get_x_axis_ratio", "get_transfer_total_bytes", "calib_x",
        "calib_lidar", "move_x"
    };
    if (!knownCommands.contains(cmd)) {
        responder.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
        return;
    }

    const QJsonObject params = data.toObject();
    LaserTransportParams connection;
    QString errorMessage;
    if (!resolveConnectionParams(params, connection, &errorMessage)) {
        respondInvalidParam(responder, errorMessage);
        return;
    }

    auto respondSessionError = [&](SessionErrorKind kind, const QString& message) {
        responder.error(errorCodeForSession(kind), QJsonObject{{"message", message}});
    };

    std::unique_ptr<ILaserTransport> transport;
    std::unique_ptr<LaserSession> session;
    auto ensureSessionOpened = [&]() -> bool {
        if (session) {
            return true;
        }
        transport.reset(m_transportFactory());
        if (!transport) {
            respondTransportError(responder, QStringLiteral("Failed to create laser transport"));
            return false;
        }
        session = std::make_unique<LaserSession>(transport.get());
        QString openError;
        if (!session->open(connection, &openError)) {
            session.reset();
            transport.reset();
            respondTransportError(responder, openError);
            return false;
        }
        return true;
    };

    auto readSemanticRegister = [&](quint16 regId, const QString& fieldName) -> bool {
        if (!ensureSessionOpened()) {
            return false;
        }
        quint32 value = 0;
        SessionErrorKind errorKind = SessionErrorKind::None;
        QString localError;
        if (!session->readRegister(regId, &value, &localError, &errorKind)) {
            respondSessionError(errorKind, localError);
            return false;
        }
        responder.done(0, QJsonObject{{fieldName, static_cast<qint64>(value)}});
        return true;
    };

    if (cmd == "test") {
        quint32 value = 0;
        if (!parseUnsignedParam(params, "value", 0, 0xFFFFFFFFu, &value, &errorMessage, false,
                                0x12345678u)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        if (!ensureSessionOpened()) {
            return;
        }

        LaserFrame response;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session->sendAndReceive(CmdId::Test, makeU32Payload(value), &response, &errorMessage,
                                     &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }
        quint32 echo = 0;
        if (!parseU32Payload(response.payload, &echo)) {
            respondProtocolError(responder, "Invalid test response");
            return;
        }
        if (echo != ~value) {
            respondProtocolError(responder, "Device test echo mismatch");
            return;
        }
        responder.done(0, QJsonObject{
            {"value", static_cast<qint64>(value)},
            {"echo", static_cast<qint64>(echo)}
        });
        return;
    }

    if (cmd == "get_reg") {
        quint32 regId = 0;
        if (!parseUnsignedParam(params, "register", 1, 500, &regId, &errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        if (!ensureSessionOpened()) {
            return;
        }
        quint32 value = 0;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session->readRegister(static_cast<quint16>(regId), &value, &errorMessage,
                                   &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{
            {"register", static_cast<int>(regId)},
            {"value", static_cast<qint64>(value)}
        });
        return;
    }

    if (cmd == "set_reg") {
        quint32 regId = 0;
        quint32 value = 0;
        if (!parseUnsignedParam(params, "register", 1, 500, &regId, &errorMessage)
            || !parseUnsignedParam(params, "value", 0, 0xFFFFFFFFu, &value, &errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        if (!ensureSessionOpened()) {
            return;
        }
        quint32 echoedValue = 0;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session->writeRegister(static_cast<quint16>(regId), value, &echoedValue,
                                    &errorMessage, &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{
            {"register", static_cast<int>(regId)},
            {"value", static_cast<qint64>(echoedValue)}
        });
        return;
    }

    if (cmd == "query") {
        quint32 op = QueryOp::Read;
        if (!parseUnsignedParam(params, "op", QueryOp::Read, QueryOp::Reset, &op, &errorMessage,
                                false, QueryOp::Read)
            || (op != QueryOp::Read && op != QueryOp::Reset)) {
            respondInvalidParam(responder, "op must be 100 or 200");
            return;
        }
        if (!ensureSessionOpened()) {
            return;
        }
        QueryTaskResult result;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session->query(op, &result, &errorMessage, &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{
            {"counter", static_cast<int>(result.lastCounter)},
            {"command", static_cast<int>(result.lastCommand)},
            {"command_name", commandName(result.lastCommand)},
            {"result_a", static_cast<qint64>(result.resultA)},
            {"result_b", static_cast<qint64>(result.resultB)},
            {"state", queryStateName(result.resultA)}
        });
        return;
    }

    if (cmd == "cancel") {
        if (!ensureSessionOpened()) {
            return;
        }
        quint16 lastCounter = 0;
        quint8 lastCommand = 0;
        quint8 resultCode = 0;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session->cancel(&lastCounter, &lastCommand, &resultCode, &errorMessage,
                             &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{
            {"counter", static_cast<int>(lastCounter)},
            {"command", static_cast<int>(lastCommand)},
            {"command_name", commandName(lastCommand)},
            {"result_code", static_cast<int>(resultCode)},
            {"result", cancelFeedbackName(resultCode)}
        });
        return;
    }

    if (cmd == "set_imaging_mode" || cmd == "reboot") {
        if (!ensureSessionOpened()) {
            return;
        }
        const quint8 command = cmd == "set_imaging_mode" ? CmdId::SetImagingMode : CmdId::Reboot;
        LaserFrame response;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session->sendAndReceive(command, makeU32Payload(100), &response, &errorMessage,
                                     &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }
        quint32 feedback = 0;
        if (!parseU32Payload(response.payload, &feedback)) {
            respondProtocolError(responder, "Invalid command response");
            return;
        }
        if (feedback != ImmediateFeedback::Accepted) {
            respondProtocolError(responder, QString("%1 rejected by device").arg(cmd));
            return;
        }
        responder.done(0, QJsonObject{
            {"feedback_code", static_cast<qint64>(feedback)},
            {"feedback", QStringLiteral("accepted")}
        });
        return;
    }

    if (cmd == "get_data") {
        quint32 segmentIndex = 0;
        if (!parseUnsignedParam(params, "segment_index", 0, 0xFFFFFFFFu, &segmentIndex,
                                &errorMessage)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        const QString outputPath = params.value("output").toString().trimmed();
        if (outputPath.isEmpty()) {
            respondInvalidParam(responder, "output is required");
            return;
        }
        if (!ensureSessionOpened()) {
            return;
        }

        quint32 segmentCount = 0;
        QByteArray segmentData;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session->readSegment(segmentIndex, &segmentCount, &segmentData, &errorMessage,
                                  &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }
        if (!writeBinaryFile(outputPath, segmentData, &errorMessage)) {
            respondTransportError(responder, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{
            {"segment_index", static_cast<qint64>(segmentIndex)},
            {"segment_count", static_cast<qint64>(segmentCount)},
            {"segment_length", segmentData.size()},
            {"output", outputPath}
        });
        return;
    }

    if (cmd == "scan_field") {
        const QString outputPath = params.value("output").toString().trimmed();
        if (outputPath.isEmpty()) {
            respondInvalidParam(responder, "output is required");
            return;
        }

        double beginXDeg = 0.0;
        double endXDeg = 0.0;
        double stepXDeg = 0.0;
        double beginYDeg = 0.0;
        double endYDeg = 0.0;
        double stepYDeg = 0.0;
        if (!parseDoubleParam(params, "begin_x_deg", 0.0, 190.0, &beginXDeg, &errorMessage)
            || !parseDoubleParam(params, "end_x_deg", 0.0, 360.0, &endXDeg, &errorMessage)
            || !parseDoubleParam(params, "step_x_deg", 0.0, 190.0, &stepXDeg, &errorMessage,
                                 false, 0.0)
            || !parseDoubleParam(params, "begin_y_deg", -5.0, 185.0, &beginYDeg, &errorMessage,
                                 false, 0.0)
            || !parseDoubleParam(params, "end_y_deg", -5.0, 360.0, &endYDeg, &errorMessage,
                                 false, 0.0)
            || !parseDoubleParam(params, "step_y_deg", 0.0, 190.0, &stepYDeg, &errorMessage,
                                 false, 0.0)) {
            respondInvalidParam(responder, errorMessage);
            return;
        }
        if (endXDeg < beginXDeg) {
            respondInvalidParam(responder, "end_x_deg must be >= begin_x_deg");
            return;
        }
        if ((beginYDeg != 0.0 || endYDeg != 0.0) && endYDeg < beginYDeg) {
            respondInvalidParam(responder, "end_y_deg must be >= begin_y_deg");
            return;
        }
        if (!ensureSessionOpened()) {
            return;
        }

        if (!resetQueryState(*session, responder)) {
            return;
        }

        quint16 taskCounter = 0;
        const QByteArray payload = makeScanFieldPayload(
            encodeMilliDegrees(beginXDeg),
            encodeMilliDegrees(endXDeg),
            encodeMicroDegrees(stepXDeg),
            encodeMilliDegrees(beginYDeg),
            encodeMilliDegrees(endYDeg),
            encodeMicroDegrees(stepYDeg));
        if (!session->sendOnly(CmdId::ScanField, payload, &taskCounter, &errorMessage)) {
            respondTransportError(responder, errorMessage);
            return;
        }

        QueryTaskResult taskResult;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session->waitTaskCompleted(taskCounter, CmdId::ScanField,
                                        resolveTaskTimeout(connection, cmd), &taskResult,
                                        &errorMessage, &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }
        if (taskResult.resultA >= TaskResult::FailedBase && taskResult.resultA < 3000) {
            responder.error(kErrorProtocol, QJsonObject{
                {"message", "scan_field failed"},
                {"result_a", static_cast<qint64>(taskResult.resultA)},
                {"result_b", static_cast<qint64>(taskResult.resultB)},
                {"error_code", static_cast<qint64>(taskResult.resultA - TaskResult::FailedBase)}
            });
            return;
        }
        if (taskResult.resultA != TaskResult::Success
            && taskResult.resultA != TaskResult::SuccessWithBlankScanline) {
            responder.error(kErrorProtocol, QJsonObject{
                {"message", "scan_field did not finish successfully"},
                {"result_a", static_cast<qint64>(taskResult.resultA)},
                {"result_b", static_cast<qint64>(taskResult.resultB)}
            });
            return;
        }

        ScanAggregateResult aggregate;
        if (!session->collectAllSegments(&aggregate, &errorMessage, &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }
        if (taskResult.resultB > 0 && aggregate.byteCount != static_cast<int>(taskResult.resultB)) {
            respondProtocolError(responder, "Aggregated byte count does not match query result");
            return;
        }
        if (!writeBinaryFile(outputPath, aggregate.data, &errorMessage)) {
            respondTransportError(responder, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{
            {"task_counter", static_cast<int>(taskCounter)},
            {"task_command", QStringLiteral("scan_field")},
            {"result_a", static_cast<qint64>(taskResult.resultA)},
            {"result_b", static_cast<qint64>(taskResult.resultB)},
            {"segment_count", aggregate.segmentCount},
            {"byte_count", aggregate.byteCount},
            {"has_blank_scanlines", taskResult.resultA == TaskResult::SuccessWithBlankScanline},
            {"output", outputPath}
        });
        return;
    }

    if (cmd == "calib_x" || cmd == "calib_lidar" || cmd == "move_x") {
        quint16 taskCounter = 0;
        quint8 taskCommand = CmdId::CalibX;
        QByteArray payload;
        if (cmd == "calib_x") {
            taskCommand = CmdId::CalibX;
            payload = makeU32Payload(100);
        } else if (cmd == "calib_lidar") {
            taskCommand = CmdId::CalibLidar;
            payload = makeU32Payload(100);
        } else {
            taskCommand = CmdId::MoveX;
            double angleDeg = 0.0;
            if (!parseDoubleParam(params, "angle_deg", 0.0, 190.0, &angleDeg, &errorMessage)) {
                respondInvalidParam(responder, errorMessage);
                return;
            }
            payload = makeU32Payload(static_cast<quint32>(encodeMilliDegrees(angleDeg)));
        }
        if (!ensureSessionOpened()) {
            return;
        }
        if (!resetQueryState(*session, responder)) {
            return;
        }

        if (!session->sendOnly(taskCommand, payload, &taskCounter, &errorMessage)) {
            respondTransportError(responder, errorMessage);
            return;
        }

        QueryTaskResult taskResult;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session->waitTaskCompleted(taskCounter, taskCommand,
                                        resolveTaskTimeout(connection, cmd), &taskResult,
                                        &errorMessage, &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }

        if (taskResult.resultA == TaskResult::Success) {
            responder.done(0, QJsonObject{
                {"task_counter", static_cast<int>(taskCounter)},
                {"task_command", commandName(taskCommand)},
                {"result_a", static_cast<qint64>(taskResult.resultA)},
                {"result_b", static_cast<qint64>(taskResult.resultB)},
                {"state", queryStateName(taskResult.resultA)}
            });
            return;
        }

        const bool hasErrorCode =
            taskResult.resultA >= TaskResult::FailedBase && taskResult.resultA < 3000;
        responder.error(kErrorProtocol, QJsonObject{
            {"message", QString("%1 failed").arg(cmd)},
            {"task_counter", static_cast<int>(taskCounter)},
            {"result_a", static_cast<qint64>(taskResult.resultA)},
            {"result_b", static_cast<qint64>(taskResult.resultB)},
            {"state", queryStateName(taskResult.resultA)},
            {"error_code", hasErrorCode
                               ? static_cast<qint64>(taskResult.resultA - TaskResult::FailedBase)
                               : 0}
        });
        return;
    }

    if (cmd == "get_work_mode") {
        if (!ensureSessionOpened()) {
            return;
        }
        quint32 value = 0;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session->readRegister(RegId::WorkMode, &value, &errorMessage, &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{
            {"mode_code", static_cast<qint64>(value)},
            {"mode", workModeName(value)}
        });
        return;
    }

    if (cmd == "get_device_status") {
        if (!ensureSessionOpened()) {
            return;
        }
        quint32 value = 0;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session->readRegister(RegId::DeviceStatus, &value, &errorMessage, &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{
            {"raw", static_cast<qint64>(value)},
            {"flags", stateFlagsFromRaw(value)}
        });
        return;
    }

    if (cmd == "get_device_code") {
        readSemanticRegister(RegId::DeviceCode, "device_code");
        return;
    }
    if (cmd == "get_uptime_ms") {
        readSemanticRegister(RegId::TimeSinceBoot, "uptime_ms");
        return;
    }
    if (cmd == "get_firmware_version") {
        readSemanticRegister(RegId::FirmwareVersion, "firmware_version");
        return;
    }
    if (cmd == "get_data_block_size") {
        readSemanticRegister(RegId::DataBlockSize, "data_block_size");
        return;
    }
    if (cmd == "get_transfer_total_bytes") {
        readSemanticRegister(RegId::TotalBytesForTransfer, "total_bytes");
        return;
    }

    if (cmd == "get_lidar_model_code") {
        if (!ensureSessionOpened()) {
            return;
        }
        quint32 value = 0;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session->readRegister(RegId::LidarModelCode, &value, &errorMessage, &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{
            {"model_code", static_cast<qint64>(value)},
            {"model_name", value == 2100 ? QStringLiteral("KS2100") : QStringLiteral("unknown")}
        });
        return;
    }

    if (cmd == "get_distance_unit") {
        if (!ensureSessionOpened()) {
            return;
        }
        quint32 value = 0;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session->readRegister(RegId::DistanceUnit, &value, &errorMessage, &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{
            {"unit_nm", static_cast<qint64>(value)},
            {"unit_mm", static_cast<double>(value) / 1000000.0}
        });
        return;
    }

    if (cmd == "get_x_axis_ratio") {
        if (!ensureSessionOpened()) {
            return;
        }
        quint32 value = 0;
        SessionErrorKind errorKind = SessionErrorKind::None;
        if (!session->readRegister(RegId::XAxisRatio, &value, &errorMessage, &errorKind)) {
            respondSessionError(errorKind, errorMessage);
            return;
        }
        responder.done(0, QJsonObject{
            {"raw_ratio", static_cast<qint64>(value)},
            {"ratio", static_cast<double>(value) / 100000.0}
        });
        return;
    }

    responder.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
}

void ThreeDLaserRadarHandler::buildMeta() {
    CommandBuilder statusCmd("status");
    statusCmd.description(QString::fromUtf8("返回驱动存活状态，固定返回 ready"));

    CommandBuilder testCmd("test");
    addConnectionParams(testCmd);
    testCmd
        .description(QString::fromUtf8("测试设备通信，发送任意 32 位数并校验返回值是否为按位取反"))
        .param(FieldBuilder("value", FieldType::Int)
            .defaultValue(305419896)
            .description(QString::fromUtf8("测试值，默认 0x12345678")));

    CommandBuilder getRegCmd("get_reg");
    addConnectionParams(getRegCmd);
    getRegCmd
        .description(QString::fromUtf8("按协议 1 号指令读取原始寄存器"))
        .param(FieldBuilder("register", FieldType::Int).required().range(1, 500));

    CommandBuilder setRegCmd("set_reg");
    addConnectionParams(setRegCmd);
    setRegCmd
        .description(QString::fromUtf8("按协议 2 号指令写入原始寄存器"))
        .param(FieldBuilder("register", FieldType::Int).required().range(1, 500))
        .param(FieldBuilder("value", FieldType::Int).required());

    CommandBuilder queryCmd("query");
    addConnectionParams(queryCmd);
    queryCmd
        .description(QString::fromUtf8("查询或重置最近一次长任务执行结果"))
        .param(FieldBuilder("op", FieldType::Int)
            .defaultValue(100)
            .description(QString::fromUtf8("100=查询，200=重置")));

    CommandBuilder cancelCmd("cancel");
    addConnectionParams(cancelCmd);
    cancelCmd.description(QString::fromUtf8("发送 5 号指令停止当前执行中的长任务"));

    CommandBuilder setImagingModeCmd("set_imaging_mode");
    addConnectionParams(setImagingModeCmd);
    setImagingModeCmd.description(QString::fromUtf8("发送 15 号指令切换到成像模式"));

    CommandBuilder rebootCmd("reboot");
    addConnectionParams(rebootCmd);
    rebootCmd.description(QString::fromUtf8("发送 16 号指令重启设备"));

    CommandBuilder getDataCmd("get_data");
    addConnectionParams(getDataCmd);
    getDataCmd
        .description(QString::fromUtf8("按 12 号指令获取指定分段，并将该分段原始字节写入 output"))
        .param(FieldBuilder("segment_index", FieldType::Int).required().range(0, 2147483647))
        .param(outputParam());

    CommandBuilder scanFieldCmd("scan_field");
    addLongTaskParams(scanFieldCmd);
    scanFieldCmd
        .description(QString::fromUtf8("按 11 号指令获取一个扫描场，成功后自动拉取全部分段并保存原始字节流"))
        .param(FieldBuilder("begin_x_deg", FieldType::Double).required().range(0, 190))
        .param(FieldBuilder("end_x_deg", FieldType::Double).required().range(0, 360))
        .param(FieldBuilder("step_x_deg", FieldType::Double)
            .defaultValue(0)
            .description(QString::fromUtf8("X 轴分辨率，单位 deg；0 表示让设备使用 KS2100 固定值")))
        .param(FieldBuilder("begin_y_deg", FieldType::Double)
            .defaultValue(0)
            .description(QString::fromUtf8("Y 轴起始角度，单位 deg；0 表示使用设备固定值")))
        .param(FieldBuilder("end_y_deg", FieldType::Double)
            .defaultValue(0)
            .range(-5, 360)
            .description(QString::fromUtf8("Y 轴结束角度，单位 deg；0 表示使用设备固定值")))
        .param(FieldBuilder("step_y_deg", FieldType::Double)
            .defaultValue(0)
            .description(QString::fromUtf8("Y 轴分辨率，单位 deg；0 表示使用设备固定值")))
        .param(outputParam());

    CommandBuilder calibXCmd("calib_x");
    addLongTaskParams(calibXCmd);
    calibXCmd.description(QString::fromUtf8("发送 4 号指令校准 X 轴电机，并轮询查询结果"));

    CommandBuilder calibLidarCmd("calib_lidar");
    addLongTaskParams(calibLidarCmd);
    calibLidarCmd.description(QString::fromUtf8("发送 8 号指令校准激光雷达，并轮询查询结果"));

    CommandBuilder moveXCmd("move_x");
    addLongTaskParams(moveXCmd);
    moveXCmd
        .description(QString::fromUtf8("发送 7 号指令，让 X 轴移动到指定线角度"))
        .param(FieldBuilder("angle_deg", FieldType::Double).required().range(0, 190));

    CommandBuilder getWorkModeCmd("get_work_mode");
    addConnectionParams(getWorkModeCmd);
    getWorkModeCmd.description(QString::fromUtf8("读取寄存器 1：设备工作模式"));

    CommandBuilder getDeviceStatusCmd("get_device_status");
    addConnectionParams(getDeviceStatusCmd);
    getDeviceStatusCmd.description(QString::fromUtf8("读取寄存器 2：设备状态位掩码"));

    CommandBuilder getDeviceCodeCmd("get_device_code");
    addConnectionParams(getDeviceCodeCmd);
    getDeviceCodeCmd.description(QString::fromUtf8("读取寄存器 3：设备代码"));

    CommandBuilder getLidarModelCodeCmd("get_lidar_model_code");
    addConnectionParams(getLidarModelCodeCmd);
    getLidarModelCodeCmd.description(QString::fromUtf8("读取寄存器 4：内部激光雷达型号代码"));

    CommandBuilder getDistanceUnitCmd("get_distance_unit");
    addConnectionParams(getDistanceUnitCmd);
    getDistanceUnitCmd.description(QString::fromUtf8("读取寄存器 5：测距单位（纳米）"));

    CommandBuilder getUptimeCmd("get_uptime_ms");
    addConnectionParams(getUptimeCmd);
    getUptimeCmd.description(QString::fromUtf8("读取寄存器 6：设备上电后运行时间（毫秒）"));

    CommandBuilder getFirmwareVersionCmd("get_firmware_version");
    addConnectionParams(getFirmwareVersionCmd);
    getFirmwareVersionCmd.description(QString::fromUtf8("读取寄存器 14：固件版本号"));

    CommandBuilder getDataBlockSizeCmd("get_data_block_size");
    addConnectionParams(getDataBlockSizeCmd);
    getDataBlockSizeCmd.description(QString::fromUtf8("读取寄存器 18：分段传输数据块字节数"));

    CommandBuilder getXAxisRatioCmd("get_x_axis_ratio");
    addConnectionParams(getXAxisRatioCmd);
    getXAxisRatioCmd.description(QString::fromUtf8("读取寄存器 22：X 轴电机传动比"));

    CommandBuilder getTransferTotalBytesCmd("get_transfer_total_bytes");
    addConnectionParams(getTransferTotalBytesCmd);
    getTransferTotalBytesCmd.description(QString::fromUtf8("读取寄存器 40：当前可分段传输数据的总字节数"));

    statusCmd.example("查询驱动状态", QStringList{"stdio", "console"}, QJsonObject{});
    testCmd.example("测试设备通信", QStringList{"stdio", "console"},
                    connectionExample({{"value", 305419896}}));
    getRegCmd.example("读取原始寄存器 1", QStringList{"stdio", "console"},
                      connectionExample({{"register", 1}}));
    setRegCmd.example("写入工作模式寄存器为 20", QStringList{"stdio", "console"},
                      connectionExample({{"register", 1}, {"value", 20}}));
    queryCmd.example("查询最近一次长任务结果", QStringList{"stdio", "console"},
                     connectionExample({{"op", 100}}));
    queryCmd.example("清空最近一次长任务结果", QStringList{"stdio", "console"},
                     connectionExample({{"op", 200}}));
    cancelCmd.example("停止当前长任务", QStringList{"stdio", "console"}, connectionExample());
    setImagingModeCmd.example("切换到成像模式", QStringList{"stdio", "console"},
                              connectionExample());
    rebootCmd.example("重启设备", QStringList{"stdio", "console"}, connectionExample());
    getDataCmd.example("读取第 0 段原始数据", QStringList{"stdio", "console"},
                       connectionExample(
                           {{"segment_index", 0}, {"output", "d:\\temp\\segment_0000.raw"}}));
    scanFieldCmd.example("采集扫描场并保存原始数据", QStringList{"stdio", "console"},
                         connectionExample({{"begin_x_deg", 0.0},
                                            {"end_x_deg", 190.0},
                                            {"step_x_deg", 0.125},
                                            {"output", "d:\\temp\\output.raw"}}));
    calibXCmd.example("校准 X 轴", QStringList{"stdio", "console"}, connectionExample());
    calibLidarCmd.example("校准激光雷达", QStringList{"stdio", "console"},
                          connectionExample());
    moveXCmd.example("移动 X 轴到 90 度", QStringList{"stdio", "console"},
                     connectionExample({{"angle_deg", 90.0}}));
    getWorkModeCmd.example("读取工作模式", QStringList{"stdio", "console"}, connectionExample());
    getDeviceStatusCmd.example("读取设备状态", QStringList{"stdio", "console"},
                               connectionExample());
    getDeviceCodeCmd.example("读取设备编码", QStringList{"stdio", "console"},
                             connectionExample());
    getLidarModelCodeCmd.example("读取雷达型号编码", QStringList{"stdio", "console"},
                                 connectionExample());
    getDistanceUnitCmd.example("读取测距单位", QStringList{"stdio", "console"},
                               connectionExample());
    getUptimeCmd.example("读取上电运行时间", QStringList{"stdio", "console"},
                         connectionExample());
    getFirmwareVersionCmd.example("读取固件版本", QStringList{"stdio", "console"},
                                  connectionExample());
    getDataBlockSizeCmd.example("读取数据分段块大小", QStringList{"stdio", "console"},
                                connectionExample());
    getXAxisRatioCmd.example("读取 X 轴传动比", QStringList{"stdio", "console"},
                             connectionExample());
    getTransferTotalBytesCmd.example("读取当前可传输总字节数", QStringList{"stdio", "console"},
                                     connectionExample());

    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("stdio.drv.3d_laser_radar",
              QString::fromUtf8("三维激光雷达"),
              "1.0.0",
              QString::fromUtf8("基于三维激光雷达 TCP LIDA 协议的 one-shot 驱动，覆盖寄存器、长任务查询和扫描场原始数据导出"))
        .vendor("stdiolink")
        .profile("oneshot")
        .command(statusCmd)
        .command(testCmd)
        .command(getRegCmd)
        .command(setRegCmd)
        .command(queryCmd)
        .command(cancelCmd)
        .command(setImagingModeCmd)
        .command(rebootCmd)
        .command(getDataCmd)
        .command(scanFieldCmd)
        .command(getWorkModeCmd)
        .command(getDeviceStatusCmd)
        .command(getDeviceCodeCmd)
        .command(getLidarModelCodeCmd)
        .command(getDistanceUnitCmd)
        .command(getUptimeCmd)
        .command(getFirmwareVersionCmd)
        .command(getDataBlockSizeCmd)
        .command(getXAxisRatioCmd)
        .command(getTransferTotalBytesCmd)
        .command(calibXCmd)
        .command(calibLidarCmd)
        .command(moveXCmd)
        .build();
}
