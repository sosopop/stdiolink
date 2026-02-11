#include <gtest/gtest.h>

#include "stdiolink_server/model/schedule.h"

using namespace stdiolink_server;

TEST(ScheduleTest, ManualDefault) {
    const QJsonObject obj{{"type", "manual"}};
    QString error;
    const Schedule schedule = Schedule::fromJson(obj, error);

    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(schedule.type, ScheduleType::Manual);
}

TEST(ScheduleTest, FixedRate) {
    const QJsonObject obj{{"type", "fixed_rate"}, {"intervalMs", 3000}, {"maxConcurrent", 2}};
    QString error;
    const Schedule schedule = Schedule::fromJson(obj, error);

    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(schedule.type, ScheduleType::FixedRate);
    EXPECT_EQ(schedule.intervalMs, 3000);
    EXPECT_EQ(schedule.maxConcurrent, 2);
}

TEST(ScheduleTest, FixedRateInvalidInterval) {
    const QJsonObject obj{{"type", "fixed_rate"}, {"intervalMs", 50}};
    QString error;
    (void)Schedule::fromJson(obj, error);

    EXPECT_FALSE(error.isEmpty());
}

TEST(ScheduleTest, Daemon) {
    const QJsonObject obj{{"type", "daemon"}, {"restartDelayMs", 5000}, {"maxConsecutiveFailures", 3}};
    QString error;
    const Schedule schedule = Schedule::fromJson(obj, error);

    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(schedule.type, ScheduleType::Daemon);
    EXPECT_EQ(schedule.restartDelayMs, 5000);
    EXPECT_EQ(schedule.maxConsecutiveFailures, 3);
}

TEST(ScheduleTest, UnknownType) {
    const QJsonObject obj{{"type", "cron"}};
    QString error;
    (void)Schedule::fromJson(obj, error);

    EXPECT_FALSE(error.isEmpty());
}

TEST(ScheduleTest, DaemonInvalidFailureThreshold) {
    const QJsonObject obj{{"type", "daemon"}, {"maxConsecutiveFailures", 0}};
    QString error;
    (void)Schedule::fromJson(obj, error);

    EXPECT_FALSE(error.isEmpty());
}
