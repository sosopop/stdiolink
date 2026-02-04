#include <QCoreApplication>
#include <QJsonObject>
#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/stdio_responder.h"

using namespace stdiolink;

class ProgressHandler : public ICommandHandler {
public:
    void handle(const QString& cmd, const QJsonValue& data, IResponder& resp) override {
        if (cmd == "progress") {
            int steps = data.toObject()["steps"].toInt(3);
            for (int i = 1; i <= steps; ++i) {
                resp.event(0, QJsonObject{{"step", i}, {"total", steps}});
            }
            resp.done(0, QJsonObject{});
        } else {
            resp.error(404, QJsonObject{{"message", "unknown command"}});
        }
    }
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    ProgressHandler handler;
    DriverCore core;
    core.setHandler(&handler);
    core.setProfile(DriverCore::Profile::KeepAlive);

    return core.run();
}
