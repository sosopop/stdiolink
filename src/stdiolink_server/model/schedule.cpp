#include "schedule.h"

namespace stdiolink_server {

Schedule Schedule::fromJson(const QJsonObject& obj, QString& error) {
    Schedule schedule;

    const QString type = obj.value("type").toString("manual");
    if (type == "manual") {
        schedule.type = ScheduleType::Manual;
    } else if (type == "fixed_rate") {
        schedule.type = ScheduleType::FixedRate;
        schedule.intervalMs = obj.value("intervalMs").toInt(5000);
        schedule.maxConcurrent = obj.value("maxConcurrent").toInt(1);

        if (schedule.intervalMs < 100) {
            error = "schedule.intervalMs must be >= 100";
            return schedule;
        }
        if (schedule.maxConcurrent < 1) {
            error = "schedule.maxConcurrent must be >= 1";
            return schedule;
        }
    } else if (type == "daemon") {
        schedule.type = ScheduleType::Daemon;
        schedule.restartDelayMs = obj.value("restartDelayMs").toInt(3000);
        schedule.maxConsecutiveFailures = obj.value("maxConsecutiveFailures").toInt(5);

        if (schedule.restartDelayMs < 0) {
            error = "schedule.restartDelayMs must be >= 0";
            return schedule;
        }
        if (schedule.maxConsecutiveFailures < 1) {
            error = "schedule.maxConsecutiveFailures must be >= 1";
            return schedule;
        }
    } else {
        error = "unknown schedule type: " + type;
        return schedule;
    }

    error.clear();
    return schedule;
}

QJsonObject Schedule::toJson() const {
    QJsonObject obj;
    switch (type) {
    case ScheduleType::Manual:
        obj["type"] = "manual";
        break;
    case ScheduleType::FixedRate:
        obj["type"] = "fixed_rate";
        obj["intervalMs"] = intervalMs;
        obj["maxConcurrent"] = maxConcurrent;
        break;
    case ScheduleType::Daemon:
        obj["type"] = "daemon";
        obj["restartDelayMs"] = restartDelayMs;
        obj["maxConsecutiveFailures"] = maxConsecutiveFailures;
        break;
    }
    return obj;
}

} // namespace stdiolink_server
