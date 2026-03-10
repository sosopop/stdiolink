#include "project_manager.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QUuid>

#include "config/service_config_validator.h"

using namespace stdiolink_service;

namespace stdiolink_server {

namespace {

QString projectDirPath(const QString& projectsDir, const QString& id) {
    return QDir(projectsDir).absoluteFilePath(id);
}

QString projectConfigPath(const QString& projectsDir, const QString& id) {
    return QDir(projectDirPath(projectsDir, id)).absoluteFilePath("config.json");
}

QString projectParamPath(const QString& projectsDir, const QString& id) {
    return QDir(projectDirPath(projectsDir, id)).absoluteFilePath("param.json");
}

bool loadJsonObjectFile(const QString& filePath,
                        const QString& objectLabel,
                        QJsonObject& out,
                        QString& error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString("cannot open %1: %2").arg(objectLabel, filePath);
        return false;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        error = QString("%1 parse error: %2").arg(objectLabel, parseErr.errorString());
        return false;
    }
    if (!doc.isObject()) {
        error = QString("%1 must contain a JSON object").arg(objectLabel);
        return false;
    }

    out = doc.object();
    error.clear();
    return true;
}

bool writeTempJsonObjectFile(const QString& dirPath,
                             const QString& baseName,
                             const QJsonObject& obj,
                             QString& tempPath,
                             QString& error) {
    QTemporaryFile file(QDir(dirPath).absoluteFilePath(baseName + ".XXXXXX.tmp"));
    file.setAutoRemove(false);
    if (!file.open()) {
        error = "cannot create temp file for: " + baseName;
        return false;
    }

    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        tempPath = file.fileName();
        file.close();
        QFile::remove(tempPath);
        error = "write failed (incomplete): " + baseName;
        return false;
    }

    if (!file.flush()) {
        tempPath = file.fileName();
        file.close();
        QFile::remove(tempPath);
        error = "cannot flush temp file for: " + baseName;
        return false;
    }

    tempPath = file.fileName();
    file.close();
    error.clear();
    return true;
}

struct SaveTransactionEntry {
    QString finalPath;
    QString tempPath;
    QString backupPath;
    bool existed = false;
    bool movedToBackup = false;
    bool installed = false;
};

void cleanupTransactionTemps(const QVector<SaveTransactionEntry>& entries) {
    for (const SaveTransactionEntry& entry : entries) {
        if (!entry.tempPath.isEmpty()) {
            QFile::remove(entry.tempPath);
        }
    }
}

bool rollbackSaveTransaction(const QVector<SaveTransactionEntry>& entries,
                             QString& error) {
    QString rollbackError;

    for (int index = entries.size() - 1; index >= 0; --index) {
        const SaveTransactionEntry& entry = entries[index];
        if (entry.installed) {
            QFile::remove(entry.finalPath);
        }
        if (entry.movedToBackup) {
            if (!QFile::rename(entry.backupPath, entry.finalPath) && rollbackError.isEmpty()) {
                rollbackError = "rollback failed for: " + entry.finalPath;
            }
        }
        if (!entry.tempPath.isEmpty()) {
            QFile::remove(entry.tempPath);
        }
    }

    if (!rollbackError.isEmpty()) {
        error += "; " + rollbackError;
    }
    return rollbackError.isEmpty();
}

} // namespace

bool ProjectManager::isValidProjectId(const QString& id) {
    static const QRegularExpression re("^[A-Za-z0-9_-]+$");
    return !id.isEmpty() && re.match(id).hasMatch();
}

Project ProjectManager::loadOne(const QString& projectDir,
                                const QString& id) {
    Project project;
    project.id = id;

    const QString configPath = QDir(projectDir).absoluteFilePath("config.json");
    const QString paramPath = QDir(projectDir).absoluteFilePath("param.json");

    QJsonObject configObj;
    QString error;
    if (!loadJsonObjectFile(configPath, "project config file", configObj, error)) {
        project.valid = false;
        project.error = error;
        return project;
    }

    QString parseError;
    project = Project::fromStorageJson(id, configObj, parseError);
    if (!parseError.isEmpty()) {
        project.valid = false;
        project.error = parseError;
        return project;
    }

    if (QFileInfo::exists(paramPath)) {
        QJsonObject paramsObj;
        if (!loadJsonObjectFile(paramPath, "project param file", paramsObj, error)) {
            project.valid = false;
            project.error = error;
            return project;
        }
        project.config = paramsObj;
    } else {
        project.config = QJsonObject{};
    }

    project.valid = true;
    project.error.clear();
    return project;
}

bool ProjectManager::loadProject(const QString& projectsDir,
                                 const QString& id,
                                 Project& project,
                                 QString& error) {
    if (!isValidProjectId(id)) {
        error = "invalid project id: " + id;
        return false;
    }

    const QString dirPath = projectDirPath(projectsDir, id);
    if (!QDir(dirPath).exists()) {
        error = "project not found: " + id;
        return false;
    }

    project = loadOne(dirPath, id);
    if (!project.valid) {
        error = project.error;
        return false;
    }

    error.clear();
    return true;
}

QMap<QString, Project> ProjectManager::loadAll(const QString& projectsDir,
                                               const QMap<QString, ServiceInfo>& services,
                                               LoadStats* stats) {
    QMap<QString, Project> result;

    QDir dir(projectsDir);
    if (!dir.exists()) {
        return result;
    }

    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        const QString id = entry;
        if (!isValidProjectId(id)) {
            qWarning("ProjectManager: skip invalid project directory: %s", qUtf8Printable(entry));
            continue;
        }

        Project project = loadOne(dir.absoluteFilePath(entry), id);
        if (project.valid) {
            (void)validateProject(project, services);
        }

        if (project.valid) {
            if (stats) {
                stats->loaded++;
            }
        } else {
            if (stats) {
                stats->invalid++;
            }
            qWarning("ProjectManager: %s invalid: %s",
                     qUtf8Printable(id),
                     qUtf8Printable(project.error));
        }

        result.insert(id, project);
    }

    return result;
}

bool ProjectManager::validateProject(Project& project,
                                     const QMap<QString, ServiceInfo>& services) {
    if (!services.contains(project.serviceId)) {
        project.valid = false;
        project.error = "service not found: " + project.serviceId;
        return false;
    }

    const ServiceInfo& service = services[project.serviceId];

    QJsonObject merged;
    auto result = ServiceConfigValidator::mergeAndValidate(
        service.configSchema,
        QJsonObject{},
        project.config,
        UnknownFieldPolicy::Reject,
        merged);

    if (!result.valid) {
        project.valid = false;
        project.error = result.toString();
        return false;
    }

    project.config = merged;
    project.valid = true;
    project.error.clear();
    return true;
}

bool ProjectManager::saveProject(const QString& projectsDir,
                                 const Project& project,
                                 QString& error) {
    if (!isValidProjectId(project.id)) {
        error = "invalid project id: " + project.id;
        return false;
    }

    if (!QDir().mkpath(projectsDir)) {
        error = "cannot create projects directory: " + projectsDir;
        return false;
    }

    const QString dirPath = projectDirPath(projectsDir, project.id);
    if (!QDir().mkpath(dirPath)) {
        error = "cannot create project directory: " + dirPath;
        return false;
    }

    QVector<SaveTransactionEntry> entries{
        SaveTransactionEntry{projectConfigPath(projectsDir, project.id)},
        SaveTransactionEntry{projectParamPath(projectsDir, project.id)}
    };
    const QVector<QPair<QString, QJsonObject>> payloads{
        {QStringLiteral("config.json"), project.toJson()},
        {QStringLiteral("param.json"), project.config}
    };

    for (int index = 0; index < entries.size(); ++index) {
        if (!writeTempJsonObjectFile(dirPath,
                                     payloads[index].first,
                                     payloads[index].second,
                                     entries[index].tempPath,
                                     error)) {
            cleanupTransactionTemps(entries);
            return false;
        }
    }

    const QString backupSuffix = ".bak." + QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (SaveTransactionEntry& entry : entries) {
        entry.existed = QFile::exists(entry.finalPath);
        if (!entry.existed) {
            continue;
        }

        entry.backupPath = entry.finalPath + backupSuffix;
        if (!QFile::rename(entry.finalPath, entry.backupPath)) {
            error = "cannot backup file: " + entry.finalPath;
            (void)rollbackSaveTransaction(entries, error);
            return false;
        }
        entry.movedToBackup = true;
    }

    for (SaveTransactionEntry& entry : entries) {
        if (!QFile::rename(entry.tempPath, entry.finalPath)) {
            error = "cannot install file: " + entry.finalPath;
            (void)rollbackSaveTransaction(entries, error);
            return false;
        }
        entry.installed = true;
        entry.tempPath.clear();
    }

    for (const SaveTransactionEntry& entry : entries) {
        if (entry.movedToBackup && !entry.backupPath.isEmpty()) {
            if (!QFile::remove(entry.backupPath)) {
                qWarning("ProjectManager: stale backup file left behind: %s",
                         qUtf8Printable(entry.backupPath));
            }
        }
    }

    error.clear();
    return true;
}

bool ProjectManager::removeProject(const QString& projectsDir,
                                   const QString& id,
                                   QString& error) {
    const QString dirPath = projectDirPath(projectsDir, id);
    if (!QDir(dirPath).exists()) {
        error = "project not found: " + id;
        return false;
    }
    if (!QDir(dirPath).removeRecursively()) {
        error = "cannot remove project directory: " + dirPath;
        return false;
    }

    error.clear();
    return true;
}

} // namespace stdiolink_server
