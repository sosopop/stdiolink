#include "multiscan_driver.h"

#include <QCoreApplication>
#include <stdiolink/driver/driver_core.h>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    stdiolink::DriverCore core;
    MultiscanDriver driver;
    core.setMetaHandler(&driver);
    return core.run(argc, argv);
}
