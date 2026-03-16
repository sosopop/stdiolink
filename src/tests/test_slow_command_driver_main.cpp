#include <QCoreApplication>
#include <QJsonObject>
#include <QTimer>

#include <cstdlib>

#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"
#include "stdiolink/driver/stdio_responder.h"
#include "stdiolink/guard/force_fast_exit.h"

using namespace stdiolink;
using namespace stdiolink::meta;

namespace {

int delayFrom(const QJsonObject& obj) {
    return obj.value("delayMs").toInt(0);
}

bool emitEventFrom(const QJsonObject& obj) {
    return obj.value("emitEvent").toBool(false);
}

} // namespace

class SlowCommandHandler : public IMetaCommandHandler {
public:
    SlowCommandHandler() {
        m_meta = DriverMetaBuilder()
                     .schemaVersion("1.0.0")
                     .info("slow-command-driver", "Slow Command Driver", "1.0.0",
                           "Driver stub for timeout and event-path tests")
                     .entry("test_slow_command_driver")
                     .command(CommandBuilder("ping").returns(FieldType::Object))
                     .command(CommandBuilder("delayed_done").returns(FieldType::Object))
                     .command(CommandBuilder("delayed_error").returns(FieldType::Object))
                     .command(CommandBuilder("delayed_exit").returns(FieldType::Object))
                     .command(CommandBuilder("delayed_batch").returns(FieldType::Object))
                     .build();
    }

    const DriverMeta& driverMeta() const override {
        return m_meta;
    }

    void handle(const QString& cmd, const QJsonValue& data, IResponder& responder) override {
        const QJsonObject obj = data.toObject();
        if (cmd == "ping") {
            responder.done(0, QJsonObject{{"ok", true}});
            return;
        }

        if (cmd == "delayed_done") {
            auto* resp = new StdioResponder();
            const int delayMs = delayFrom(obj);
            QTimer::singleShot(delayMs, qApp, [resp, delayMs]() {
                resp->done(0, QJsonObject{{"ok", true}, {"delayMs", delayMs}});
                delete resp;
            });
            return;
        }

        if (cmd == "delayed_error") {
            auto* resp = new StdioResponder();
            const int delayMs = delayFrom(obj);
            QTimer::singleShot(delayMs, qApp, [resp, delayMs]() {
                resp->error(500, QJsonObject{{"message", "delayed error"}, {"delayMs", delayMs}});
                delete resp;
            });
            return;
        }

        if (cmd == "delayed_exit") {
            const int delayMs = delayFrom(obj);
            QTimer::singleShot(delayMs, qApp, []() {
                stdiolink::forceFastExit(0);
            });
            return;
        }

        if (cmd == "delayed_batch") {
            auto* resp = new StdioResponder();
            const int delayMs = delayFrom(obj);
            const bool emitEvent = emitEventFrom(obj);
            if (emitEvent) {
                resp->event(QStringLiteral("progress"), 0, QJsonObject{{"step", 1}});
            }
            QTimer::singleShot(delayMs, qApp, [resp, delayMs, emitEvent]() {
                resp->done(0, QJsonObject{{"ok", true}, {"delayMs", delayMs}, {"emitEvent", emitEvent}});
                delete resp;
            });
            return;
        }

        responder.error(404, QJsonObject{{"message", "unknown command"}});
    }

private:
    DriverMeta m_meta;
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    SlowCommandHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);

    return core.run(argc, argv);
}
