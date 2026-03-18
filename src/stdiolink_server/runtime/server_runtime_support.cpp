#include "runtime/server_runtime_support.h"

#include "stdiolink/platform/platform_utils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QSharedMemory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace stdiolink_server {

QString buildServerConsoleUrl(quint16 port) {
    return QStringLiteral("http://127.0.0.1:%1").arg(port);
}

QString normalizeServerDataRootPath(const QString& dataRoot) {
    QString normalized = QDir::cleanPath(dataRoot);
    normalized = QDir(normalized).absolutePath();
    normalized = QDir::fromNativeSeparators(normalized);
#ifdef Q_OS_WIN
    normalized = normalized.toLower();
#endif
    return normalized;
}

QString buildServerSingleInstanceKey(const QString& dataRoot) {
    const QString normalized = normalizeServerDataRootPath(dataRoot);
    const QByteArray digest = QCryptographicHash::hash(
        normalized.toUtf8(),
        QCryptographicHash::Sha1);
    return QStringLiteral("stdiolink_server_%1").arg(QString::fromLatin1(digest.toHex()));
}

bool ensureServerConsole(QString* errorMessage) {
#ifdef Q_OS_WIN
    if (GetConsoleWindow() == nullptr) {
        bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
        if (!attached) {
            const DWORD attachError = GetLastError();
            if (attachError != ERROR_ACCESS_DENIED) {
                attached = AllocConsole() != FALSE;
            } else {
                attached = true;
            }
        }

        if (!attached) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Failed to attach or allocate Windows console.");
            }
            return false;
        }
    }

    FILE* inputFile = nullptr;
    FILE* outputFile = nullptr;
    FILE* errorFile = nullptr;
    const errno_t inErr = freopen_s(&inputFile, "CONIN$", "r", stdin);
    const errno_t outErr = freopen_s(&outputFile, "CONOUT$", "w", stdout);
    const errno_t errErr = freopen_s(&errorFile, "CONOUT$", "w", stderr);
    if (inErr != 0 || outErr != 0 || errErr != 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to bind stdin/stdout/stderr to Windows console.");
        }
        return false;
    }
#endif

    stdiolink::PlatformUtils::initConsoleEncoding();
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
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
