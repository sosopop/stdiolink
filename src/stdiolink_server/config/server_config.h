#pragma once

#include <QString>

#include "server_args.h"

namespace stdiolink_server {

struct ServerConfig {
    int port = 8080;
    QString host = "127.0.0.1";
    QString logLevel = "info";
    QString serviceProgram;
    QString corsOrigin = "*";
    QString webuiDir;

    static ServerConfig loadFromFile(const QString& filePath, QString& error);
    void applyArgs(const ServerArgs& args);
};

} // namespace stdiolink_server
