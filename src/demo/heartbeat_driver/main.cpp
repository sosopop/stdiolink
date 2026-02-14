/**
 * Heartbeat Driver - KeepAlive + 持续事件流演示
 *
 * 功能演示:
 * 1. KeepAlive 生命周期（持续接收请求）
 * 2. 持续事件流（heartbeat / metrics 事件）
 * 3. 配置注入（intervalMs）
 * 4. 内部状态跟踪（requestsHandled / uptimeMs）
 */

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QThread>

#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class HeartbeatHandler : public IMetaCommandHandler {
public:
    HeartbeatHandler()
    {
        m_uptime.start();
        buildMeta();
    }

    const DriverMeta& driverMeta() const override { return m_meta; }

    void handle(const QString& cmd, const QJsonValue& data,
                IResponder& resp) override
    {
        QJsonObject params = data.toObject();
        ++m_requestsHandled;

        if (cmd == "monitor") {
            handleMonitor(params, resp);
        } else if (cmd == "ping") {
            handlePing(resp);
        } else if (cmd == "status") {
            handleStatus(resp);
        } else {
            resp.error(404, QJsonObject{{"message", "unknown command: " + cmd}});
        }
    }

private:
    void buildMeta();
    void handleMonitor(const QJsonObject& params, IResponder& resp);
    void handlePing(IResponder& resp);
    void handleStatus(IResponder& resp);

    DriverMeta m_meta;
    QElapsedTimer m_uptime;
    int m_requestsHandled = 0;
    int m_intervalMs = 500;
};

void HeartbeatHandler::buildMeta()
{
    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("demo.heartbeat", "Heartbeat Driver", "1.0.0",
              "KeepAlive 心跳监控，演示持续事件流与配置注入")
        .vendor("stdiolink-demo")
        .configField(FieldBuilder("intervalMs", FieldType::Int)
            .description("心跳间隔（毫秒）")
            .defaultValue(500)
            .range(100, 10000))
        .configApply("startupArgs")
        .command(CommandBuilder("monitor")
            .description("持续心跳监控，发送 heartbeat/metrics 事件流")
            .param(FieldBuilder("count", FieldType::Int)
                .required()
                .range(1, 100)
                .description("心跳次数"))
            .param(FieldBuilder("includeMetrics", FieldType::Bool)
                .description("是否包含模拟指标")
                .defaultValue(false))
            .event("heartbeat", "心跳事件 {seq, timestampMs}")
            .event("metrics", "指标事件 {cpu, mem}")
            .returns(FieldType::Object, "完成摘要 {totalBeats, elapsedMs}"))
        .command(CommandBuilder("ping")
            .description("简单 ping/pong 测试")
            .returns(FieldType::Object, "{pong, timestampMs}"))
        .command(CommandBuilder("status")
            .description("返回 driver 运行状态")
            .returns(FieldType::Object, "{requestsHandled, uptimeMs}"))
        .build();
}

void HeartbeatHandler::handleMonitor(const QJsonObject& params, IResponder& resp)
{
    int count = params["count"].toInt(10);
    bool includeMetrics = params["includeMetrics"].toBool(false);

    QElapsedTimer elapsed;
    elapsed.start();

    for (int i = 1; i <= count; ++i) {
        QThread::msleep(m_intervalMs);

        resp.event("heartbeat", 0, QJsonObject{
            {"seq", i},
            {"timestampMs", static_cast<qint64>(QDateTime::currentMSecsSinceEpoch())}
        });

        if (includeMetrics) {
            // 模拟 CPU/内存指标
            double fakeCpu = 10.0 + (i % 5) * 8.5;
            double fakeMem = 45.0 + (i % 3) * 5.2;
            resp.event("metrics", 0, QJsonObject{
                {"cpu", fakeCpu},
                {"mem", fakeMem}
            });
        }
    }

    resp.done(0, QJsonObject{
        {"totalBeats", count},
        {"elapsedMs", static_cast<qint64>(elapsed.elapsed())}
    });
}

void HeartbeatHandler::handlePing(IResponder& resp)
{
    resp.done(0, QJsonObject{
        {"pong", true},
        {"timestampMs", static_cast<qint64>(QDateTime::currentMSecsSinceEpoch())}
    });
}

void HeartbeatHandler::handleStatus(IResponder& resp)
{
    resp.done(0, QJsonObject{
        {"requestsHandled", m_requestsHandled},
        {"uptimeMs", static_cast<qint64>(m_uptime.elapsed())}
    });
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    HeartbeatHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
