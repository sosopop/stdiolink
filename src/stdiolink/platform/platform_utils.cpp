#include "platform_utils.h"

#include <QDir>

#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#define platform_isatty _isatty
#define platform_fileno _fileno
#else
#include <unistd.h>
#define platform_isatty isatty
#define platform_fileno fileno
#endif

namespace stdiolink::PlatformUtils {

void initConsoleEncoding() {
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

bool isInteractiveTerminal(FILE* stream) {
    return platform_isatty(platform_fileno(stream)) != 0;
}

QString executableSuffix() {
#ifdef Q_OS_WIN
    return QStringLiteral(".exe");
#else
    return QString();
#endif
}

QString executablePath(const QString& dir, const QString& baseName) {
    return QDir::fromNativeSeparators(dir + "/" + baseName + executableSuffix());
}

QString executableFilter() {
#ifdef Q_OS_WIN
    return QStringLiteral("*.exe");
#else
    return QStringLiteral("*");
#endif
}

} // namespace stdiolink::PlatformUtils
