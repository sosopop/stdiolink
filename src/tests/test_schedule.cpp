#include <gtest/gtest.h>

#include "stdiolink_server/model/project.h"
#include "stdiolink_server/model/schedule.h"

using namespace stdiolink_server;

TEST(ScheduleTest, T01_ManualDefaultDisablesRunTimeoutMs) {
    const QJsonObject obj{{"type", "manual"}};
    QString error;
    const Schedule schedule = Schedule::fromJson(obj, error);

    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(schedule.type, ScheduleType::Manual);
    EXPECT_EQ(schedule.runTimeoutMs, 0);
}

TEST(ScheduleTest, T02_FixedRateRoundTripsRunTimeoutMs) {
    const QJsonObject obj{
        {"type", "fixed_rate"},
        {"intervalMs", 3000},
        {"maxConcurrent", 2},
        {"runTimeoutMs", 2000},
    };
    QString error;
    const Schedule schedule = Schedule::fromJson(obj, error);

    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(schedule.type, ScheduleType::FixedRate);
    EXPECT_EQ(schedule.intervalMs, 3000);
    EXPECT_EQ(schedule.maxConcurrent, 2);
    EXPECT_EQ(schedule.runTimeoutMs, 2000);
    EXPECT_EQ(schedule.toJson().value("runTimeoutMs").toInt(), 2000);
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

TEST(ScheduleTest, T03_NegativeRunTimeoutIsRejected) {
    const QJsonObject obj{{"type", "manual"}, {"runTimeoutMs", -1}};
    QString error;
    (void)Schedule::fromJson(obj, error);

    EXPECT_TRUE(error.contains("runTimeoutMs"));
}

TEST(ScheduleTest, RunTimeoutRejectsFractionalValue) {
    const QJsonObject obj{{"type", "manual"}, {"runTimeoutMs", 1.5}};
    QString error;
    (void)Schedule::fromJson(obj, error);

    EXPECT_TRUE(error.contains("runTimeoutMs"));
}

TEST(ScheduleTest, RunTimeoutRejectsNonNumericValue) {
    const QJsonObject obj{{"type", "manual"}, {"runTimeoutMs", "100"}};
    QString error;
    (void)Schedule::fromJson(obj, error);

    EXPECT_TRUE(error.contains("runTimeoutMs"));
}

TEST(ScheduleTest, T04_ProjectToJsonIncludesRunTimeoutMs) {
    Project project;
    project.name = "demo";
    project.serviceId = "svc";
    project.schedule.type = ScheduleType::Manual;
    project.schedule.runTimeoutMs = 3000;

    const QJsonObject obj = project.toJson();
    EXPECT_EQ(obj.value("schedule").toObject().value("runTimeoutMs").toInt(), 3000);
}
