#pragma once

#include <QJsonObject>
#include <QString>

namespace stdiolink_server {

enum class ScheduleType {
    Manual,
    FixedRate,
    Daemon
};

struct Schedule {
    ScheduleType type = ScheduleType::Manual;

    int intervalMs = 5000;
    int maxConcurrent = 1;

    int restartDelayMs = 3000;
    int maxConsecutiveFailures = 5;

    static Schedule fromJson(const QJsonObject& obj, QString& error);
    QJsonObject toJson() const;
};

} // namespace stdiolink_server
