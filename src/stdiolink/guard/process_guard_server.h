#pragma once

#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QString>

namespace stdiolink {

class ProcessGuardServer {
public:
    explicit ProcessGuardServer(QObject* parent = nullptr);
    ~ProcessGuardServer();

    ProcessGuardServer(const ProcessGuardServer&) = delete;
    ProcessGuardServer& operator=(const ProcessGuardServer&) = delete;

    bool start();
    bool start(const QString& nameOverride);
    void stop();
    QString guardName() const;
    bool isListening() const;

private:
    bool listenInternal();

    QLocalServer* m_server = nullptr;
    QString m_name;
    QList<QLocalSocket*> m_connections;
};

} // namespace stdiolink
