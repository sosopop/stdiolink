#include "server_logger.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace stdiolink_server {

namespace {

spdlog::level::level_enum toSpdlogLevel(const QString& level) {
    if (level == "debug") return spdlog::level::debug;
    if (level == "warn")  return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    return spdlog::level::info;
}

static QtMessageHandler s_previousHandler = nullptr;

void qtToSpdlogHandler(QtMsgType type,
                       const QMessageLogContext&,
                       const QString& msg) {
    auto logger = spdlog::default_logger();
    switch (type) {
    case QtDebugMsg:    logger->debug("{}", msg.toStdString()); break;
    case QtInfoMsg:     logger->info("{}", msg.toStdString()); break;
    case QtWarningMsg:  logger->warn("{}", msg.toStdString()); break;
    case QtCriticalMsg: logger->error("{}", msg.toStdString()); break;
    case QtFatalMsg:    logger->critical("{}", msg.toStdString()); abort();
    }
}

} // namespace

bool ServerLogger::init(const Config& config, QString& error) {
    try {
        auto consoleSink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            (config.logDir + "/server.log").toStdString(),
            static_cast<size_t>(config.maxFileBytes),
            static_cast<size_t>(config.maxFiles));

        auto logger = std::make_shared<spdlog::logger>(
            "server", spdlog::sinks_init_list{consoleSink, fileSink});

        logger->set_level(toSpdlogLevel(config.logLevel));
        logger->set_pattern("%Y-%m-%dT%H:%M:%S.%eZ [%L] %v",
                            spdlog::pattern_time_type::utc);
        logger->flush_on(spdlog::level::warn);

        spdlog::set_default_logger(logger);
        s_previousHandler = qInstallMessageHandler(qtToSpdlogHandler);
        return true;
    } catch (const spdlog::spdlog_ex& ex) {
        error = QString("failed to initialize logger: %1").arg(ex.what());
        return false;
    }
}

void ServerLogger::shutdown() {
    qInstallMessageHandler(s_previousHandler);
    s_previousHandler = nullptr;
    spdlog::shutdown();
}

} // namespace stdiolink_server
