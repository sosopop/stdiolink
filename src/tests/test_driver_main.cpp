#include <QCoreApplication>
#include <QJsonObject>
#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/icommand_handler.h"

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
        } else {
            r.error(404, QJsonObject{{"message", "unknown command"}});
        }
    }
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    DriverCore::Profile profile = DriverCore::Profile::OneShot;
    for (int i = 1; i < argc; ++i) {
        QString arg = argv[i];
        if (arg == "--profile=keepalive") {
            profile = DriverCore::Profile::KeepAlive;
        }
    }

    TestHandler handler;
    DriverCore driver;
    driver.setProfile(profile);
    driver.setHandler(&handler);

    return driver.run();
}
