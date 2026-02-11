#include <QCoreApplication>
#include <QDir>
#include <QHostAddress>
#include <QHttpServer>
#include <QTcpServer>
#include <QTextStream>

#include <csignal>
#include <cstdio>

#include "config/server_args.h"
#include "config/server_config.h"
#include "http/api_router.h"
#include "server_manager.h"
#include "stdiolink/platform/platform_utils.h"

using namespace stdiolink_server;

namespace {

void printHelp() {
    QTextStream err(stderr);
    err << "Usage: stdiolink_server [options]\n"
        << "Options:\n"
        << "  --data-root=<path>       Data root directory (default: .)\n"
        << "  --port=<port>            HTTP port (default: 8080)\n"
        << "  --host=<addr>            Listen address (default: 127.0.0.1)\n"
        << "  --log-level=<level>      debug|info|warn|error (default: info)\n"
        << "  -h, --help               Show this help\n"
        << "  -v, --version            Show version\n";
    err.flush();
}

bool ensureDirectories(const QString& dataRoot) {
    static const char* kDirs[] = {"services", "projects", "workspaces", "logs"};
    for (const char* sub : kDirs) {
        if (!QDir(dataRoot + "/" + sub).mkpath(".")) {
            qCritical("Failed to create: %s/%s", qUtf8Printable(dataRoot), sub);
            return false;
        }
    }
    return true;
}

void requestQuitSignalHandler(int) {
    QMetaObject::invokeMethod(
        qApp,
        []() { QCoreApplication::quit(); },
        Qt::QueuedConnection);
}

} // namespace

int main(int argc, char* argv[]) {
    stdiolink::PlatformUtils::initConsoleEncoding();
    QCoreApplication app(argc, argv);

    const ServerArgs args = ServerArgs::parse(app.arguments());
    if (args.help) {
        printHelp();
        return 0;
    }
    if (args.version) {
        std::fprintf(stderr, "stdiolink_server 0.1.0\n");
        return 0;
    }
    if (!args.error.isEmpty()) {
        std::fprintf(stderr, "Error: %s\n", qUtf8Printable(args.error));
        return 2;
    }

    const QString dataRoot = QDir(args.dataRoot).absolutePath();

    QString cfgErr;
    ServerConfig config = ServerConfig::loadFromFile(dataRoot + "/config.json", cfgErr);
    if (!cfgErr.isEmpty()) {
        std::fprintf(stderr, "Error: %s\n", qUtf8Printable(cfgErr));
        return 2;
    }
    config.applyArgs(args);

    if (!ensureDirectories(dataRoot)) {
        return 1;
    }

    ServerManager manager(dataRoot, config);
    QString initErr;
    if (!manager.initialize(initErr)) {
        std::fprintf(stderr, "Init error: %s\n", qUtf8Printable(initErr));
        return 1;
    }

    manager.startScheduling();

    QHttpServer httpServer;
    QTcpServer tcpServer;
    ApiRouter router(&manager);
    router.registerRoutes(httpServer);

    if (!tcpServer.listen(QHostAddress(config.host), static_cast<quint16>(config.port))) {
        std::fprintf(stderr,
                     "Error: failed to listen on %s:%d\n",
                     qUtf8Printable(config.host),
                     config.port);
        return 1;
    }
    if (!httpServer.bind(&tcpServer)) {
        std::fprintf(stderr, "Error: failed to bind HTTP server\n");
        return 1;
    }
    const quint16 port = tcpServer.serverPort();

    qInfo("HTTP server listening on %s:%d", qUtf8Printable(config.host), config.port);

    std::signal(SIGINT, requestQuitSignalHandler);
#ifdef SIGTERM
    std::signal(SIGTERM, requestQuitSignalHandler);
#endif

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        manager.shutdown();
    });

    return app.exec();
}
