#include "js_driver_resolve.h"

#include <QDir>
#include <QFileInfo>

namespace stdiolink_service {

static QString execName(const QString &baseName) {
#ifdef Q_OS_WIN
    return baseName + ".exe";
#else
    return baseName;
#endif
}

static bool isValidDriverName(const QString &name) {
    if (name.contains('/') || name.contains('\\')) return false;
    if (name.endsWith(".exe", Qt::CaseInsensitive)) return false;
    return true;
}

static bool isDriverCandidate(const QString &path) {
    QFileInfo fi(path);
    if (!fi.exists()) return false;
#ifdef Q_OS_WIN
    return true;
#else
    return fi.isExecutable();
#endif
}

DriverResolveResult resolveDriverPath(const QString &driverName,
                                      const QString &dataRoot,
                                      const QString &appDir)
{
    DriverResolveResult result;
    if (!isValidDriverName(driverName)) return result;
    const QString name = execName(driverName);

    // 1. dataRoot/drivers/*/
    if (!dataRoot.isEmpty()) {
        QDir driversDir(dataRoot + "/drivers");
        QString searchEntry = driversDir.absolutePath() + "/*/";
        result.searchedPaths << searchEntry;
        if (driversDir.exists()) {
            const auto subs = driversDir.entryList(
                QDir::Dirs | QDir::NoDotAndDotDot);
            for (const auto &sub : subs) {
                QString p = driversDir.absoluteFilePath(sub + "/" + name);
                if (isDriverCandidate(p)) {
                    result.path = p;
                    return result;
                }
            }
        }
    }

    // 2. appDir/
    if (!appDir.isEmpty()) {
        QString p = QDir(appDir).absoluteFilePath(name);
        result.searchedPaths << p;
        if (isDriverCandidate(p)) {
            result.path = p;
            return result;
        }
    }

    // 3. CWD/
    {
        QString p = QDir::current().absoluteFilePath(name);
        result.searchedPaths << p;
        if (isDriverCandidate(p)) {
            result.path = p;
            return result;
        }
    }

    return result;
}

} // namespace stdiolink_service
