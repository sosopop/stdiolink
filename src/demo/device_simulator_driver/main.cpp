/**
 * Device Simulator Driver - 设备模拟演示
 *
 * 功能演示:
 * 1. 枚举类型参数
 * 2. 配置注入演示
 * 3. 高级 UI 提示（分组、条件显示）
 * 4. 配置模式
 */

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>

#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class DeviceSimulatorHandler : public IMetaCommandHandler {
public:
    DeviceSimulatorHandler() { buildMeta(); }

    const DriverMeta& driverMeta() const override { return m_meta; }

    void handle(const QString& cmd, const QJsonValue& data,
                IResponder& resp) override;

private:
    void buildMeta();
    DriverMeta m_meta;
};

void DeviceSimulatorHandler::handle(const QString& cmd,
                                     const QJsonValue& data,
                                     IResponder& resp)
{
    QJsonObject p = data.toObject();
    if (cmd == "connect") {
        QString addr = p["address"].toString();
        resp.done(0, QJsonObject{{"connected", true}, {"address", addr}});
    } else if (cmd == "disconnect") {
        resp.done(0, QJsonObject{{"disconnected", true}});
    } else if (cmd == "read_sensor") {
        QString type = p["sensor_type"].toString();
        double val = QRandomGenerator::global()->bounded(100.0);
        resp.done(0, QJsonObject{{"type", type}, {"value", val}});
    } else if (cmd == "configure") {
        resp.done(0, QJsonObject{{"configured", true}, {"params", p}});
    } else if (cmd == "scan") {
        int count = p["count"].toInt(5);
        for (int i = 0; i < count; ++i) {
            resp.event("device", 0, QJsonObject{
                {"id", i}, {"name", QString("Device_%1").arg(i)}
            });
        }
        resp.done(0, QJsonObject{{"found", count}});
    } else {
        resp.error(404, QJsonObject{{"message", "unknown: " + cmd}});
    }
}

void DeviceSimulatorHandler::buildMeta()
{
    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("demo.device_simulator", "Device Simulator", "1.0.0",
              "设备模拟器，演示枚举和配置注入")
        .vendor("stdiolink-demo")
        .configField(FieldBuilder("timeout", FieldType::Int)
            .defaultValue(5000)
            .unit("ms"))
        .configField(FieldBuilder("debug", FieldType::Bool)
            .defaultValue(false))
        .configApply("startupArgs")
        .command(CommandBuilder("connect")
            .description("连接设备")
            .param(FieldBuilder("address", FieldType::String)
                .required()
                .pattern(R"(^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$)")
                .placeholder("192.168.1.1")
                .group("连接"))
            .param(FieldBuilder("port", FieldType::Int)
                .defaultValue(8080)
                .range(1, 65535)))
        .command(CommandBuilder("disconnect")
            .description("断开连接"))
        .command(CommandBuilder("read_sensor")
            .description("读取传感器")
            .param(FieldBuilder("sensor_type", FieldType::Enum)
                .required()
                .enumValues(QStringList{
                    "temperature", "humidity", "pressure"})))
        .command(CommandBuilder("configure")
            .description("配置设备")
            .param(FieldBuilder("mode", FieldType::Enum)
                .required()
                .enumValues(QStringList{"auto", "manual"}))
            .param(FieldBuilder("interval", FieldType::Int)
                .defaultValue(1000)
                .range(100, 10000)
                .unit("ms")))
        .command(CommandBuilder("scan")
            .description("扫描设备")
            .param(FieldBuilder("count", FieldType::Int)
                .defaultValue(5)
                .range(1, 20))
            .event("device", "发现设备"))
        .build();
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    DeviceSimulatorHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
