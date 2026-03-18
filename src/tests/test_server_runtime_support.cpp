#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QThread>

#include "stdiolink/platform/platform_utils.h"
#include "stdiolink_server/runtime/server_runtime_support.h"

using namespace stdiolink_server;

namespace {

quint16 reserveLoopbackPort() {
    QTcpServer server;
    EXPECT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    return server.serverPort();
}

QString serverExecutablePath() {
    return stdiolink::PlatformUtils::executablePath(QCoreApplication::applicationDirPath(),
                                                    "stdiolink_server");
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(20);
    }
    return predicate();
}

QString makeWindowsCaseVariantPath(const QString& path) {
    QString variant = QDir::toNativeSeparators(path);
    bool toggleUpper = true;
    for (int i = 0; i < variant.size(); ++i) {
        if (!variant[i].isLetter()) {
            continue;
        }
        variant[i] = toggleUpper ? variant[i].toUpper() : variant[i].toLower();
        toggleUpper = !toggleUpper;
    }
    return variant;
}

} // namespace

TEST(ServerRuntimeSupportTest, ConsoleUrlUsesLoopbackAndPort) {
    EXPECT_EQ(buildServerConsoleUrl(6200), "http://127.0.0.1:6200");
    EXPECT_EQ(buildServerConsoleUrl(8088), "http://127.0.0.1:8088");
}

TEST(ServerRuntimeSupportTest, SingleInstanceKeyIsStableForSamePath) {
    const QString key1 = buildServerSingleInstanceKey("./tmp/server-data");
    const QString key2 = buildServerSingleInstanceKey("./tmp/../tmp/server-data");

    EXPECT_EQ(key1, key2);
    EXPECT_TRUE(key1.startsWith("stdiolink_server_"));
}

TEST(ServerRuntimeSupportTest, NormalizeServerDataRootPathNormalizesSeparatorsAndCase) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    const QString withDotSegments = tmpDir.path() + "/nested/../";
    const QString normalized1 = normalizeServerDataRootPath(withDotSegments);
    const QString normalized2 = normalizeServerDataRootPath(QDir::toNativeSeparators(tmpDir.path()));

    EXPECT_EQ(normalized1, normalized2);
#ifdef Q_OS_WIN
    EXPECT_EQ(normalized1, normalized1.toLower());
    EXPECT_EQ(normalized1, normalizeServerDataRootPath(makeWindowsCaseVariantPath(tmpDir.path())));
#endif
}

TEST(ServerRuntimeSupportTest, SingleInstanceGuardRejectsSecondAcquire) {
    ServerSingleInstanceGuard guard1("./tmp/server-single-instance-test");
    QString error1;
    ASSERT_TRUE(guard1.tryAcquire(&error1)) << qPrintable(error1);

    ServerSingleInstanceGuard guard2("./tmp/server-single-instance-test");
    QString error2;
    EXPECT_FALSE(guard2.tryAcquire(&error2));
    EXPECT_FALSE(error2.isEmpty());
}

#ifdef Q_OS_WIN
TEST(ServerRuntimeSupportTest, SecondServerProcessFailsImmediatelyForCaseVariantDataRootPath) {
    ASSERT_TRUE(QFileInfo::exists(serverExecutablePath())) << qPrintable(serverExecutablePath());

    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString dataRoot = tmpDir.path() + "/server_data";
    ASSERT_TRUE(QDir().mkpath(dataRoot));

    const quint16 port = reserveLoopbackPort();

    QProcess first;
    first.setProcessChannelMode(QProcess::SeparateChannels);
    first.start(serverExecutablePath(),
                {QStringLiteral("--data-root=%1").arg(dataRoot),
                 QStringLiteral("--host=127.0.0.1"),
                 QStringLiteral("--port=%1").arg(port)});
    ASSERT_TRUE(first.waitForStarted(3000));

    QString firstStderr;
    const bool firstReady = waitUntil(
        [&]() {
            if (first.state() != QProcess::Running) {
                return false;
            }
            firstStderr += QString::fromUtf8(first.readAllStandardError());
            return firstStderr.contains(QStringLiteral("HTTP server listening"));
        },
        5000);
    ASSERT_TRUE(firstReady) << qPrintable(firstStderr);

    QProcess second;
    second.setProcessChannelMode(QProcess::SeparateChannels);
    second.start(serverExecutablePath(),
                 {QStringLiteral("--data-root=%1").arg(makeWindowsCaseVariantPath(dataRoot)),
                  QStringLiteral("--host=127.0.0.1"),
                  QStringLiteral("--port=%1").arg(port)});

    ASSERT_TRUE(second.waitForStarted(3000));
    ASSERT_TRUE(second.waitForFinished(5000));
    const QString secondStderr = QString::fromUtf8(second.readAllStandardError());
    EXPECT_EQ(second.exitStatus(), QProcess::NormalExit);
    EXPECT_EQ(second.exitCode(), 1) << qPrintable(secondStderr);
    EXPECT_TRUE(secondStderr.contains(QStringLiteral("already running"))) << qPrintable(secondStderr);

    first.kill();
    first.waitForFinished(5000);
}
#endif
