#pragma once

#include <QString>

QT_BEGIN_NAMESPACE
class QSharedMemory;
QT_END_NAMESPACE

namespace stdiolink_server {

QString buildServerConsoleUrl(quint16 port);
QString buildServerSingleInstanceKey(const QString& dataRoot);

class ServerSingleInstanceGuard {
public:
    explicit ServerSingleInstanceGuard(const QString& dataRoot);
    ~ServerSingleInstanceGuard();

    bool tryAcquire(QString* errorMessage);

private:
    QSharedMemory* m_sharedMemory = nullptr;
};

} // namespace stdiolink_server
