#include <QCoreApplication>

#include "driver_3d_scan_robot/handler.h"
#include "stdiolink/driver/driver_core.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    ThreeDScanRobotHandler handler;
    stdiolink::DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
