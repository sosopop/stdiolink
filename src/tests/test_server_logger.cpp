#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include "stdiolink_server/utils/server_logger.h"
#include "stdiolink_server/config/server_config.h"

using namespace stdiolink_server;

namespace {

QStringList readLogLines(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QStringList lines;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (!line.isEmpty()) lines.append(line);
    }
    return lines;
}

} // namespace

TEST(ServerLoggerTest, InfoLevelFiltersDebug) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    ServerLogger::Config cfg;
    cfg.logLevel = "info";
    cfg.logDir = tmpDir.path();
    QString error;
    ASSERT_TRUE(ServerLogger::init(cfg, error)) << qPrintable(error);
    qDebug("d");
    qInfo("i");
    qWarning("w");
    qCritical("e");
    ServerLogger::shutdown();

    const auto lines = readLogLines(tmpDir.path() + "/server.log");
    EXPECT_EQ(lines.size(), 3);
    for (const auto& line : lines) {
        EXPECT_FALSE(line.contains("[D]"));
    }
}

TEST(ServerLoggerTest, WarnLevelFiltersDebugAndInfo) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    ServerLogger::Config cfg;
    cfg.logLevel = "warn";
    cfg.logDir = tmpDir.path();
    QString error;
    ASSERT_TRUE(ServerLogger::init(cfg, error)) << qPrintable(error);
    qDebug("d");
    qInfo("i");
    qWarning("w");
    qCritical("e");
    ServerLogger::shutdown();

    const auto lines = readLogLines(tmpDir.path() + "/server.log");
    EXPECT_EQ(lines.size(), 2);
}

TEST(ServerLoggerTest, DebugLevelOutputsAll) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    ServerLogger::Config cfg;
    cfg.logLevel = "debug";
    cfg.logDir = tmpDir.path();
    QString error;
    ASSERT_TRUE(ServerLogger::init(cfg, error)) << qPrintable(error);
    qDebug("d");
    qInfo("i");
    qWarning("w");
    qCritical("e");
    ServerLogger::shutdown();

    const auto lines = readLogLines(tmpDir.path() + "/server.log");
    EXPECT_EQ(lines.size(), 4);
}

TEST(ServerLoggerTest, ErrorLevelOnlyError) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    ServerLogger::Config cfg;
    cfg.logLevel = "error";
    cfg.logDir = tmpDir.path();
    QString error;
    ASSERT_TRUE(ServerLogger::init(cfg, error)) << qPrintable(error);
    qDebug("d");
    qInfo("i");
    qWarning("w");
    qCritical("e");
    ServerLogger::shutdown();

    const auto lines = readLogLines(tmpDir.path() + "/server.log");
    EXPECT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines[0].contains("[E]"));
}

TEST(ServerLoggerTest, TimestampFormatISO8601) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    ServerLogger::Config cfg;
    cfg.logLevel = "info";
    cfg.logDir = tmpDir.path();
    QString error;
    ASSERT_TRUE(ServerLogger::init(cfg, error)) << qPrintable(error);
    qInfo("test_timestamp");
    ServerLogger::shutdown();

    const auto lines = readLogLines(tmpDir.path() + "/server.log");
    ASSERT_EQ(lines.size(), 1);
    QRegularExpression re(
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z \[I\] test_timestamp$)");
    EXPECT_TRUE(re.match(lines[0]).hasMatch())
        << "Line did not match ISO 8601 format: " << qPrintable(lines[0]);
}

TEST(ServerLoggerTest, ConfigDefaultValues) {
    ServerConfig config;
    EXPECT_EQ(config.logMaxBytes, 10 * 1024 * 1024);
    EXPECT_EQ(config.logMaxFiles, 3);
}
