#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <memory>
#include <spdlog/spdlog.h>

#include "event_bus.h"

namespace stdiolink_server {

class EventLog : public QObject {
    Q_OBJECT
public:
    explicit EventLog(const QString& logPath, EventBus* bus,
                      qint64 maxBytes = 5 * 1024 * 1024,
                      int maxFiles = 2,
                      QObject* parent = nullptr);
    ~EventLog() override;

    QJsonArray query(int limit = 100,
                     const QString& typePrefix = QString(),
                     const QString& projectId = QString()) const;
    QString logPath() const { return m_logPath; }

private slots:
    void onEventPublished(const ServerEvent& event);

private:
    std::shared_ptr<spdlog::logger> m_logger;
    QString m_logPath;
};

} // namespace stdiolink_server
