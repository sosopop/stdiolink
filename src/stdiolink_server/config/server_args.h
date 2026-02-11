#pragma once

#include <QString>
#include <QStringList>

namespace stdiolink_server {

struct ServerArgs {
    QString dataRoot = ".";
    int port = 8080;
    QString host = "127.0.0.1";
    QString logLevel = "info";

    bool hasPort = false;
    bool hasHost = false;
    bool hasLogLevel = false;

    bool help = false;
    bool version = false;
    QString error;

    static ServerArgs parse(const QStringList& args);
};

} // namespace stdiolink_server
