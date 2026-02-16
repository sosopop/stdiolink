#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonObject>
#include <QVector>

#include "stdiolink_server/http/event_bus.h"
#include "stdiolink_server/http/event_stream_handler.h"

using namespace stdiolink_server;

// ---------------------------------------------------------------------------
// EventBus
// ---------------------------------------------------------------------------

TEST(EventBusTest, PublishEmitsSignal) {
    EventBus bus;
    QVector<ServerEvent> received;
    QObject::connect(&bus, &EventBus::eventPublished,
                     [&](const ServerEvent& e) { received.append(e); });

    bus.publish("instance.started", QJsonObject{{"instanceId", "abc"}});

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0].type, "instance.started");
    EXPECT_EQ(received[0].data.value("instanceId").toString(), "abc");
}

TEST(EventBusTest, EventContainsTimestamp) {
    EventBus bus;
    QVector<ServerEvent> received;
    QObject::connect(&bus, &EventBus::eventPublished,
                     [&](const ServerEvent& e) { received.append(e); });

    bus.publish("test.event", QJsonObject{});

    ASSERT_EQ(received.size(), 1);
    EXPECT_TRUE(received[0].timestamp.isValid());
}

TEST(EventBusTest, MultiplePublishesMultipleSignals) {
    EventBus bus;
    QVector<ServerEvent> received;
    QObject::connect(&bus, &EventBus::eventPublished,
                     [&](const ServerEvent& e) { received.append(e); });

    bus.publish("event.a", QJsonObject{});
    bus.publish("event.b", QJsonObject{});
    bus.publish("event.c", QJsonObject{});

    ASSERT_EQ(received.size(), 3);
    EXPECT_EQ(received[0].type, "event.a");
    EXPECT_EQ(received[1].type, "event.b");
    EXPECT_EQ(received[2].type, "event.c");
}

// ---------------------------------------------------------------------------
// Filter matching (static method)
// ---------------------------------------------------------------------------

TEST(EventBusTest, FilterMatchesPrefix) {
    QSet<QString> filters{"instance"};
    EXPECT_TRUE(EventStreamConnection::matchesFilter(filters, "instance.started"));
    EXPECT_TRUE(EventStreamConnection::matchesFilter(filters, "instance.finished"));
}

TEST(EventBusTest, FilterDoesNotMatchOtherType) {
    QSet<QString> filters{"instance"};
    EXPECT_FALSE(EventStreamConnection::matchesFilter(filters, "project.status_changed"));
    EXPECT_FALSE(EventStreamConnection::matchesFilter(filters, "schedule.triggered"));
}

TEST(EventBusTest, EmptyFilterMatchesAll) {
    QSet<QString> filters;
    EXPECT_TRUE(EventStreamConnection::matchesFilter(filters, "instance.started"));
    EXPECT_TRUE(EventStreamConnection::matchesFilter(filters, "project.status_changed"));
    EXPECT_TRUE(EventStreamConnection::matchesFilter(filters, "anything"));
}

TEST(EventBusTest, MultipleFiltersMatchMultipleTypes) {
    QSet<QString> filters{"instance", "project"};
    EXPECT_TRUE(EventStreamConnection::matchesFilter(filters, "instance.started"));
    EXPECT_TRUE(EventStreamConnection::matchesFilter(filters, "project.status_changed"));
    EXPECT_FALSE(EventStreamConnection::matchesFilter(filters, "schedule.triggered"));
    EXPECT_FALSE(EventStreamConnection::matchesFilter(filters, "driver.scanned"));
}

// ---------------------------------------------------------------------------
// EventStreamHandler connection count
// ---------------------------------------------------------------------------

TEST(EventBusTest, HandlerInitialConnectionCountIsZero) {
    EventBus bus;
    EventStreamHandler handler(&bus);
    EXPECT_EQ(handler.activeConnectionCount(), 0);
}

TEST(EventBusTest, MaxSseConnectionsConstant) {
    EXPECT_EQ(EventStreamHandler::kMaxSseConnections, 32);
}

// M72_R14 — SSE disconnect recovery: constant invariants
TEST(EventBusTest, M72_R14_SseTimeoutConstants) {
    EXPECT_GT(EventStreamHandler::kHeartbeatIntervalMs, 0);
    EXPECT_GT(EventStreamHandler::kConnectionTimeoutMs, 0);
    EXPECT_GE(EventStreamHandler::kConnectionTimeoutMs,
              EventStreamHandler::kHeartbeatIntervalMs);
}
