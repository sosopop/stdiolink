#include "driver_3d_temp_scanner/handler.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

#include <memory>

#include "driver_3d_temp_scanner/protocol_codec.h"
#include "driver_3d_temp_scanner/thermal_session.h"
#include "driver_3d_temp_scanner/thermal_transport.h"
#include "stdiolink/driver/meta_builder.h"

using namespace stdiolink;
using namespace stdiolink::meta;
using namespace temp_scanner;

namespace {

constexpr int kErrorTransport = 1;
constexpr int kErrorProtocol = 2;
constexpr int kErrorParam = 3;

void respondInvalidParam(IResponder& responder, const QString& message) {
    responder.error(kErrorParam, QJsonObject{{"message", message}});
}

void respondIoError(IResponder& responder, const QString& message) {
    responder.error(kErrorTransport, QJsonObject{{"message", message}});
}

void respondProtocolError(IResponder& responder, const QString& message) {
    responder.error(kErrorProtocol, QJsonObject{{"message", message}});
}

FieldBuilder deviceAddrParam() {
    return FieldBuilder("device_addr", FieldType::Int)
        .defaultValue(1)
        .range(1, 254)
        .description(QString::fromUtf8("设备地址（1-254）"));
}

FieldBuilder timeoutParam() {
    return FieldBuilder("timeout_ms", FieldType::Int)
        .defaultValue(3000)
        .range(100, 60000)
        .unit("ms")
        .description(QString::fromUtf8("单次收发超时（毫秒）"));
}

FieldBuilder scanTimeoutParam() {
    return FieldBuilder("scan_timeout_ms", FieldType::Int)
        .defaultValue(25000)
        .range(1, 120000)
        .unit("ms")
        .description(QString::fromUtf8("测温整体超时（毫秒）"));
}

FieldBuilder pollIntervalParam() {
    return FieldBuilder("poll_interval_ms", FieldType::Int)
        .defaultValue(1000)
        .range(1, 10000)
        .unit("ms")
        .description(QString::fromUtf8("测温完成状态轮询间隔（毫秒）"));
}

void addCommonParams(CommandBuilder& command) {
    command.param(deviceAddrParam())
        .param(timeoutParam())
        .param(scanTimeoutParam())
        .param(pollIntervalParam())
        .param(FieldBuilder("port_name", FieldType::String)
            .description(QString::fromUtf8("串口名，如 COM3")))
        .param(FieldBuilder("baud_rate", FieldType::Int)
            .defaultValue(115200)
            .description(QString::fromUtf8("串口波特率，默认 115200")))
        .param(FieldBuilder("parity", FieldType::Enum)
            .defaultValue("none")
            .enumValues(QStringList{"none", "odd", "even"})
            .description(QString::fromUtf8("串口校验位")))
        .param(FieldBuilder("stop_bits", FieldType::Enum)
            .defaultValue("1")
            .enumValues(QStringList{"1", "2"})
            .description(QString::fromUtf8("串口停止位")));
}

bool resolveSerialParams(const QJsonObject& params,
                         ThermalTransportParams& result,
                         QString* errorMessage) {
    result.deviceAddr = static_cast<quint8>(params.value("device_addr").toInt(1));
    if (result.deviceAddr < 1 || result.deviceAddr > 254) {
        if (errorMessage) {
            *errorMessage = "device_addr must be 1-254";
        }
        return false;
    }

    result.timeoutMs = params.value("timeout_ms").toInt(3000);
    if (result.timeoutMs < 100 || result.timeoutMs > 60000) {
        if (errorMessage) {
            *errorMessage = "timeout_ms must be 100-60000";
        }
        return false;
    }

    result.scanTimeoutMs = params.value("scan_timeout_ms").toInt(25000);
    if (result.scanTimeoutMs < 1 || result.scanTimeoutMs > 120000) {
        if (errorMessage) {
            *errorMessage = "scan_timeout_ms must be 1-120000";
        }
        return false;
    }

    result.pollIntervalMs = params.value("poll_interval_ms").toInt(1000);
    if (result.pollIntervalMs < 1 || result.pollIntervalMs > 10000) {
        if (errorMessage) {
            *errorMessage = "poll_interval_ms must be 1-10000";
        }
        return false;
    }

    result.portName = params.value("port_name").toString().trimmed();
    if (result.portName.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "port_name is required";
        }
        return false;
    }

    result.baudRate = params.value("baud_rate").toInt(115200);
    if (result.baudRate <= 0) {
        if (errorMessage) {
            *errorMessage = "baud_rate must be > 0";
        }
        return false;
    }

    result.parity = params.value("parity").toString("none").trimmed().toLower();
    if (result.parity != "none" && result.parity != "odd" && result.parity != "even") {
        if (errorMessage) {
            *errorMessage = "parity must be one of none, odd, even";
        }
        return false;
    }

    result.stopBits = params.value("stop_bits").toString("1").trimmed();
    if (result.stopBits != "1" && result.stopBits != "2") {
        if (errorMessage) {
            *errorMessage = "stop_bits must be 1 or 2";
        }
        return false;
    }

    return true;
}

QString inferFormatFromPath(const QString& outputPath) {
    const QString suffix = QFileInfo(outputPath).suffix().trimmed().toLower();
    if (suffix == "json") {
        return "json";
    }
    if (suffix == "csv") {
        return "csv";
    }
    if (suffix == "raw" || suffix == "bin" || suffix == "dat") {
        return "raw";
    }
    if (suffix == "png") {
        return "png";
    }
    return "png";
}

QString resolveOutputFormat(const QJsonObject& params) {
    const QString explicitFormat = params.value("format").toString().trimmed().toLower();
    if (!explicitFormat.isEmpty()) {
        return explicitFormat;
    }
    const QString outputPath = params.value("output").toString().trimmed();
    if (outputPath.isEmpty()) {
        return "png";
    }
    return inferFormatFromPath(outputPath);
}

bool ensureOutputDirectory(const QString& outputPath, QString* errorMessage) {
    const QFileInfo fileInfo(outputPath);
    const QDir dir = fileInfo.dir();
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

QColor colorFromStops(const QVector<QPair<double, QColor>>& stops, double t) {
    if (t <= 0.0) {
        return stops.first().second;
    }
    if (t >= 1.0) {
        return stops.last().second;
    }

    for (int i = 1; i < stops.size(); ++i) {
        if (t <= stops[i].first) {
            const double span = stops[i].first - stops[i - 1].first;
            const double localT = span <= 0.0 ? 0.0 : (t - stops[i - 1].first) / span;
            const QColor left = stops[i - 1].second;
            const QColor right = stops[i].second;
            return QColor(
                static_cast<int>(left.red() + (right.red() - left.red()) * localT),
                static_cast<int>(left.green() + (right.green() - left.green()) * localT),
                static_cast<int>(left.blue() + (right.blue() - left.blue()) * localT));
        }
    }
    return stops.last().second;
}

QImage buildHeatmap(const CaptureResult& result) {
    static const QVector<QPair<double, QColor>> kStops = {
        {0.00, QColor(32, 20, 110)},
        {0.25, QColor(40, 110, 220)},
        {0.50, QColor(40, 190, 140)},
        {0.75, QColor(245, 210, 70)},
        {1.00, QColor(220, 40, 30)},
    };

    QImage image(result.width, result.height, QImage::Format_RGB32);
    const double minTemp = result.minTempDegC;
    const double maxTemp = result.maxTempDegC;
    const double span = maxTemp - minTemp;
    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            const double temp = result.temperaturesDegC[y * result.width + x];
            const double t = span <= 0.0 ? 0.5 : (temp - minTemp) / span;
            image.setPixelColor(x, y, colorFromStops(kStops, t));
        }
    }
    return image.scaled(320, 240, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

bool writePng(const QString& outputPath, const CaptureResult& result, QString* errorMessage) {
    const QImage image = buildHeatmap(result);
    if (image.save(outputPath, "PNG")) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("Failed to save PNG: %1").arg(outputPath);
    }
    return false;
}

bool writeJson(const QString& outputPath, const CaptureResult& result, QString* errorMessage) {
    QJsonArray temperatures;
    for (double temp : result.temperaturesDegC) {
        temperatures.append(temp);
    }

    const QJsonObject root{
        {"width", result.width},
        {"height", result.height},
        {"min_temp_deg_c", result.minTempDegC},
        {"max_temp_deg_c", result.maxTempDegC},
        {"temperatures_deg_c", temperatures}
    };

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open output file: %1").arg(outputPath);
        }
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool writeCsv(const QString& outputPath, const CaptureResult& result, QString* errorMessage) {
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open output file: %1").arg(outputPath);
        }
        return false;
    }

    QTextStream stream(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif
    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            if (x > 0) {
                stream << ",";
            }
            stream << result.temperaturesDegC[y * result.width + x];
        }
        stream << "\n";
    }
    return true;
}

bool writeRaw(const QString& outputPath, const CaptureResult& result, QString* errorMessage) {
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open output file: %1").arg(outputPath);
        }
        return false;
    }

    QByteArray bytes;
    bytes.reserve(result.rawTemperatures.size() * 2);
    for (quint16 rawValue : result.rawTemperatures) {
        bytes.append(static_cast<char>((rawValue >> 8) & 0xFF));
        bytes.append(static_cast<char>(rawValue & 0xFF));
    }
    file.write(bytes);
    return true;
}

bool saveCaptureOutput(const QString& outputPath,
                       const QString& format,
                       const CaptureResult& result,
                       QString* errorMessage) {
    if (!ensureOutputDirectory(outputPath, errorMessage)) {
        return false;
    }
    if (format == "png") {
        return writePng(outputPath, result, errorMessage);
    }
    if (format == "json") {
        return writeJson(outputPath, result, errorMessage);
    }
    if (format == "csv") {
        return writeCsv(outputPath, result, errorMessage);
    }
    if (format == "raw") {
        return writeRaw(outputPath, result, errorMessage);
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("Unsupported format: %1").arg(format);
    }
    return false;
}

QJsonObject captureJson(const CaptureResult& result) {
    QJsonArray temperatures;
    for (double temp : result.temperaturesDegC) {
        temperatures.append(temp);
    }

    return QJsonObject{
        {"width", result.width},
        {"height", result.height},
        {"min_temp_deg_c", result.minTempDegC},
        {"max_temp_deg_c", result.maxTempDegC},
        {"temperatures_deg_c", temperatures},
    };
}

} // namespace

ThreeDTempScannerHandler::ThreeDTempScannerHandler() {
    buildMeta();
    m_transportFactory = []() {
        return temp_scanner::newThermalTransport();
    };
}

void ThreeDTempScannerHandler::setTransportFactory(
    std::function<IThermalTransport*()> factory) {
    m_transportFactory = std::move(factory);
}

void ThreeDTempScannerHandler::handle(const QString& cmd, const QJsonValue& data, IResponder& responder) {
    if (cmd == "status") {
        responder.done(0, QJsonObject{{"status", "ready"}});
        return;
    }

    static const QSet<QString> knownCommands = {
        "capture",
    };
    if (!knownCommands.contains(cmd)) {
        responder.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
        return;
    }

    const QJsonObject params = data.toObject();
    ThermalTransportParams transportParams;
    QString errorMessage;
    if (!resolveSerialParams(params, transportParams, &errorMessage)) {
        respondInvalidParam(responder, errorMessage);
        return;
    }

    std::unique_ptr<IThermalTransport> transport(m_transportFactory());
    // Keep session declared after transport so session is destroyed first and can safely close it.
    ThermalSession session(transport.get());
    if (!session.open(transportParams, &errorMessage)) {
        respondIoError(responder, errorMessage);
        return;
    }

    const QString outputPath = params.value("output").toString().trimmed();
    const QString format = resolveOutputFormat(params);
    if (format != "png" && format != "json" && format != "csv" && format != "raw") {
        respondInvalidParam(responder, "format must be one of png, json, csv, raw");
        return;
    }

    CaptureResult result;
    if (!session.capture(&result, &errorMessage)) {
        respondProtocolError(responder, errorMessage);
        return;
    }

    QJsonObject payload = captureJson(result);
    if (!outputPath.isEmpty()) {
        if (!saveCaptureOutput(outputPath, format, result, &errorMessage)) {
            respondIoError(responder, errorMessage);
            return;
        }
        payload["output"] = outputPath;
        payload["format"] = format;
    }
    responder.done(0, payload);
}

void ThreeDTempScannerHandler::buildMeta() {
    CommandBuilder statusCmd("status");
    statusCmd
        .description(QString::fromUtf8("返回驱动存活状态，固定返回 ready"))
        .returnField(FieldBuilder("result", FieldType::Object)
            .description(QString::fromUtf8("状态结果"))
            .addField(FieldBuilder("status", FieldType::String).description("固定返回 ready")))
        .example("查询驱动状态", QStringList{"stdio", "console"}, QJsonObject{});

    CommandBuilder captureCmd("capture");
    addCommonParams(captureCmd);
    captureCmd
        .description(QString::fromUtf8("按 v3dtemserialpproto 支持范围执行一次测温：写 60000=100 启动测温，轮询 60000 状态，再读取 0..767 温度数据并按 output/format 保存文件"))
        .param(FieldBuilder("output", FieldType::String)
            .description(QString::fromUtf8("可选输出路径；提供时在同一条命令内完成保存")))
        .param(FieldBuilder("format", FieldType::Enum)
            .enumValues(QStringList{"png", "json", "csv", "raw"})
            .description(QString::fromUtf8("输出格式；优先级高于 output 后缀，默认 png")))
        .returnField(FieldBuilder("result", FieldType::Object)
            .description(QString::fromUtf8("测温结果"))
            .addField(FieldBuilder("width", FieldType::Int).description("固定为 32"))
            .addField(FieldBuilder("height", FieldType::Int).description("固定为 24"))
            .addField(FieldBuilder("min_temp_deg_c", FieldType::Double).description("当前帧最小温度"))
            .addField(FieldBuilder("max_temp_deg_c", FieldType::Double).description("当前帧最大温度"))
            .addField(FieldBuilder("temperatures_deg_c", FieldType::Array)
                .description(QString::fromUtf8("32x24 行优先摄氏温度数组"))
                .items(FieldBuilder("temperature", FieldType::Double)))
            .addField(FieldBuilder("output", FieldType::String)
                .description(QString::fromUtf8("实际保存路径，仅提供 output 时返回")))
            .addField(FieldBuilder("format", FieldType::Enum)
                .enumValues(QStringList{"png", "json", "csv", "raw"})
                .description(QString::fromUtf8("实际保存格式，仅提供 output 时返回"))))
        .example("串口测温并保存伪彩 PNG", QStringList{"stdio", "console"},
                 QJsonObject{{"port_name", "COM3"},
                             {"device_addr", 1},
                             {"output", "D:/temp/thermal.png"}})
        .example("串口测温并导出 CSV", QStringList{"stdio", "console"},
                 QJsonObject{{"port_name", "COM3"},
                             {"device_addr", 1},
                             {"output", "D:/temp/thermal.csv"},
                             {"format", "csv"}});

    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("stdio.drv.3d_temp_scanner",
              QString::fromUtf8("3D系统温度扫描仪"),
              "1.0.0",
              QString::fromUtf8("基于 v3dtemserialpproto 串口协议子集的测温热成像驱动，仅支持状态查询与单次温度帧采集"))
        .vendor("stdiolink")
        .profile("oneshot")
        .command(statusCmd)
        .command(captureCmd)
        .build();
}
