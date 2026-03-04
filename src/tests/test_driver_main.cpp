#include <QCoreApplication>
#include <QJsonObject>
#include <QTimer>
#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/icommand_handler.h"
#include "stdiolink/driver/stdio_responder.h"

using namespace stdiolink;

class TestHandler : public ICommandHandler {
public:
    void handle(const QString& cmd, const QJsonValue& data, IResponder& r) override {
        if (cmd == "echo") {
            r.done(0, data);
        } else if (cmd == "progress") {
            int steps = data.toObject()["steps"].toInt(3);
            for (int i = 1; i <= steps; ++i) {
                r.event(0, QJsonObject{{"step", i}});
            }
            r.done(0, QJsonObject{{"total", steps}});
        } else if (cmd == "timer_echo") {
            // 异步命令：验证事件循环可在无新 stdin 输入时调度定时器回调。
            const int delayMs = data.toObject()["delay"].toInt(100);
            auto* resp = new StdioResponder();
            QTimer::singleShot(delayMs, qApp, [resp, delayMs]() {
                resp->done(0, QJsonObject{{"timer_fired", true}, {"delay", delayMs}});
                delete resp;
            });
        } else if (cmd == "async_event_once") {
            // 先立即 done，再延迟 event，用于验证 keepalive 空闲期异步事件派发。
            r.done(0, QJsonObject{{"scheduled", true}});
            auto* evtResp = new StdioResponder();
            QTimer::singleShot(200, qApp, [evtResp]() {
                evtResp->event(QStringLiteral("tick"), 0,
                               QJsonObject{{"source", "async_event_once"}});
                delete evtResp;
            });
        } else if (cmd == "noop") {
            // 空操作：故意不返回响应，用于 OneShot 多行截断测试。
        } else if (cmd == "exit_now") {
            // Simulate a driver that exits before sending terminal response.
            QCoreApplication::exit(0);
        } else {
            r.error(404, QJsonObject{{"message", "unknown command"}});
        }
    }
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    TestHandler handler;
    DriverCore driver;
    driver.setHandler(&handler);

    return driver.run(argc, argv);
}
