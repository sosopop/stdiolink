#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>
#include <cstdio>

#include "bindings/js_stdiolink_module.h"
#include "bindings/js_task_scheduler.h"
#include "engine/console_bridge.h"
#include "engine/js_engine.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

void initConsoleEncoding() {
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

void utf8MessageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    QByteArray line;
    if (type == QtWarningMsg) {
        line += "Warning: ";
    } else if (type == QtCriticalMsg) {
        line += "Error: ";
    } else if (type == QtFatalMsg) {
        line += "Fatal: ";
    }
    line += msg.toUtf8();
    line += '\n';
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
    std::fflush(stderr);
    if (type == QtFatalMsg) {
        std::abort();
    }
}

void printHelp() {
    QTextStream err(stderr);
    err << "Usage: stdiolink_service <script.js>\n";
    err << "Options:\n";
    err << "  --help     Show this help\n";
    err << "  --version  Show version\n";
    err.flush();
}

void printVersion() {
    QTextStream err(stderr);
    err << "stdiolink_service 0.1.0\n";
    err.flush();
}

} // namespace

int main(int argc, char* argv[]) {
    initConsoleEncoding();
    qInstallMessageHandler(utf8MessageHandler);
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        printHelp();
        return 2;
    }

    const QString arg1 = QString::fromLocal8Bit(argv[1]);
    if (arg1 == "--help" || arg1 == "-h") {
        printHelp();
        return 0;
    }

    if (arg1 == "--version" || arg1 == "-v") {
        printVersion();
        return 0;
    }

    if (!QFileInfo::exists(arg1)) {
        QTextStream err(stderr);
        err << "File not found: " << arg1 << "\n";
        err.flush();
        return 2;
    }

    JsEngine engine;
    if (!engine.context()) {
        return 1;
    }

    ConsoleBridge::install(engine.context());
    engine.registerModule("stdiolink", jsInitStdiolinkModule);
    JsTaskScheduler scheduler(engine.context());
    JsTaskScheduler::installGlobal(engine.context(), &scheduler);

    int ret = engine.evalFile(arg1);
    while (scheduler.hasPending() || engine.hasPendingJobs()) {
        if (scheduler.hasPending()) {
            scheduler.poll(50);
        }
        while (engine.hasPendingJobs()) {
            engine.executePendingJobs();
        }
    }
    if (ret == 0 && engine.hadJobError()) {
        ret = 1;
    }

    return ret;
}
