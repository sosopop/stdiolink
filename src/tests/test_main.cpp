#include <cstdlib>
#include <QCoreApplication>
#include <QTextStream>
#include <gtest/gtest.h>

#include "runtime_layout_guard.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto layoutCheck = stdiolink::tests::validateTestRuntimeLayout(
        QCoreApplication::applicationFilePath(),
        QCoreApplication::applicationDirPath());
    if (!layoutCheck.ok) {
        QTextStream err(stderr);
        err << layoutCheck.message << "\n";
        return EXIT_FAILURE;
    }

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
