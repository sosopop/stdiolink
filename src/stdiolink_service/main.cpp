#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>
#include <cstdio>

#include "bindings/js_config.h"
#include "bindings/js_stdiolink_module.h"
#include "bindings/js_wait_any_scheduler.h"
#include "bindings/js_task_scheduler.h"
#include "config/service_args.h"
#include "config/service_config_help.h"
#include "config/service_config_schema.h"
#include "config/service_config_validator.h"
#include "config/service_directory.h"
#include "config/service_manifest.h"
#include "engine/console_bridge.h"
#include "engine/js_engine.h"
#include "stdiolink/platform/platform_utils.h"

namespace {

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
    err << "Usage: stdiolink_service <service_dir> [options]\n";
    err << "Options:\n";
    err << "  -h, --help              Show this help\n";
    err << "  -v, --version           Show version\n";
    err << "  --config.key=value      Set config value\n";
    err << "  --config-file=<path>    Load config from JSON file ('-' for stdin)\n";
    err << "  --dump-config-schema    Dump config schema and exit\n";
    err.flush();
}

void printVersion() {
    QTextStream err(stderr);
    err << "stdiolink_service 0.1.0\n";
    err.flush();
}

void printServiceHelp(const stdiolink_service::ServiceManifest& manifest) {
    QTextStream err(stderr);
    err << manifest.name << " v" << manifest.version << "\n";
    if (!manifest.description.isEmpty()) {
        err << manifest.description << "\n";
    }
    err.flush();
}

} // namespace

int main(int argc, char* argv[]) {
    stdiolink::PlatformUtils::initConsoleEncoding();
    qInstallMessageHandler(utf8MessageHandler);
    QCoreApplication app(argc, argv);

    using namespace stdiolink_service;

    auto parsed = ServiceArgs::parse(app.arguments());

    // Global help (no service directory)
    if (parsed.help && parsed.serviceDir.isEmpty()) {
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

    // Validate service directory
    ServiceDirectory svcDir(parsed.serviceDir);
    QString dirErr;
    if (!svcDir.validate(dirErr)) {
        QTextStream err(stderr);
        err << "Error: " << dirErr << "\n";
        err.flush();
        return 2;
    }

    // Load manifest
    QString mErr;
    auto manifest = ServiceManifest::loadFromFile(svcDir.manifestPath(), mErr);
    if (!mErr.isEmpty()) {
        QTextStream err(stderr);
        err << "Error: " << mErr << "\n";
        err.flush();
        return 2;
    }

    // Load config schema (needed by both --help and normal execution)
    QString schemaErr;
    auto schema = ServiceConfigSchema::fromJsonFile(svcDir.configSchemaPath(), schemaErr);
    if (!schemaErr.isEmpty()) {
        QTextStream err(stderr);
        err << "Error: " << schemaErr << "\n";
        err.flush();
        return 2;
    }

    // --help with service directory: show manifest info + general help + config help
    if (parsed.help) {
        printServiceHelp(manifest);
        printHelp();
        const QString configHelp = ServiceConfigHelp::generate(schema);
        if (!configHelp.isEmpty()) {
            QTextStream err(stderr);
            err << "\n" << configHelp;
            err.flush();
        }
        return 0;
    }

    // --dump-config-schema: output standardized schema JSON
    if (parsed.dumpSchema) {
        QTextStream out(stdout);
        out << QJsonDocument(schema.toJson()).toJson(QJsonDocument::Indented);
        out.flush();
        return 0;
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

    // Merge and validate config (cli > file > defaults)
    QJsonObject mergedConfig;
    auto vr = ServiceConfigValidator::mergeAndValidate(
        schema, fileConfig, parsed.rawConfigValues,
        UnknownFieldPolicy::Reject, mergedConfig);
    if (!vr.valid) {
        QTextStream err(stderr);
        err << "Error: config validation failed: " << vr.toString() << "\n";
        err.flush();
        return 1;
    }

    JsEngine engine;
    if (!engine.context()) {
        return 1;
    }

    ConsoleBridge::install(engine.context());

    JsConfigBinding::attachRuntime(engine.runtime());
    JsConfigBinding::setMergedConfig(engine.context(), mergedConfig);

    engine.registerModule("stdiolink", jsInitStdiolinkModule);
    JsTaskScheduler scheduler(engine.context());
    WaitAnyScheduler waitAnyScheduler(engine.context());
    JsTaskScheduler::installGlobal(engine.context(), &scheduler);
    WaitAnyScheduler::installGlobal(engine.context(), &waitAnyScheduler);

    int ret = engine.evalFile(svcDir.entryPath());

    // Drain pending jobs
    while (scheduler.hasPending() || waitAnyScheduler.hasPending() || engine.hasPendingJobs()) {
        if (scheduler.hasPending()) {
            scheduler.poll(50);
        }
        if (waitAnyScheduler.hasPending()) {
            waitAnyScheduler.poll(50);
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
