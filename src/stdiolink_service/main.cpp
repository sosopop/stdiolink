#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>
#include <cstdio>

#include "bindings/js_config.h"
#include "bindings/js_stdiolink_module.h"
#include "bindings/js_task_scheduler.h"
#include "config/service_args.h"
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
    err << "Usage: stdiolink_service <script.js> [options]\n";
    err << "Options:\n";
    err << "  --help                  Show this help\n";
    err << "  --version               Show version\n";
    err << "  --config.key=value      Set config value\n";
    err << "  --config-file=<path>    Load config from JSON file\n";
    err << "  --dump-config-schema    Dump config schema and exit\n";
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

    using namespace stdiolink_service;

    auto parsed = ServiceArgs::parse(app.arguments());

    if (parsed.help) {
        printHelp();
        return 0;
    }
    if (parsed.version) {
        printVersion();
        return 0;
    }
    if (!parsed.error.isEmpty()) {
        QTextStream err(stderr);
        err << "Error: " << parsed.error << "\n";
        err.flush();
        return 2;
    }

    if (!QFileInfo::exists(parsed.scriptPath)) {
        QTextStream err(stderr);
        err << "File not found: " << parsed.scriptPath << "\n";
        err.flush();
        return 2;
    }

    // Load config file if specified
    QJsonObject fileConfig;
    if (!parsed.configFilePath.isEmpty()) {
        QString loadErr;
        fileConfig = ServiceArgs::loadConfigFile(parsed.configFilePath, loadErr);
        if (!loadErr.isEmpty()) {
            QTextStream err(stderr);
            err << "Error: " << loadErr << "\n";
            err.flush();
            return 2;
        }
    }

    JsEngine engine;
    if (!engine.context()) {
        return 1;
    }

    ConsoleBridge::install(engine.context());

    JsConfigBinding::attachRuntime(engine.runtime());
    JsConfigBinding::setRawConfig(engine.context(),
                                  parsed.rawConfigValues,
                                  fileConfig,
                                  parsed.dumpSchema);

    engine.registerModule("stdiolink", jsInitStdiolinkModule);
    JsTaskScheduler scheduler(engine.context());
    JsTaskScheduler::installGlobal(engine.context(), &scheduler);

    int ret = engine.evalFile(parsed.scriptPath);

    // Drain pending jobs
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

    // --dump-config-schema mode
    if (parsed.dumpSchema) {
        if (ret != 0) {
            if (JsConfigBinding::takeBlockedSideEffectFlag(engine.context())) {
                return 2;
            }
            return ret;
        }
        if (!JsConfigBinding::hasSchema(engine.context())) {
            QTextStream err(stderr);
            err << "Error: --dump-config-schema requires the script to call defineConfig()\n";
            err.flush();
            return 2;
        }
        auto schema = JsConfigBinding::getSchema(engine.context());
        QTextStream out(stdout);
        out << QJsonDocument(schema.toJson()).toJson(QJsonDocument::Indented);
        out.flush();
        return 0;
    }

    return ret;
}
