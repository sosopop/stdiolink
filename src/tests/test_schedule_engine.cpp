#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTemporaryDir>

#include <functional>
#include <memory>

#include "stdiolink_server/config/server_config.h"
#include "stdiolink_server/manager/instance_manager.h"
#include "stdiolink_server/manager/schedule_engine.h"

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

void spinMs(int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

ServiceInfo makeService(const QString& serviceDir) {
    ServiceInfo svc;
    svc.id = "svc";
    svc.name = "Svc";
    svc.version = "1.0.0";
    svc.serviceDir = serviceDir;
    svc.valid = true;
    svc.hasSchema = true;
    return svc;
}

Project makeProject(const QString& id,
                    ScheduleType type,
                    int exitCode,
                    int sleepMs) {
    Project p;
    p.id = id;
    p.name = id;
    p.serviceId = "svc";
    p.enabled = true;
    p.valid = true;
    p.schedule.type = type;
    p.config = QJsonObject{{"_test", QJsonObject{{"exitCode", exitCode}, {"sleepMs", sleepMs}}}};
    return p;
}

} // namespace

class ScheduleEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmpDir.isValid());

        dataRoot = tmpDir.path();
        ASSERT_TRUE(QDir().mkpath(dataRoot + "/logs"));
        ASSERT_TRUE(QDir().mkpath(dataRoot + "/workspaces"));

        serviceDir = dataRoot + "/services/demo";
        ASSERT_TRUE(QDir().mkpath(serviceDir));

        cfg.serviceProgram = testBinaryPath("test_service_stub");
        ASSERT_TRUE(QFileInfo::exists(cfg.serviceProgram));

        instanceMgr = std::make_unique<InstanceManager>(dataRoot, cfg);
        scheduleEngine = std::make_unique<ScheduleEngine>(instanceMgr.get());

        services.insert("svc", makeService(serviceDir));
    }

    void TearDown() override {
        scheduleEngine->setShuttingDown(true);
        scheduleEngine->stopAll();
        instanceMgr->terminateAll();
        instanceMgr->waitAllFinished(3000);
    }

    QTemporaryDir tmpDir;
    QString dataRoot;
    QString serviceDir;
    ServerConfig cfg;
    QMap<QString, ServiceInfo> services;
    std::unique_ptr<InstanceManager> instanceMgr;
    std::unique_ptr<ScheduleEngine> scheduleEngine;
};

TEST_F(ScheduleEngineTest, ManualNotAutoStarted) {
    QMap<QString, Project> projects;
    projects.insert("p", makeProject("p", ScheduleType::Manual, 0, 0));

    scheduleEngine->startAll(projects, services);
    spinMs(250);

    EXPECT_EQ(instanceMgr->instanceCount("p"), 0);
}

TEST_F(ScheduleEngineTest, FixedRateRespectsMaxConcurrent) {
    Project p = makeProject("p", ScheduleType::FixedRate, 0, 800);
    p.schedule.intervalMs = 100;
    p.schedule.maxConcurrent = 1;

    QMap<QString, Project> projects;
    projects.insert(p.id, p);

    scheduleEngine->startAll(projects, services);

    ASSERT_TRUE(waitUntil([&]() { return instanceMgr->instanceCount("p") == 1; }, 2000));
    spinMs(250);
    EXPECT_EQ(instanceMgr->instanceCount("p"), 1);
}

TEST_F(ScheduleEngineTest, DaemonCrashLoopStopsRestart) {
    Project p = makeProject("p", ScheduleType::Daemon, 1, 0);
    p.schedule.restartDelayMs = 100;
    p.schedule.maxConsecutiveFailures = 2;

    int startCount = 0;
    QObject::connect(instanceMgr.get(), &InstanceManager::instanceStarted, [&startCount](const QString&, const QString&) {
        startCount++;
    });

    QMap<QString, Project> projects;
    projects.insert(p.id, p);
    scheduleEngine->startAll(projects, services);

    ASSERT_TRUE(waitUntil([&]() { return startCount >= 2; }, 3000));
    spinMs(400);

    EXPECT_EQ(startCount, 2);
    EXPECT_EQ(instanceMgr->instanceCount("p"), 0);
}

TEST_F(ScheduleEngineTest, DaemonNormalExitDoesNotRestart) {
    Project p = makeProject("p", ScheduleType::Daemon, 0, 10);
    p.schedule.restartDelayMs = 100;
    p.schedule.maxConsecutiveFailures = 3;

    int startCount = 0;
    QObject::connect(instanceMgr.get(), &InstanceManager::instanceStarted, [&startCount](const QString&, const QString&) {
        startCount++;
    });

    QMap<QString, Project> projects;
    projects.insert(p.id, p);
    scheduleEngine->startAll(projects, services);

    ASSERT_TRUE(waitUntil([&]() { return startCount >= 1; }, 1500));
    ASSERT_TRUE(waitUntil([&]() { return instanceMgr->instanceCount("p") == 0; }, 2000));
    spinMs(300);

    EXPECT_EQ(startCount, 1);
}

TEST_F(ScheduleEngineTest, StopProjectSuppressesDaemonRestart) {
    Project p = makeProject("p", ScheduleType::Daemon, 1, 0);
    p.schedule.restartDelayMs = 100;
    p.schedule.maxConsecutiveFailures = 5;

    int startCount = 0;
    QObject::connect(instanceMgr.get(), &InstanceManager::instanceStarted, [&startCount](const QString&, const QString&) {
        startCount++;
    });

    QMap<QString, Project> projects;
    projects.insert(p.id, p);
    scheduleEngine->startAll(projects, services);

    ASSERT_TRUE(waitUntil([&]() { return startCount >= 1; }, 1500));
    scheduleEngine->stopProject("p");
    spinMs(300);

    EXPECT_EQ(startCount, 1);
}

TEST_F(ScheduleEngineTest, ResumeProjectAllowsDaemonRestartAgain) {
    Project p = makeProject("p", ScheduleType::Daemon, 1, 0);
    p.schedule.restartDelayMs = 100;
    p.schedule.maxConsecutiveFailures = 5;

    int startCount = 0;
    QObject::connect(instanceMgr.get(), &InstanceManager::instanceStarted, [&startCount](const QString&, const QString&) {
        startCount++;
    });

    QMap<QString, Project> projects;
    projects.insert(p.id, p);
    scheduleEngine->startAll(projects, services);

    ASSERT_TRUE(waitUntil([&]() { return startCount >= 1; }, 1500));
    scheduleEngine->stopProject("p");
    spinMs(200);
    const int stoppedAt = startCount;

    scheduleEngine->resumeProject("p");
    QString error;
    (void)instanceMgr->startInstance(projects["p"], serviceDir, error);
    ASSERT_TRUE(error.isEmpty());

    ASSERT_TRUE(waitUntil([&]() { return startCount >= stoppedAt + 2; }, 2500));
}
