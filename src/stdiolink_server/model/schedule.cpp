#include "schedule.h"

#include <cmath>
#include <limits>

namespace stdiolink_server {

namespace {

bool parseNonNegativeIntField(const QJsonObject& obj,
                              const QString& key,
                              int defaultValue,
                              int& out,
                              QString& error) {
    if (!obj.contains(key)) {
        out = defaultValue;
        return true;
    }

    const QJsonValue value = obj.value(key);
    if (!value.isDouble()) {
        error = QString("schedule.%1 must be an integer >= 0").arg(key);
        return false;
    }

    const double raw = value.toDouble();
    if (!std::isfinite(raw) || std::floor(raw) != raw
        || raw < 0.0 || raw > static_cast<double>(std::numeric_limits<int>::max())) {
        error = QString("schedule.%1 must be an integer >= 0").arg(key);
        return false;
    }

    out = static_cast<int>(raw);
    return true;
}

} // namespace

Schedule Schedule::fromJson(const QJsonObject& obj, QString& error) {
    Schedule schedule;
    if (!parseNonNegativeIntField(obj, QStringLiteral("runTimeoutMs"), 0, schedule.runTimeoutMs, error)) {
        return schedule;
    }

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
    obj["runTimeoutMs"] = runTimeoutMs;
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
