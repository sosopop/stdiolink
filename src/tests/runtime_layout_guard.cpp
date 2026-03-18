#include "runtime_layout_guard.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace stdiolink::tests {

namespace {

QStringList missingLayoutEntries(const QString& runtimeRootPath) {
    const QDir runtimeRoot(runtimeRootPath);
    QStringList missing;

    const QString dataRootPath = runtimeRoot.absoluteFilePath("data_root");
    const QDir dataRoot(dataRootPath);
    if (!QFileInfo(dataRootPath).isDir()) {
        missing << "data_root/";
        return missing;
    }

    if (!QFileInfo(dataRoot.absoluteFilePath("drivers")).isDir()) {
        missing << "data_root/drivers/";
    }
    if (!QFileInfo(dataRoot.absoluteFilePath("services")).isDir()) {
        missing << "data_root/services/";
    }

    return missing;
}

}  // namespace

RuntimeLayoutCheckResult validateTestRuntimeLayout(const QString& applicationFilePath,
                                                   const QString& applicationDirPath) {
    const QFileInfo appDirInfo(applicationDirPath);
    if (!appDirInfo.exists() || !appDirInfo.isDir()) {
        return {
            false,
            QString("stdiolink test executables must run from a runtime bin directory, but the application "
                    "directory does not exist: %1")
                .arg(QDir::toNativeSeparators(applicationDirPath)),
        };
    }

    if (appDirInfo.fileName() != "bin") {
        return {
            false,
            QString("stdiolink test executables must run from a runtime layout like "
                    "<runtime_root>/bin/%1 with sibling data_root/. Actual executable path: %2")
                .arg(QFileInfo(applicationFilePath).fileName(),
                     QDir::toNativeSeparators(applicationFilePath)),
        };
    }

    const QDir appDir(applicationDirPath);
    const QString runtimeRootPath = QFileInfo(appDir.absoluteFilePath("..")).absoluteFilePath();
    const QStringList missing = missingLayoutEntries(runtimeRootPath);
    if (!missing.isEmpty()) {
        return {
            false,
            QString("stdiolink test executables must run from a runtime layout rooted at %1. Missing: %2. "
                    "Do not run raw build outputs like build/release/stdiolink_tests; use "
                    "runtime_<config>/bin/stdiolink_tests instead.")
                .arg(QDir::toNativeSeparators(runtimeRootPath),
                     missing.join(", ")),
        };
    }

    return {true, QString()};
}

}  // namespace stdiolink::tests
