#include "driver_limaco_1_radar/handler.h"

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

FieldBuilder buildReadDistanceReturnPointField() {
    return FieldBuilder("raw_registers", FieldType::Array)
        .description("读取到的 5 个原始保持寄存器")
        .items(FieldBuilder("register", FieldType::Int));
}

} // namespace

Limaco1RadarHandler::Limaco1RadarHandler() {
    buildMeta();
}

void Limaco1RadarHandler::handle(const QString& cmd,
                                 const QJsonValue& data,
                                 IResponder& responder) {
    if (cmd == "status") {
        responder.done(0, QJsonObject{{"status", "ready"}});
        return;
    }

    const QJsonObject params = data.toObject();
    if (cmd == "read_distance") {
        QVector<uint16_t> registers;
        if (!m_transport.readHoldingRegisters(params, 0x0000u, 5u, registers, responder)) {
            return;
        }

        limaco_driver::Limaco1DistanceData decoded;
        QString errorMessage;
        if (!limaco_driver::decodeLimaco1Distance(registers, decoded, &errorMessage)) {
            responder.error(2, QJsonObject{{"message", errorMessage}});
            return;
        }

        responder.done(0, QJsonObject{
            {"raw_registers", registersToJson(decoded.rawRegisters)},
            {"distance_m", decoded.distanceMeters},
            {"status_register", static_cast<int>(decoded.statusRegister)},
            {"distance_valid", decoded.distanceValid}
        });
        return;
    }

    responder.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
}

void Limaco1RadarHandler::buildMeta() {
    CommandBuilder readDistance("read_distance");
    limaco_driver::addLimacoConnectionParams(readDistance);
    readDistance
        .description("读取利马克单点雷达距离寄存器")
        .returns(FieldType::Object, "单次距离读取结果")
        .returnField(buildReadDistanceReturnPointField())
        .returnField(FieldBuilder("distance_m", FieldType::Double)
            .description("按设备协议解码后的距离值，单位米"))
        .returnField(FieldBuilder("status_register", FieldType::Int)
            .description("原始状态寄存器值"))
        .returnField(FieldBuilder("distance_valid", FieldType::Bool)
            .description("状态寄存器小于 50 时为 true"));

    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("stdio.drv.limaco_1_radar", QString::fromUtf8("利马克单点雷达"), "1.0.0",
              QString::fromUtf8("基于通用 Modbus RTU 传输的利马克单点雷达驱动"))
        .vendor("stdiolink")
        .command(CommandBuilder("status")
            .description("获取驱动状态")
            .returns(FieldType::Object, "状态信息")
            .returnField(FieldBuilder("status", FieldType::String).description("固定返回 ready")))
        .command(readDistance)
        .build();
}
