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
#include <QTextStream>

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

QString readUtf8File(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString writeFakeExecutable(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return {};
    }
#ifdef Q_OS_WIN
    QTextStream out(&file);
    out << "not a real executable\n";
#else
    QTextStream out(&file);
    out << "#!/definitely/missing/interpreter\n";
#endif
    out.flush();
    file.close();

#ifndef Q_OS_WIN
    const QFileDevice::Permissions perms =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
        | QFileDevice::ReadGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::ExeOther;
    if (!QFile::setPermissions(path, perms)) {
        return {};
    }
#endif
    return path;
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

TEST(InstanceManagerTest, T05_RuntimeWatchdogKillsLongRunningService) {
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

    int startFailedCount = 0;
    int finishedCount = 0;
    QObject::connect(&mgr, &InstanceManager::instanceStartFailed,
                     [&startFailedCount](const QString&, const QString&, const QString&) {
                         startFailedCount++;
                     });
    QObject::connect(&mgr, &InstanceManager::instanceFinished,
                     [&finishedCount](const QString&, const QString&, int, QProcess::ExitStatus) {
                         finishedCount++;
                     });

    Project project = makeProject("timeout_project", 0, 5000);
    project.schedule.runTimeoutMs = 100;

    QString error;
    const QString instanceId = mgr.startInstance(project, serviceDir, error);
    ASSERT_TRUE(error.isEmpty());
    ASSERT_FALSE(instanceId.isEmpty());

    const Instance* started = nullptr;
    ASSERT_TRUE(waitUntil([&]() {
        started = mgr.getInstance(instanceId);
        return started != nullptr && started->status == "running";
    }, 3000));
    const QString tempConfigPath =
        started->tempConfigFile ? started->tempConfigFile->fileName() : QString();
    ASSERT_FALSE(tempConfigPath.isEmpty());

    ASSERT_TRUE(waitUntil([&]() { return finishedCount == 1 && mgr.instanceCount("timeout_project") == 0; },
                          3000));
    EXPECT_EQ(startFailedCount, 0);
    EXPECT_FALSE(QFileInfo::exists(tempConfigPath));

    const QString logContent = readUtf8File(dataRoot + "/logs/timeout_project.log");
    EXPECT_TRUE(logContent.contains("service run timeout (100 ms)"));
}

TEST(InstanceManagerTest, T06_RuntimeWatchdogDoesNotKillShortTask) {
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
    int startFailedCount = 0;
    QObject::connect(&mgr, &InstanceManager::instanceFinished,
                     [&finishedCount](const QString&, const QString&, int, QProcess::ExitStatus) {
                         finishedCount++;
                     });
    QObject::connect(&mgr, &InstanceManager::instanceStartFailed,
                     [&startFailedCount](const QString&, const QString&, const QString&) {
                         startFailedCount++;
                     });

    Project project = makeProject("short_project", 0, 20);
    project.schedule.runTimeoutMs = 1000;

    QString error;
    const QString instanceId = mgr.startInstance(project, serviceDir, error);
    ASSERT_TRUE(error.isEmpty());
    ASSERT_FALSE(instanceId.isEmpty());

    ASSERT_TRUE(waitUntil([&]() { return finishedCount == 1 && mgr.instanceCount("short_project") == 0; },
                          3000));
    EXPECT_EQ(startFailedCount, 0);

    const QString logContent = readUtf8File(dataRoot + "/logs/short_project.log");
    EXPECT_FALSE(logContent.contains("service run timeout"));
}

TEST(InstanceManagerTest, T07_StartupFailureDoesNotReportRuntimeTimeout) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString dataRoot = tmp.path();
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/logs"));
    ASSERT_TRUE(QDir().mkpath(dataRoot + "/workspaces"));

    const QString serviceDir = dataRoot + "/services/demo";
    ASSERT_TRUE(QDir().mkpath(serviceDir));

#ifdef Q_OS_WIN
    const QString fakeProgram = dataRoot + "/fake_service.exe";
#else
    const QString fakeProgram = dataRoot + "/fake_service";
#endif
    ASSERT_FALSE(writeFakeExecutable(fakeProgram).isEmpty());

    ServerConfig cfg;
    cfg.serviceProgram = fakeProgram;

    InstanceManager mgr(dataRoot, cfg);

    int startFailedCount = 0;
    int finishedCount = 0;
    QString startFailedReason;
    QObject::connect(&mgr, &InstanceManager::instanceStartFailed,
                     [&startFailedCount, &startFailedReason](const QString&, const QString&, const QString& reason) {
                         startFailedCount++;
                         startFailedReason = reason;
                     });
    QObject::connect(&mgr, &InstanceManager::instanceFinished,
                     [&finishedCount](const QString&, const QString&, int, QProcess::ExitStatus) {
                         finishedCount++;
                     });

    Project project = makeProject("startup_fail_project", 0, 0);
    project.schedule.runTimeoutMs = 10;

    QString error;
    const QString instanceId = mgr.startInstance(project, serviceDir, error);
    ASSERT_TRUE(error.isEmpty());
    ASSERT_FALSE(instanceId.isEmpty());

    ASSERT_TRUE(waitUntil([&]() { return startFailedCount == 1 && finishedCount == 1; }, 3000));
    EXPECT_EQ(mgr.instanceCount(), 0);
    EXPECT_FALSE(startFailedReason.contains("service run timeout"));
}

TEST(InstanceManagerTest, T08_RuntimeTimeoutCleansTimerAndInstanceRecord) {
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
    QString tempConfigPath;
    QString instanceId;
    QObject::connect(&mgr, &InstanceManager::instanceStarted,
                     [&mgr, &tempConfigPath, &instanceId](const QString& startedId, const QString&) {
                         instanceId = startedId;
                         const Instance* inst = mgr.getInstance(startedId);
                         if (inst && inst->tempConfigFile) {
                             tempConfigPath = inst->tempConfigFile->fileName();
                         }
                     });
    QObject::connect(&mgr, &InstanceManager::instanceFinished,
                     [&finishedCount](const QString&, const QString&, int, QProcess::ExitStatus) {
                         finishedCount++;
                     });

    Project project = makeProject("cleanup_project", 0, 5000);
    project.schedule.runTimeoutMs = 100;

    QString error;
    const QString startedId = mgr.startInstance(project, serviceDir, error);
    ASSERT_TRUE(error.isEmpty());
    ASSERT_FALSE(startedId.isEmpty());

    ASSERT_TRUE(waitUntil([&]() { return finishedCount == 1; }, 3000));
    EXPECT_EQ(mgr.instanceCount(), 0);
    EXPECT_EQ(mgr.getInstance(startedId), nullptr);
    ASSERT_FALSE(tempConfigPath.isEmpty());
    EXPECT_FALSE(QFileInfo::exists(tempConfigPath));

    QCoreApplication::processEvents(QEventLoop::AllEvents, 300);
    EXPECT_EQ(finishedCount, 1);
}
