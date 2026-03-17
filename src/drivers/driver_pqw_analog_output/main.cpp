#include <QCoreApplication>

#include "driver_pqw_analog_output/handler.h"
#include "stdiolink/driver/driver_core.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    PqwAnalogOutputHandler handler;
    stdiolink::DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
