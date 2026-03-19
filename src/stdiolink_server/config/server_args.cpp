#include "server_args.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace stdiolink_server {

namespace {

QString defaultDataRoot() {
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        const QString bundledDataRoot = QDir(appDir).absoluteFilePath("../data_root");
        const QFileInfo bundledInfo(bundledDataRoot);
        if (bundledInfo.exists() && bundledInfo.isDir()) {
            return QDir::cleanPath(bundledInfo.absoluteFilePath());
        }
    }
    return QStringLiteral(".");
}

} // namespace

ServerArgs ServerArgs::parse(const QStringList& args) {
    ServerArgs result;
    result.dataRoot = defaultDataRoot();

    for (int i = 1; i < args.size(); ++i) {
        const QString& arg = args[i];
        if (arg == "-h" || arg == "--help") {
            result.help = true;
            continue;
        }
        if (arg == "-v" || arg == "--version") {
            result.version = true;
            continue;
        }
        if (arg == "--attach-console") {
            result.attachConsole = true;
            continue;
        }
        if (arg.startsWith("--data-root=")) {
            result.dataRoot = arg.mid(12);
            if (result.dataRoot.isEmpty()) {
                result.error = "data-root cannot be empty";
                return result;
            }
            continue;
        }
        if (arg.startsWith("--port=")) {
            bool ok = false;
            const QString rawPort = arg.mid(7);
            result.port = rawPort.toInt(&ok);
            if (!ok || result.port < 1 || result.port > 65535) {
                result.error = "invalid port: " + rawPort;
                return result;
            }
            result.hasPort = true;
            continue;
        }
        if (arg.startsWith("--host=")) {
            result.host = arg.mid(7);
            if (result.host.isEmpty()) {
                result.error = "host cannot be empty";
                return result;
            }
            result.hasHost = true;
            continue;
        }
        if (arg.startsWith("--webui-dir=")) {
            result.webuiDir = arg.mid(12);
            if (result.webuiDir.isEmpty()) {
                result.error = "webui-dir cannot be empty";
                return result;
            }
            result.hasWebuiDir = true;
            continue;
        }
        if (arg.startsWith("--log-level=")) {
            result.logLevel = arg.mid(12);
            if (result.logLevel != "debug" && result.logLevel != "info"
                && result.logLevel != "warn" && result.logLevel != "error") {
                result.error = "invalid log level: " + result.logLevel;
                return result;
            }
            result.hasLogLevel = true;
            continue;
        }

        result.error = "unknown option: " + arg;
        return result;
    }

    return result;
}

} // namespace stdiolink_server
