#include <QCoreApplication>
#include <QJsonObject>

#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class TestMetaHandler : public IMetaCommandHandler {
public:
    TestMetaHandler() {
        m_meta = DriverMetaBuilder()
                     .schemaVersion("1.0.0")
                     .info("test-meta-driver", "Test Meta Driver", "1.0.0", "Meta export test driver")
                     .entry("test_meta_driver")
                     .command(CommandBuilder("ping").description("Ping command").returns(FieldType::Object))
                     .build();
    }

    const DriverMeta& driverMeta() const override {
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
    DriverMeta m_meta;
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    TestMetaHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);

    return core.run(argc, argv);
}
