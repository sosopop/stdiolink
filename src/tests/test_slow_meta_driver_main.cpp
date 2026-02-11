// Slow meta driver stub for M48 metaTimeoutMs tests.
// Delays the meta response by --meta-delay-ms milliseconds.
#include <QCoreApplication>
#include <QJsonObject>
#include <QThread>

#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class SlowMetaHandler : public IMetaCommandHandler {
public:
    explicit SlowMetaHandler(int delayMs) : m_delayMs(delayMs) {
        m_meta = DriverMetaBuilder()
                     .schemaVersion("1.0.0")
                     .info("slow-meta-driver", "Slow Meta Driver", "1.0.0",
                           "Driver that delays meta response")
                     .entry("test_slow_meta_driver")
                     .command(CommandBuilder("ping")
                                  .description("Ping")
                                  .returns(FieldType::Object))
                     .build();
    }

    const DriverMeta& driverMeta() const override {
        if (m_delayMs > 0) {
            QThread::msleep(m_delayMs);
        }
        return m_meta;
    }

    void handle(const QString& cmd, const QJsonValue&, IResponder& responder) override {
        if (cmd == "ping") {
            responder.done(0, QJsonObject{{"ok", true}});
            return;
        }
        responder.error(404, QJsonObject{{"message", "unknown command"}});
    }

private:
    int m_delayMs = 0;
    mutable DriverMeta m_meta;
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    int delayMs = 0;
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromUtf8(argv[i]);
        if (arg.startsWith("--meta-delay-ms=")) {
            delayMs = arg.mid(16).toInt();
        }
    }

    SlowMetaHandler handler(delayMs);
    DriverCore core;
    core.setMetaHandler(&handler);

    return core.run(argc, argv);
}
