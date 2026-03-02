#include "multiscan_driver.h"

#include <QJsonArray>
#include <QJsonObject>

using stdiolink::IResponder;
using namespace stdiolink::meta;

MultiscanDriver::MultiscanDriver() {
    buildMeta();
}

const DriverMeta& MultiscanDriver::driverMeta() const {
    return m_meta;
}

void MultiscanDriver::handle(const QString& cmd, const QJsonValue& data, IResponder& resp) {
    if (cmd == "scan_targets") {
        handleScanTargets(data, resp);
        return;
    }
    if (cmd == "configure_channels") {
        handleConfigureChannels(data, resp);
        return;
    }
    resp.error(404, QJsonObject{{"message", QString("unknown command: %1").arg(cmd)}});
}

void MultiscanDriver::handleScanTargets(const QJsonValue& data, IResponder& resp) {
    const QJsonObject obj = data.toObject();
    const QJsonArray targets = obj.value("targets").toArray();
    const QString mode = obj.value("mode").toString("quick");

    if (targets.isEmpty()) {
        resp.error(400, QJsonObject{{"message", "targets must not be empty"}});
        return;
    }

    QJsonArray results;
    for (const auto& item : targets) {
        const QJsonObject target = item.toObject();
        const QString host = target.value("host").toString();
        const int port = target.value("port").toInt();
        const int timeout = target.value("timeout_ms").toInt(2000);

        // 模拟扫描：无实际阻塞等待，latency 仅为数值模拟。
        // 所有 resp.event() 和 resp.done() 在同一调用栈内同步发出，
        // DriverLab 端会看到消息批量到达而非逐条流式到达。
        // 如需演示流式效果，需改为 QTimer/事件循环异步化。
        const int latency = 100; // 固定模拟延迟数值（ms），非阻塞
        const bool reachable = (latency < timeout / 4);

        QJsonObject result{
            {"host", host},
            {"port", port},
            {"status", reachable ? "reachable" : "timeout"},
            {"latency_ms", latency},
        };
        results.append(result);

        resp.event(0, QJsonObject{
                          {"target", host},
                          {"port", port},
                          {"status", result.value("status")},
                      });
    }

    resp.done(0, QJsonObject{
                     {"mode", mode},
                     {"results", results},
                     {"total", targets.size()},
                 });
}

void MultiscanDriver::handleConfigureChannels(const QJsonValue& data, IResponder& resp) {
    const QJsonArray channels = data.toObject().value("channels").toArray();
    if (channels.isEmpty()) {
        resp.error(400, QJsonObject{{"message", "channels must not be empty"}});
        return;
    }

    int configured = 0;
    for (const auto& item : channels) {
        const QJsonObject channel = item.toObject();
        if (!channel.contains("id") || !channel.contains("label")) {
            continue;
        }
        ++configured;
    }

    resp.done(0, QJsonObject{
                     {"configured", configured},
                     {"skipped", channels.size() - configured},
                 });
}

void MultiscanDriver::buildMeta() {
    m_meta = DriverMeta{};
    m_meta.schemaVersion = "1.0";
    m_meta.info.id = "stdio.drv.multiscan";
    m_meta.info.name = "Multiscan Driver";
    m_meta.info.version = "1.0.0";
    m_meta.info.description = "演示 array<object> 参数的扫描与通道配置 Driver";
    m_meta.info.vendor = "stdiolink-demo";

    CommandMeta scanTargets;
    scanTargets.name = "scan_targets";
    scanTargets.description = "对多个目标地址并发扫描，返回各目标响应结果";

    FieldMeta targetsParam;
    targetsParam.name = "targets";
    targetsParam.type = FieldType::Array;
    targetsParam.required = true;
    targetsParam.description = "扫描目标列表（至少 1 个，最多 16 个）";
    targetsParam.constraints.minItems = 1;
    targetsParam.constraints.maxItems = 16;

    auto targetItem = std::make_shared<FieldMeta>();
    targetItem->name = "target";
    targetItem->type = FieldType::Object;
    targetItem->description = "单个扫描目标";

    FieldMeta hostField;
    hostField.name = "host";
    hostField.type = FieldType::String;
    hostField.required = true;
    hostField.description = "目标 IP 或主机名";

    FieldMeta portField;
    portField.name = "port";
    portField.type = FieldType::Int;
    portField.required = true;
    portField.constraints.min = 1;
    portField.constraints.max = 65535;
    portField.description = "端口";

    FieldMeta timeoutField;
    timeoutField.name = "timeout_ms";
    timeoutField.type = FieldType::Int;
    timeoutField.required = false;
    timeoutField.constraints.min = 100;
    timeoutField.constraints.max = 10000;
    timeoutField.defaultValue = QJsonValue(2000);
    timeoutField.description = "超时（ms）";

    targetItem->fields = {hostField, portField, timeoutField};
    targetsParam.items = targetItem;

    FieldMeta modeParam;
    modeParam.name = "mode";
    modeParam.type = FieldType::Enum;
    modeParam.required = false;
    modeParam.description = "扫描模式";
    modeParam.constraints.enumValues = QJsonArray{"quick", "full", "deep"};
    modeParam.defaultValue = "quick";

    scanTargets.params = {targetsParam, modeParam};

    CommandMeta configureChannels;
    configureChannels.name = "configure_channels";
    configureChannels.description = "批量配置数据采集通道";

    FieldMeta channelsParam;
    channelsParam.name = "channels";
    channelsParam.type = FieldType::Array;
    channelsParam.required = true;
    channelsParam.description = "通道配置列表（1–8 条）";
    channelsParam.constraints.minItems = 1;
    channelsParam.constraints.maxItems = 8;

    auto channelItem = std::make_shared<FieldMeta>();
    channelItem->name = "channel";
    channelItem->type = FieldType::Object;

    FieldMeta chIdField;
    chIdField.name = "id";
    chIdField.type = FieldType::Int;
    chIdField.required = true;
    chIdField.description = "通道 ID（0-7）";
    chIdField.constraints.min = 0;
    chIdField.constraints.max = 7;

    FieldMeta chLabelField;
    chLabelField.name = "label";
    chLabelField.type = FieldType::String;
    chLabelField.required = true;
    chLabelField.description = "通道名称";

    FieldMeta chEnabledField;
    chEnabledField.name = "enabled";
    chEnabledField.type = FieldType::Bool;
    chEnabledField.required = false;
    chEnabledField.defaultValue = true;

    FieldMeta chSampleField;
    chSampleField.name = "sample_hz";
    chSampleField.type = FieldType::Int;
    chSampleField.required = false;
    chSampleField.description = "采样频率（Hz）";
    chSampleField.constraints.min = 1;
    chSampleField.constraints.max = 1000;
    chSampleField.defaultValue = 100;

    channelItem->fields = {chIdField, chLabelField, chEnabledField, chSampleField};
    channelsParam.items = channelItem;
    configureChannels.params = {channelsParam};

    m_meta.commands = {scanTargets, configureChannels};
}
