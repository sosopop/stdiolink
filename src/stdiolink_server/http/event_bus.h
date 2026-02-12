#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace stdiolink_server {

struct ServerEvent {
    QString type;
    QJsonObject data;
    QDateTime timestamp;
};

class EventBus : public QObject {
    Q_OBJECT
public:
    explicit EventBus(QObject* parent = nullptr);

    void publish(const QString& type, const QJsonObject& data);

signals:
    void eventPublished(const ServerEvent& event);
};

} // namespace stdiolink_server

Q_DECLARE_METATYPE(stdiolink_server::ServerEvent)
