#pragma once

#include <QJsonObject>
#include <QString>

#include "schedule.h"

namespace stdiolink_server {

struct Project {
    QString id;
    QString name;
    QString serviceId;
    bool enabled = true;
    Schedule schedule;
    QJsonObject config;

    bool valid = true;
    QString error;

    static Project fromJson(const QString& id,
                            const QJsonObject& obj,
                            QString& parseError);
    QJsonObject toJson() const;
};

} // namespace stdiolink_server
