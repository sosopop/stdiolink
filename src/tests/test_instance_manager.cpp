#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <functional>

#include "stdiolink_server/config/server_config.h"
#include "stdiolink_server/manager/instance_manager.h"
#include "stdiolink_server/utils/process_env_utils.h"

using namespace stdiolink_server;

namespace {

QString exeSuffix() {
#ifdef Q_OS_WIN
    return ".exe";
#else
    return QString();
#endif
}

QString testBinaryPath(const QString& baseName) {
    return QCoreApplication::applicationDirPath() + "/" + baseName + exeSuffix();
}

bool copyExecutable(const QString& fromPath, const QString& toPath) {
    QFile::remove(toPath);
    if (!QFile::copy(fromPath, toPath)) {
        return false;
    }
    const QFileDevice::Permissions perms =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
        | QFileDevice::ReadGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::ExeOther;
    return QFile::setPermissions(toPath, perms);
}

bool waitUntil(const std::function<bool()>& pred, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (pred()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return pred();
}

Project makeProject(const QString& id,
                    int exitCode,
                    int sleepMs,
                    const QString& markerFile = QString()) {
    Project p;
    p.id = id;
    p.name = id;
    p.serviceId = "svc";
    p.enabled = true;
    p.valid = true;
    p.schedule.type = ScheduleType::Manual;

    QJsonObject testObj{{"exitCode", exitCode}, {"sleepMs", sleepMs}};
    if (!markerFile.isEmpty()) {
        testObj["markerFile"] = markerFile;
    }
    p.config = QJsonObject{{"_test", testObj}};
    return p;
}

} // namespace

TEST(InstanceManagerTest, StartInstanceAndCleanup) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString dataRoot = tmp.path();
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/logs"));
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/workspaces"));

    const QString serviceDir = dataRoot + "/services/demo";
    ASSERT_TRUE(QDir().mkpath(serviceDir));

    ServerConfig cfg;
    cfg.serviceProgram = testBinaryPath("test_service_stub");
    ASSERT_TRUE(QFileInfo::exists(cfg.serviceProgram));

    InstanceManager mgr(dataRoot, cfg);

    int startedCount = 0;
    int finishedCount = 0;
    QObject::connect(&mgr, &InstanceManager::instanceStarted, [&startedCount](const QString&, const QString&) {
        startedCount++;
    });
    QObject::connect(
        &mgr,
        &InstanceManager::instanceFinished,
        [&finishedCount](const QString&, const QString&, int, QProcess::ExitStatus) {
            finishedCount++;
        });

    const QString markerFile = dataRoot + "/marker.json";
    const Project project = makeProject("p1", 0, 10, markerFile);

    QString error;
    const QString instanceId = mgr.startInstance(project, serviceDir, error);
    ASSERT_TRUE(error.isEmpty());
    ASSERT_FALSE(instanceId.isEmpty());
    EXPECT_EQ(mgr.instanceCount("p1"), 1);

    // startInstance 现在是异步的，需要等待 QProcess::started 信号触发
    ASSERT_TRUE(waitUntil([&]() { return startedCount == 1; }, 3000));

    const Instance* inst = mgr.getInstance(instanceId);
    ASSERT_NE(inst, nullptr);
    const QString tempConfigPath = inst->tempConfigFile ? inst->tempConfigFile->fileName() : QString();
    ASSERT_FALSE(tempConfigPath.isEmpty());
    EXPECT_TRUE(QFileInfo::exists(tempConfigPath));

    ASSERT_TRUE(waitUntil([&]() { return finishedCount == 1 && mgr.instanceCount("p1") == 0; }, 3000));

    EXPECT_TRUE(QFileInfo::exists(dataRoot + "/logs/p1.log"));
    EXPECT_TRUE(QFileInfo::exists(markerFile));
    EXPECT_FALSE(QFileInfo::exists(tempConfigPath));
}

TEST(InstanceManagerTest, TerminateByProject) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString dataRoot = tmp.path();
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/logs"));
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/workspaces"));

    const QString serviceDir = dataRoot + "/services/demo";
    ASSERT_TRUE(QDir().mkpath(serviceDir));

    ServerConfig cfg;
    cfg.serviceProgram = testBinaryPath("test_service_stub");
    ASSERT_TRUE(QFileInfo::exists(cfg.serviceProgram));

    InstanceManager mgr(dataRoot, cfg);

    QString err1;
    QString err2;
    const QString inst1 = mgr.startInstance(makeProject("a", 0, 5000), serviceDir, err1);
    const QString inst2 = mgr.startInstance(makeProject("b", 0, 5000), serviceDir, err2);
    ASSERT_TRUE(err1.isEmpty());
    ASSERT_TRUE(err2.isEmpty());
    ASSERT_FALSE(inst1.isEmpty());
    ASSERT_FALSE(inst2.isEmpty());

    ASSERT_TRUE(waitUntil([&]() { return mgr.instanceCount() == 2; }, 2000));

    mgr.terminateByProject("a");
    ASSERT_TRUE(waitUntil([&]() { return mgr.instanceCount("a") == 0; }, 3000));
    EXPECT_EQ(mgr.instanceCount("b"), 1);

    mgr.terminateAll();
    mgr.waitAllFinished(3000);
    EXPECT_EQ(mgr.instanceCount(), 0);
}

TEST(InstanceManagerTest, StartFailsWhenProgramMissing) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString dataRoot = tmp.path();
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/logs"));
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/workspaces"));

    const QString serviceDir = dataRoot + "/services/demo";
    ASSERT_TRUE(QDir().mkpath(serviceDir));

    ServerConfig cfg;
    cfg.serviceProgram = dataRoot + "/missing-program";

    InstanceManager mgr(dataRoot, cfg);
    QString error;
    const QString instanceId = mgr.startInstance(makeProject("x", 0, 0), serviceDir, error);

    EXPECT_TRUE(instanceId.isEmpty());
    EXPECT_FALSE(error.isEmpty());
    EXPECT_EQ(mgr.instanceCount(), 0);
}

TEST(InstanceManagerTest, RelativeServiceProgramResolvedUnderDataRoot) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString dataRoot = tmp.path();
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/logs"));
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/workspaces"));
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/bin"));
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/services/demo"));

    const QString source = testBinaryPath("test_service_stub");
    const QString target = dataRoot + "/bin/test_service_stub" + exeSuffix();
    ASSERT_TRUE(copyExecutable(source, target));

    ServerConfig cfg;
    cfg.serviceProgram = "bin/test_service_stub";

    InstanceManager mgr(dataRoot, cfg);
    QString error;
    const QString instanceId = mgr.startInstance(makeProject("rel", 0, 0), dataRoot + "/services/demo", error);
    ASSERT_TRUE(error.isEmpty());
    ASSERT_FALSE(instanceId.isEmpty());

    mgr.terminateAll();
    mgr.waitAllFinished(2000);
}

TEST(InstanceManagerTest, StartFailsForInvalidProjectAndMissingServiceDir) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString dataRoot = tmp.path();
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/logs"));
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/workspaces"));

    ServerConfig cfg;
    cfg.serviceProgram = testBinaryPath("test_service_stub");

    InstanceManager mgr(dataRoot, cfg);

    Project invalidProject = makeProject("bad", 0, 0);
    invalidProject.valid = false;
    invalidProject.error = "invalid cfg";

    QString error;
    EXPECT_TRUE(mgr.startInstance(invalidProject, dataRoot + "/services/demo", error).isEmpty());
    EXPECT_FALSE(error.isEmpty());

    Project validProject = makeProject("ok", 0, 0);
    error.clear();
    EXPECT_TRUE(mgr.startInstance(validProject, dataRoot + "/services/missing", error).isEmpty());
    EXPECT_FALSE(error.isEmpty());
}

// M72_R01 — PATH uses platform list separator
TEST(InstanceManagerTest, M72_R01_PathUsesPlatformListSeparator) {
    // Verify prependDirToPath uses QDir::listSeparator()
    QProcessEnvironment env;
    env.insert("PATH", "/usr/bin");
    stdiolink_server::prependDirToPath("/my/dir", env);
    const QString expected = QStringLiteral("/my/dir") + QDir::listSeparator() + QStringLiteral("/usr/bin");
    EXPECT_EQ(env.value("PATH"), expected);
}

TEST(InstanceManagerTest, M72_R01_PathPrependToEmptyPath) {
    QProcessEnvironment env;
    stdiolink_server::prependDirToPath("/my/dir", env);
    EXPECT_EQ(env.value("PATH"), "/my/dir");
}

TEST(InstanceManagerTest, LogContentHasTimestampAndStderrPrefix) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString dataRoot = tmp.path();
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/logs"));
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/workspaces"));

    const QString serviceDir = dataRoot + "/services/demo";
    ASSERT_TRUE(QDir().mkpath(serviceDir));

    ServerConfig cfg;
    cfg.serviceProgram = testBinaryPath("test_service_stub");
    ASSERT_TRUE(QFileInfo::exists(cfg.serviceProgram));

    InstanceManager mgr(dataRoot, cfg);

    int finishedCount = 0;
    QObject::connect(
        &mgr, &InstanceManager::instanceFinished,
        [&finishedCount](const QString&, const QString&, int, QProcess::ExitStatus) {
            finishedCount++;
        });

    // Build project with stdout/stderr output
    Project p = makeProject("logfmt", 0, 100);
    QJsonObject testObj = p.config.value("_test").toObject();
    testObj["stdoutText"] = "hello_stdout_marker";
    testObj["stderrText"] = "hello_stderr_marker";
    p.config["_test"] = testObj;

    QString error;
    const QString instanceId = mgr.startInstance(p, serviceDir, error);
    ASSERT_TRUE(error.isEmpty()) << qPrintable(error);

    ASSERT_TRUE(waitUntil([&]() { return finishedCount == 1; }, 5000));

    const QString logPath = dataRoot + "/logs/logfmt.log";
    ASSERT_TRUE(QFileInfo::exists(logPath));

    QFile file(logPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    const QStringList lines = content.split('\n', Qt::SkipEmptyParts);

    // Timestamp pattern: 2026-02-25T06:19:51.123Z | ...
    const QRegularExpression tsRe(
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z \| .+$)");

    bool foundStdout = false;
    bool foundStderr = false;
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        EXPECT_TRUE(tsRe.match(trimmed).hasMatch())
            << "Missing timestamp: " << trimmed.toStdString();
        if (trimmed.contains("hello_stdout_marker")) {
            foundStdout = true;
            EXPECT_FALSE(trimmed.contains("[stderr]"));
        }
        if (trimmed.contains("hello_stderr_marker")) {
            foundStderr = true;
            EXPECT_TRUE(trimmed.contains("[stderr]"));
        }
    }
    EXPECT_TRUE(foundStdout) << "stdout marker not found in log";
    EXPECT_TRUE(foundStderr) << "stderr marker not found in log";
}
