#include "instance_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>

#include "stdiolink/guard/process_guard_server.h"

namespace stdiolink_server {

namespace {

QString appendExeSuffix(const QString& path) {
#ifdef Q_OS_WIN
    if (!path.endsWith(".exe", Qt::CaseInsensitive)) {
        return path + ".exe";
    }
#endif
    return path;
}

} // namespace

InstanceManager::InstanceManager(const QString& dataRoot,
                                 const ServerConfig& config,
                                 QObject* parent)
    : QObject(parent)
    , m_dataRoot(dataRoot)
    , m_config(config) {
}

QString InstanceManager::findServiceProgram() const {
    if (!m_config.serviceProgram.isEmpty()) {
        const QFileInfo explicitPath(m_config.serviceProgram);
        if (explicitPath.isExecutable()) {
            return explicitPath.absoluteFilePath();
        }

        if (explicitPath.isRelative()) {
            const QString underDataRoot = appendExeSuffix(m_dataRoot + "/" + m_config.serviceProgram);
            if (QFileInfo(underDataRoot).isExecutable()) {
                return underDataRoot;
            }
        }

        return {};
    }

    const QString sameDir = appendExeSuffix(
        QCoreApplication::applicationDirPath() + "/stdiolink_service");
    if (QFileInfo(sameDir).isExecutable()) {
        return sameDir;
    }

    return QStandardPaths::findExecutable("stdiolink_service");
}

QString InstanceManager::generateInstanceId() const {
    return "inst_" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

QString InstanceManager::startInstance(const Project& project,
                                       const QString& serviceDir,
                                       QString& error) {
    error.clear();

    if (!project.valid) {
        error = "project invalid: " + project.error;
        return {};
    }

    if (serviceDir.isEmpty() || !QDir(serviceDir).exists()) {
        error = "service directory not found: " + serviceDir;
        return {};
    }

    const QString program = findServiceProgram();
    if (program.isEmpty()) {
        error = "stdiolink_service not found";
        return {};
    }

    auto tempFile = std::make_unique<QTemporaryFile>();
    tempFile->setAutoRemove(true);
    if (!tempFile->open()) {
        error = "cannot create temp config file";
        return {};
    }

    const QByteArray content = QJsonDocument(project.config).toJson(QJsonDocument::Compact);
    if (tempFile->write(content) != content.size()) {
        error = "cannot write temp config file";
        return {};
    }
    tempFile->flush();
    const QString tempConfigPath = tempFile->fileName();
    tempFile->close();

    const QString workspaceDir = m_dataRoot + "/workspaces/" + project.id;
    if (!QDir().mkpath(workspaceDir)) {
        error = "cannot create workspace: " + workspaceDir;
        return {};
    }

    const QString logsDir = m_dataRoot + "/logs";
    if (!QDir().mkpath(logsDir)) {
        error = "cannot create logs directory: " + logsDir;
        return {};
    }

    const QString instanceId = generateInstanceId();
    auto inst = std::make_unique<Instance>();
    inst->id = instanceId;
    inst->projectId = project.id;
    inst->serviceId = project.serviceId;
    inst->startedAt = QDateTime::currentDateTimeUtc();
    inst->status = "starting";
    inst->tempConfigFile = std::move(tempFile);

    // Create ProcessGuard for parent-child liveness monitoring
    auto guard = std::make_unique<stdiolink::ProcessGuardServer>();
    bool guardOk = m_guardNameOverride.isEmpty()
                       ? guard->start()
                       : guard->start(m_guardNameOverride);
    if (!guardOk) {
        error = "failed to start process guard server";
        return {};
    }

    auto* proc = new QProcess(this);
    proc->setWorkingDirectory(workspaceDir);
    proc->setProgram(program);
    proc->setArguments({serviceDir, "--config-file=" + tempConfigPath,
                        "--guard=" + guard->guardName()});

    // Add server directory to PATH so child process can find Qt DLLs
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString pathValue = env.value("PATH");
    if (!pathValue.isEmpty()) {
        env.insert("PATH", appDir + ";" + pathValue);
    } else {
        env.insert("PATH", appDir);
    }
    proc->setProcessEnvironment(env);

    const QString logPath = logsDir + "/" + project.id + ".log";
    proc->setStandardOutputFile(logPath, QIODevice::Append);
    proc->setStandardErrorFile(logPath, QIODevice::Append);

    inst->workingDirectory = workspaceDir;
    inst->logPath = logPath;
    inst->commandLine = QStringList{program} + proc->arguments();
    inst->process = proc;
    inst->guard = std::move(guard);

    connect(proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, instanceId](int exitCode, QProcess::ExitStatus status) {
                onProcessFinished(instanceId, exitCode, status);
            });

    proc->start();
    if (!proc->waitForStarted(5000)) {
        error = "process failed to start: " + proc->errorString();
        proc->deleteLater();
        return {};
    }

    inst->pid = proc->processId();
    inst->status = "running";

    m_instances.emplace(instanceId, std::move(inst));
    emit instanceStarted(instanceId, project.id);
    return instanceId;
}

void InstanceManager::terminateInstance(const QString& instanceId) {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) {
        return;
    }

    QProcess* proc = it->second->process;
    if (!proc || proc->state() == QProcess::NotRunning) {
        return;
    }

    proc->kill();
    proc->waitForFinished(1000);
}

void InstanceManager::terminateByProject(const QString& projectId) {
    QStringList ids;
    for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
        if (it->second->projectId == projectId) {
            ids.append(it->first);
        }
    }
    for (const QString& id : ids) {
        terminateInstance(id);
    }
}

void InstanceManager::terminateAll() {
    QStringList ids;
    for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
        ids.append(it->first);
    }
    for (const QString& id : ids) {
        terminateInstance(id);
    }
}

void InstanceManager::waitAllFinished(int graceTimeoutMs) {
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < graceTimeoutMs) {
        bool running = false;
        for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
            QProcess* proc = it->second->process;
            if (proc && proc->state() != QProcess::NotRunning) {
                running = true;
                break;
            }
        }

        if (!running) {
            return;
        }

        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
        QProcess* proc = it->second->process;
        if (proc && proc->state() != QProcess::NotRunning) {
            proc->kill();
        }
    }

    QElapsedTimer drain;
    drain.start();
    while (!m_instances.empty() && drain.elapsed() < 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
}

QList<const Instance*> InstanceManager::getInstances(const QString& projectId) const {
    QList<const Instance*> out;
    for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
        const Instance* inst = it->second.get();
        if (projectId.isEmpty() || inst->projectId == projectId) {
            out.push_back(inst);
        }
    }
    return out;
}

const Instance* InstanceManager::getInstance(const QString& instanceId) const {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) {
        return nullptr;
    }
    return it->second.get();
}

int InstanceManager::instanceCount(const QString& projectId) const {
    if (projectId.isEmpty()) {
        return static_cast<int>(m_instances.size());
    }

    int count = 0;
    for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
        if (it->second->projectId == projectId) {
            count++;
        }
    }
    return count;
}

void InstanceManager::onProcessFinished(const QString& instanceId,
                                        int exitCode,
                                        QProcess::ExitStatus status) {
    auto it = m_instances.find(instanceId);
    if (it == m_instances.end()) {
        return;
    }

    Instance* inst = it->second.get();
    const QString projectId = inst->projectId;
    const bool abnormal = status == QProcess::CrashExit || exitCode != 0;
    inst->status = abnormal ? "failed" : "stopped";

    emit instanceFinished(instanceId, projectId, exitCode, status);

    if (inst->process) {
        inst->process->deleteLater();
        inst->process = nullptr;
    }

    m_instances.erase(it);
}

} // namespace stdiolink_server
