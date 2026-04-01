#include <QCoreApplication>

#include "driver_opcua/handler.h"
#include "stdiolink/driver/driver_core.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    OpcUaHandler handler;
    stdiolink::DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
