#include "event_bus.h"

namespace stdiolink_server {

EventBus::EventBus(QObject* parent)
    : QObject(parent) {
}

void EventBus::publish(const QString& type, const QJsonObject& data) {
    ServerEvent event;
    event.type = type;
    event.data = data;
    event.timestamp = QDateTime::currentDateTimeUtc();
    emit eventPublished(event);
}

} // namespace stdiolink_server
