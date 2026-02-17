#include "driver_manager_scanner.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>

#include "stdiolink/platform/platform_utils.h"
#include "stdiolink/protocol/meta_types.h"
#include "stdiolink_server/utils/process_env_utils.h"

namespace stdiolink_server {

bool DriverManagerScanner::isFailedDir(const QString& dirName) {
    return dirName.endsWith(".failed");
}

bool DriverManagerScanner::markFailed(const QString& dirPath) {
    const QFileInfo info(dirPath);
    if (info.fileName().endsWith(".failed")) {
        return true;
    }

    QDir parent(info.absolutePath());
    const QString oldName = info.fileName();
    const QString newName = oldName + ".failed";

    if (parent.exists(newName)) {
        return false;
    }
    return parent.rename(oldName, newName);
}

QString DriverManagerScanner::findDriverExecutable(const QString& dirPath) {
    QDir dir(dirPath);
    QStringList filters;
    filters << stdiolink::PlatformUtils::executableFilter();
    const QStringList files = dir.entryList(filters, QDir::Files | QDir::Executable);
    for (const QString& file : files) {
        const QString stem = QFileInfo(file).completeBaseName();
        if (stdiolink::PlatformUtils::isDriverExecutableName(stem)) {
            return dir.absoluteFilePath(file);
        }
        qWarning("Driver executable '%s' in '%s' does not match prefix '%s', skipped",
                 qUtf8Printable(file), qUtf8Printable(dirPath),
                 qUtf8Printable(stdiolink::PlatformUtils::driverExecutablePrefix()));
    }
    return {};
}

QString DriverManagerScanner::computeMetaHash(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

bool DriverManagerScanner::loadMetaFile(const QString& metaPath,
                                        stdiolink::DriverConfig& config) {
    QFile file(metaPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    auto meta = std::make_shared<stdiolink::meta::DriverMeta>(
        stdiolink::meta::DriverMeta::fromJson(doc.object()));
    if (meta->info.id.isEmpty()) {
        return false;
    }

    config.id = meta->info.id;
    config.meta = meta;
    config.metaHash = computeMetaHash(data);
    config.program = findDriverExecutable(QFileInfo(metaPath).absolutePath());
    return true;
}

bool DriverManagerScanner::tryExportMeta(const QString& executable,
                                         const QString& metaPath) const {
    QProcess proc;
    proc.setProgram(executable);
    proc.setArguments({"--export-meta=" + metaPath});
    
    // Add application directory to PATH so driver can find Qt DLLs
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    prependDirToPath(QCoreApplication::applicationDirPath(), env);
    proc.setProcessEnvironment(env);
    
    proc.start();
    if (!proc.waitForFinished(kExportTimeoutMs)) {
        proc.kill();
        (void)proc.waitForFinished(1000);
        return false;
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        return false;
    }

    QFile out(metaPath);
    if (!out.exists() || !out.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(out.readAll(), &parseErr);
    return parseErr.error == QJsonParseError::NoError && doc.isObject();
}

QHash<QString, stdiolink::DriverConfig> DriverManagerScanner::scan(const QString& driversDir,
                                                                    bool refreshMeta,
                                                                    ScanStats* stats) const {
    QHash<QString, stdiolink::DriverConfig> result;

    QDir root(driversDir);
    if (!root.exists()) {
        return result;
    }

    const auto entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        if (isFailedDir(entry)) {
            if (stats) {
                stats->skippedFailed++;
            }
            continue;
        }

        if (stats) {
            stats->scanned++;
        }

        const QString subDir = root.absoluteFilePath(entry);
        const QString metaPath = subDir + "/driver.meta.json";
        const bool hasMeta = QFileInfo::exists(metaPath);
        const QString executable = findDriverExecutable(subDir);

        if (!hasMeta) {
            if (executable.isEmpty() || !tryExportMeta(executable, metaPath)) {
                qWarning("Driver export failed, marking failed: %s", qUtf8Printable(entry));
                if (markFailed(subDir)) {
                    if (stats) {
                        stats->newlyFailed++;
                    }
                } else {
                    qWarning("Failed to rename directory to .failed: %s", qUtf8Printable(subDir));
                }
                continue;
            }
        } else if (refreshMeta && !executable.isEmpty()) {
            if (!tryExportMeta(executable, metaPath)) {
                qWarning("Driver re-export failed, keeping old meta: %s", qUtf8Printable(entry));
            }
        }

        stdiolink::DriverConfig config;
        if (!loadMetaFile(metaPath, config)) {
            qWarning("Invalid driver meta, skip: %s", qUtf8Printable(metaPath));
            continue;
        }

        if (config.program.isEmpty()) {
            qWarning("Driver '%s' has meta but no %s executable, skip",
                     qUtf8Printable(entry),
                     qUtf8Printable(stdiolink::PlatformUtils::driverExecutablePrefix()));
            continue;
        }

        result.insert(config.id, config);
        if (stats) {
            stats->updated++;
        }
    }

    return result;
}

} // namespace stdiolink_server
