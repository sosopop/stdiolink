#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonObject>
#include <QTemporaryDir>

#include "stdiolink_server/http/event_bus.h"
#include "stdiolink_server/http/event_stream_handler.h"
#include "stdiolink_server/manager/instance_manager.h"
#include "stdiolink_server/manager/schedule_engine.h"
#include "stdiolink_server/server_manager.h"

using namespace stdiolink_server;

// ── EventBus Tests ──────────────────────────────────────────────────

TEST(EventBusTest, PublishTriggersSignalWithCorrectType) {
    EventBus bus;
    ServerEvent received;
    int count = 0;
    QObject::connect(&bus, &EventBus::eventPublished, [&](const ServerEvent& e) {
        received = e;
        count++;
    });

    bus.publish("instance.started", QJsonObject{{"pid", 123}});

    ASSERT_EQ(count, 1);
    EXPECT_EQ(received.type, "instance.started");
}

TEST(EventBusTest, PublishTriggersSignalWithCorrectData) {
    EventBus bus;
    ServerEvent received;
    int count = 0;
    QObject::connect(&bus, &EventBus::eventPublished, [&](const ServerEvent& e) {
        received = e;
        count++;
    });

    QJsonObject data{{"instanceId", "inst_1"}, {"projectId", "proj_a"}};
    bus.publish("instance.finished", data);

    ASSERT_EQ(count, 1);
    EXPECT_EQ(received.data.value("instanceId").toString(), "inst_1");
    EXPECT_EQ(received.data.value("projectId").toString(), "proj_a");
}

TEST(EventBusTest, PublishSetsValidTimestamp) {
    EventBus bus;
    ServerEvent received;
    int count = 0;
    QObject::connect(&bus, &EventBus::eventPublished, [&](const ServerEvent& e) {
        received = e;
        count++;
    });

    const QDateTime before = QDateTime::currentDateTimeUtc();
    bus.publish("test.event", {});
    const QDateTime after = QDateTime::currentDateTimeUtc();

    ASSERT_EQ(count, 1);
    EXPECT_TRUE(received.timestamp.isValid());
    EXPECT_GE(received.timestamp, before);
    EXPECT_LE(received.timestamp, after);
}

TEST(EventBusTest, MultiplePublishesFireMultipleSignals) {
    EventBus bus;
    QVector<ServerEvent> events;
    QObject::connect(&bus, &EventBus::eventPublished,
                     [&](const ServerEvent& e) { events.append(e); });

    bus.publish("event.a", {});
    bus.publish("event.b", {});
    bus.publish("event.c", {});

    ASSERT_EQ(events.size(), 3);
    EXPECT_EQ(events[0].type, "event.a");
    EXPECT_EQ(events[1].type, "event.b");
    EXPECT_EQ(events[2].type, "event.c");
}

// ── EventStreamConnection Filter Tests ──────────────────────────────

TEST(EventStreamFilterTest, EventBusDispatchesDifferentTypes) {
    EventBus bus;
    QVector<ServerEvent> events;
    QObject::connect(&bus, &EventBus::eventPublished,
                     [&](const ServerEvent& e) { events.append(e); });

    bus.publish("instance.started", QJsonObject{{"id", "a"}});
    bus.publish("project.status_changed", QJsonObject{{"id", "b"}});

    ASSERT_EQ(events.size(), 2);
    EXPECT_TRUE(events[0].type.startsWith("instance"));
    EXPECT_FALSE(events[1].type.startsWith("instance"));
    EXPECT_TRUE(events[1].type.startsWith("project"));
}

TEST(EventStreamFilterTest, HandlerCreatedWithZeroConnections) {
    EventBus bus;
    EventStreamHandler handler(&bus);

    EXPECT_EQ(handler.activeConnectionCount(), 0);
    EXPECT_EQ(EventStreamHandler::kMaxSseConnections, 32);
}

// ── ScheduleEngine Signal Tests ─────────────────────────────────────

class ScheduleEngineSignalTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_tempDir.reset(new QTemporaryDir());
        ASSERT_TRUE(m_tempDir->isValid());

        m_config.serviceProgram = "test_service_stub";
        m_instanceMgr = new InstanceManager(m_tempDir->path(), m_config);
        m_engine = new ScheduleEngine(m_instanceMgr);
    }

    void TearDown() override {
        delete m_engine;
        delete m_instanceMgr;
    }

    ServerConfig m_config;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    InstanceManager* m_instanceMgr = nullptr;
    ScheduleEngine* m_engine = nullptr;
};

TEST_F(ScheduleEngineSignalTest, ScheduleTriggeredSignalCanBeConnected) {
    QString receivedProjectId;
    QString receivedType;
    QObject::connect(m_engine, &ScheduleEngine::scheduleTriggered,
                     [&](const QString& pid, const QString& type) {
                         receivedProjectId = pid;
                         receivedType = type;
                     });

    emit m_engine->scheduleTriggered("proj_x", "daemon");
    EXPECT_EQ(receivedProjectId, "proj_x");
    EXPECT_EQ(receivedType, "daemon");
}

TEST_F(ScheduleEngineSignalTest, ScheduleSuppressedSignalCanBeConnected) {
    QString receivedProjectId;
    QString receivedReason;
    int receivedFailures = 0;
    QObject::connect(m_engine, &ScheduleEngine::scheduleSuppressed,
                     [&](const QString& pid, const QString& reason, int failures) {
                         receivedProjectId = pid;
                         receivedReason = reason;
                         receivedFailures = failures;
                     });

    emit m_engine->scheduleSuppressed("proj_y", "crash_loop", 3);
    EXPECT_EQ(receivedProjectId, "proj_y");
    EXPECT_EQ(receivedReason, "crash_loop");
    EXPECT_EQ(receivedFailures, 3);
}

// ── ServerManager EventBus Integration Tests ────────────────────────

class ServerManagerEventBusTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_tempDir.reset(new QTemporaryDir());
        ASSERT_TRUE(m_tempDir->isValid());

        QDir root(m_tempDir->path());
        root.mkpath("services");
        root.mkpath("projects");
        root.mkpath("drivers");
        root.mkpath("logs");

        m_config.host = "127.0.0.1";
        m_config.port = 0;
        m_config.serviceProgram = "test_service_stub";

        m_manager = new ServerManager(m_tempDir->path(), m_config);
    }

    void TearDown() override { delete m_manager; }

    ServerConfig m_config;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    ServerManager* m_manager = nullptr;
};

TEST_F(ServerManagerEventBusTest, EventBusIsCreated) {
    ASSERT_NE(m_manager->eventBus(), nullptr);
}

TEST_F(ServerManagerEventBusTest, EventStreamHandlerIsCreated) {
    ASSERT_NE(m_manager->eventStreamHandler(), nullptr);
}

TEST_F(ServerManagerEventBusTest, InstanceStartedWiredToEventBus) {
    ServerEvent received;
    int count = 0;
    QObject::connect(m_manager->eventBus(), &EventBus::eventPublished, [&](const ServerEvent& e) {
        received = e;
        count++;
    });

    emit m_manager->instanceManager()->instanceStarted("inst_1", "proj_a");

    ASSERT_EQ(count, 1);
    EXPECT_EQ(received.type, "instance.started");
    EXPECT_EQ(received.data.value("instanceId").toString(), "inst_1");
    EXPECT_EQ(received.data.value("projectId").toString(), "proj_a");
}

TEST_F(ServerManagerEventBusTest, InstanceFinishedWiredToEventBus) {
    ServerEvent received;
    int count = 0;
    QObject::connect(m_manager->eventBus(), &EventBus::eventPublished, [&](const ServerEvent& e) {
        received = e;
        count++;
    });

    emit m_manager->instanceManager()->instanceFinished("inst_2", "proj_b", 0,
                                                        QProcess::NormalExit);

    ASSERT_EQ(count, 1);
    EXPECT_EQ(received.type, "instance.finished");
    EXPECT_EQ(received.data.value("instanceId").toString(), "inst_2");
    EXPECT_EQ(received.data.value("projectId").toString(), "proj_b");
    EXPECT_EQ(received.data.value("exitCode").toInt(), 0);
    EXPECT_EQ(received.data.value("status").toString(), "normal");
}

TEST_F(ServerManagerEventBusTest, InstanceCrashedStatusMapped) {
    ServerEvent received;
    int count = 0;
    QObject::connect(m_manager->eventBus(), &EventBus::eventPublished, [&](const ServerEvent& e) {
        received = e;
        count++;
    });

    emit m_manager->instanceManager()->instanceFinished("inst_3", "proj_c", 1, QProcess::CrashExit);

    ASSERT_EQ(count, 1);
    EXPECT_EQ(received.data.value("status").toString(), "crashed");
    EXPECT_EQ(received.data.value("exitCode").toInt(), 1);
}

TEST_F(ServerManagerEventBusTest, ScheduleTriggeredWiredToEventBus) {
    ServerEvent received;
    int count = 0;
    QObject::connect(m_manager->eventBus(), &EventBus::eventPublished, [&](const ServerEvent& e) {
        received = e;
        count++;
    });

    emit m_manager->scheduleEngine()->scheduleTriggered("proj_d", "daemon");

    ASSERT_EQ(count, 1);
    EXPECT_EQ(received.type, "schedule.triggered");
    EXPECT_EQ(received.data.value("projectId").toString(), "proj_d");
    EXPECT_EQ(received.data.value("scheduleType").toString(), "daemon");
}

TEST_F(ServerManagerEventBusTest, ScheduleSuppressedWiredToEventBus) {
    ServerEvent received;
    int count = 0;
    QObject::connect(m_manager->eventBus(), &EventBus::eventPublished, [&](const ServerEvent& e) {
        received = e;
        count++;
    });

    emit m_manager->scheduleEngine()->scheduleSuppressed("proj_e", "crash_loop", 5);

    ASSERT_EQ(count, 1);
    EXPECT_EQ(received.type, "schedule.suppressed");
    EXPECT_EQ(received.data.value("projectId").toString(), "proj_e");
    EXPECT_EQ(received.data.value("reason").toString(), "crash_loop");
    EXPECT_EQ(received.data.value("consecutiveFailures").toInt(), 5);
}
