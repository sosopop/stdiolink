#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/stdio_responder.h"
#include <QCoreApplication>
#include <QJsonObject>

using namespace stdiolink;

class EchoHandler : public ICommandHandler {
public:
    void handle(const QString& cmd, const QJsonValue& data, IResponder& resp) override
    {
        if (cmd == "echo") {
            QString msg = data.toObject()["msg"].toString();
            resp.done(0, QJsonObject{{"echo", msg}});
        } else {
            resp.error(404, QJsonObject{{"message", "unknown command"}});
        }
    }
};

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    EchoHandler handler;
    DriverCore core;
    core.setHandler(&handler);
    core.setProfile(DriverCore::Profile::KeepAlive);

    return core.run();
}
