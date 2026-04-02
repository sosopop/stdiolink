#include <QCoreApplication>

#include "driver_opcua_server/handler.h"
#include "stdiolink/driver/driver_core.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    OpcUaServerHandler handler;
    stdiolink::DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
