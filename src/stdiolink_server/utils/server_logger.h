#pragma once

#include <QString>

namespace stdiolink_server {

class ServerLogger {
public:
    struct Config {
        QString logLevel = "info";
        QString logDir;
        qint64 maxFileBytes = 10 * 1024 * 1024;
        int maxFiles = 3;
    };

    static bool init(const Config& config, QString& error);
    static void shutdown();

private:
    ServerLogger() = delete;
};

} // namespace stdiolink_server
