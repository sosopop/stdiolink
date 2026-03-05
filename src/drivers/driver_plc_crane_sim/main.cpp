#include "handler.h"
#include "stdiolink/driver/driver_core.h"

#include <QCoreApplication>
#include <QFile>
#include <QVector>

using namespace stdiolink;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    SimDriverConfig cfg;
    QString error;
    QStringList passthroughArgs;
    if (!parseSimDriverConfigArgs(argc, argv, cfg, &error, &passthroughArgs)) {
        QFile err;
        (void)err.open(stderr, QIODevice::WriteOnly);
        err.write(error.toUtf8());
        err.write("\n");
        err.flush();
        return 3;
    }

    QVector<QByteArray> utf8Storage;
    utf8Storage.reserve(passthroughArgs.size());
    QVector<char*> argvPtrs;
    argvPtrs.reserve(passthroughArgs.size());
    for (const QString& item : passthroughArgs) {
        utf8Storage.push_back(item.toUtf8());
    }
    for (QByteArray& item : utf8Storage) {
        argvPtrs.push_back(item.data());
    }

    SimPlcCraneHandler handler(cfg);
    DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argvPtrs.size(), argvPtrs.data());
}

