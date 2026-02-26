#include "handler.h"
#include "stdiolink/driver/driver_core.h"
#include <QCoreApplication>

using namespace stdiolink;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    ModbusRtuSerialHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
