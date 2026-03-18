#include "runtime/server_runtime_support.h"

#include <QCryptographicHash>
#include <QDir>
#include <QSharedMemory>

namespace stdiolink_server {

QString buildServerConsoleUrl(quint16 port) {
    return QStringLiteral("http://127.0.0.1:%1").arg(port);
}

QString buildServerSingleInstanceKey(const QString& dataRoot) {
    const QString normalized = QDir(dataRoot).absolutePath();
    const QByteArray digest = QCryptographicHash::hash(
        normalized.toUtf8(),
        QCryptographicHash::Sha1);
    return QStringLiteral("stdiolink_server_%1").arg(QString::fromLatin1(digest.toHex()));
}

ServerSingleInstanceGuard::ServerSingleInstanceGuard(const QString& dataRoot)
    : m_sharedMemory(new QSharedMemory(buildServerSingleInstanceKey(dataRoot))) {}

ServerSingleInstanceGuard::~ServerSingleInstanceGuard() {
    if (m_sharedMemory != nullptr) {
        if (m_sharedMemory->isAttached()) {
            m_sharedMemory->detach();
        }
        delete m_sharedMemory;
    }
}

bool ServerSingleInstanceGuard::tryAcquire(QString* errorMessage) {
    if (m_sharedMemory->create(1)) {
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return true;
    }

    if (errorMessage != nullptr) {
        if (m_sharedMemory->error() == QSharedMemory::AlreadyExists) {
            *errorMessage = QStringLiteral("stdiolink_server is already running for this data root.");
        } else {
            *errorMessage = QStringLiteral("Failed to create single-instance guard: %1")
                                .arg(m_sharedMemory->errorString());
        }
    }
    return false;
}

} // namespace stdiolink_server
