#include "driver_limaco_5_radar/handler.h"

#include <QJsonArray>
#include <QJsonObject>

#include "driver_limaco_common/limaco_decode.h"
#include "stdiolink/driver/meta_builder.h"

using namespace stdiolink;
using namespace stdiolink::meta;

namespace {

QJsonArray registersToJson(const QVector<uint16_t>& registers) {
    QJsonArray array;
    for (uint16_t value : registers) {
        array.append(static_cast<int>(value));
    }
    return array;
}

QJsonArray pointsToJson(const QVector<limaco_driver::Limaco5PointData>& points) {
    QJsonArray array;
    for (const auto& point : points) {
        array.append(QJsonObject{
            {"index", point.index},
            {"distance_raw", static_cast<int>(point.distanceRaw)},
            {"distance_m", point.distanceMeters},
            {"state", static_cast<int>(point.state)},
            {"valid", point.valid}
        });
    }
    return array;
}

FieldBuilder buildPointsField() {
    return FieldBuilder("points", FieldType::Array)
        .description("5 个测点的原始距离和状态信息")
        .items(
            FieldBuilder("point", FieldType::Object)
                .addField(FieldBuilder("index", FieldType::Int).description("测点序号，1 到 5"))
                .addField(FieldBuilder("distance_raw", FieldType::Int).description("该点原始距离寄存器值"))
                .addField(FieldBuilder("distance_m", FieldType::Double).description("该点距离值，单位米"))
                .addField(FieldBuilder("state", FieldType::Int).description("该点状态寄存器值"))
                .addField(FieldBuilder("valid", FieldType::Bool).description("state 为 0 时为 true"))
                .requiredKeys(QStringList{"index", "distance_raw", "distance_m", "state", "valid"}));
}

} // namespace

Limaco5RadarHandler::Limaco5RadarHandler() {
    buildMeta();
}

void Limaco5RadarHandler::handle(const QString& cmd,
                                 const QJsonValue& data,
                                 IResponder& responder) {
    if (cmd == "status") {
        responder.done(0, QJsonObject{{"status", "ready"}});
        return;
    }

    const QJsonObject params = data.toObject();
    if (cmd == "set_distance_mode") {
        if (!m_transport.writeMultipleRegisters(params, 0x0F0Fu, QVector<uint16_t>{1u}, responder)) {
            return;
        }
        responder.done(0, QJsonObject{
            {"register", 0x0F0F},
            {"written", QJsonArray{1}}
        });
        return;
    }

    if (cmd == "read_distance") {
        QVector<uint16_t> registers;
        if (!m_transport.readHoldingRegisters(params, 0x0A5Au, 15u, registers, responder)) {
            return;
        }

        limaco_driver::Limaco5DistanceData decoded;
        QString errorMessage;
        if (!limaco_driver::decodeLimaco5Distance(registers, decoded, &errorMessage)) {
            responder.error(2, QJsonObject{{"message", errorMessage}});
            return;
        }

        responder.done(0, QJsonObject{
            {"format", decoded.format},
            {"raw_registers", registersToJson(decoded.rawRegisters)},
            {"points", pointsToJson(decoded.points)}
        });
        return;
    }

    responder.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
}

void Limaco5RadarHandler::buildMeta() {
    CommandBuilder setDistanceMode("set_distance_mode");
    limaco_driver::addLimacoConnectionParams(setDistanceMode);
    setDistanceMode
        .description("将五点雷达切换到距离模式")
        .returns(FieldType::Object, "写寄存器结果")
        .returnField(FieldBuilder("register", FieldType::Int).description("固定为 0x0F0F"))
        .returnField(FieldBuilder("written", FieldType::Array)
            .description("实际写入的寄存器值")
            .items(FieldBuilder("value", FieldType::Int)));

    CommandBuilder readDistance("read_distance");
    limaco_driver::addLimacoConnectionParams(readDistance);
    readDistance
        .description("读取利马克五点雷达距离寄存器")
        .returns(FieldType::Object, "一次读取返回 5 个测点结果")
        .returnField(FieldBuilder("format", FieldType::Enum)
            .description("寄存器布局版本: legacy 或 v2")
            .enumValues(QStringList{"legacy", "v2"}))
        .returnField(FieldBuilder("raw_registers", FieldType::Array)
            .description("读取到的 15 个原始保持寄存器")
            .items(FieldBuilder("register", FieldType::Int)))
        .returnField(buildPointsField());

    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("stdio.drv.limaco_5_radar", QString::fromUtf8("利马克五点雷达"), "1.0.0",
              QString::fromUtf8("基于通用 Modbus RTU 传输的利马克五点雷达驱动"))
        .vendor("stdiolink")
        .command(CommandBuilder("status")
            .description("获取驱动状态")
            .returns(FieldType::Object, "状态信息")
            .returnField(FieldBuilder("status", FieldType::String).description("固定返回 ready")))
        .command(setDistanceMode)
        .command(readDistance)
        .build();
}
