#include "driverlab_ws_connection.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace stdiolink_server {

DriverLabWsConnection::DriverLabWsConnection(std::unique_ptr<QWebSocket> socket,
                                             const QString& driverId,
                                             const QString& program,
                                             const QString& runMode,
                                             const QStringList& extraArgs,
                                             QObject* parent)
    : QObject(parent)
    , m_socket(std::move(socket))
    , m_driverId(driverId)
    , m_program(program)
    , m_runMode(runMode)
    , m_extraArgs(extraArgs) {

    connect(m_socket.get(), &QWebSocket::textMessageReceived,
            this, &DriverLabWsConnection::onTextMessageReceived);
    connect(m_socket.get(), &QWebSocket::disconnected,
            this, &DriverLabWsConnection::onSocketDisconnected);

    startDriver();
}

DriverLabWsConnection::~DriverLabWsConnection() {
    m_closing = true;
    stopDriver();
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }
}

void DriverLabWsConnection::startDriver(bool queryMeta) {
    m_process = std::make_unique<QProcess>();

    connect(m_process.get(), &QProcess::readyReadStandardOutput,
            this, &DriverLabWsConnection::onDriverStdoutReady);
    connect(m_process.get(), &QProcess::finished,
            this, &DriverLabWsConnection::onDriverFinished);
    connect(m_process.get(), &QProcess::errorOccurred,
            this, &DriverLabWsConnection::onDriverErrorOccurred);

    // Add server directory to PATH so driver can find Qt DLLs
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString pathValue = env.value("PATH");
    if (!pathValue.isEmpty()) {
        env.insert("PATH", appDir + ";" + pathValue);
    } else {
        env.insert("PATH", appDir);
    }
    m_process->setProcessEnvironment(env);

    m_lastDriverStart = QDateTime::currentDateTimeUtc();
    m_metaSent = !queryMeta;  // Skip meta parsing on restart (already have it)
    m_stdoutBuffer.clear();

    QStringList args = m_extraArgs;
    args.prepend(QStringLiteral("--profile=") + m_runMode);

    m_process->start(m_program, args);
    if (!m_process->waitForStarted(5000)) {
        sendJson(QJsonObject{
            {"type", "error"},
            {"message", "failed to start driver: " + m_process->errorString()}
        });
        // Close WebSocket on start failure
        QTimer::singleShot(0, this, [this]() {
            if (m_socket) {
                m_socket->close(QWebSocketProtocol::CloseCodeAbnormalDisconnection,
                                "driver start failed");
            }
        });
        return;
    }

    sendJson(QJsonObject{
        {"type", "driver.started"},
        {"pid", static_cast<qint64>(m_process->processId())}
    });

    // Query meta (skip on OneShot restart — we already have it)
    if (queryMeta) {
        const QByteArray metaCmd = "{\"cmd\":\"meta.describe\",\"data\":{}}\n";
        m_process->write(metaCmd);

        // Meta timeout
        QTimer::singleShot(5000, this, [this]() {
            if (!m_metaSent && m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
                sendJson(QJsonObject{
                    {"type", "error"},
                    {"message", "meta query timeout"}
                });
            }
        });
    }
}

void DriverLabWsConnection::stopDriver() {
    if (!m_process) {
        return;
    }

    if (m_process->state() != QProcess::NotRunning) {
        m_process->closeWriteChannel();
        if (!m_process->waitForFinished(2000)) {
            m_process->terminate();
            if (!m_process->waitForFinished(2000)) {
                m_process->kill();
                m_process->waitForFinished(1000);
            }
        }
    }

    m_process.reset();
}

void DriverLabWsConnection::sendJson(const QJsonObject& msg) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    m_socket->sendTextMessage(
        QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void DriverLabWsConnection::onTextMessageReceived(const QString& message) {
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        sendJson(QJsonObject{
            {"type", "error"},
            {"message", "invalid JSON"}
        });
        return;
    }

    const QJsonObject msg = doc.object();
    const QString type = msg.value("type").toString();

    if (type == "exec") {
        handleExecMessage(msg);
    } else if (type == "cancel") {
        handleCancelMessage();
    } else {
        sendJson(QJsonObject{
            {"type", "error"},
            {"message", "unknown message type: " + type}
        });
    }
}

void DriverLabWsConnection::handleExecMessage(const QJsonObject& msg) {
    // If driver is not running in oneshot mode, restart it
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        if (m_runMode == "oneshot") {
            if (m_restartSuppressed) {
                sendJson(QJsonObject{
                    {"type", "error"},
                    {"message", "driver restart suppressed due to rapid crashes"}
                });
                return;
            }
            restartDriverForOneShot();
        } else {
            sendJson(QJsonObject{
                {"type", "error"},
                {"message", "driver is not running"}
            });
            return;
        }
    }

    // Forward command to driver stdin
    QJsonObject cmd;
    cmd["cmd"] = msg.value("cmd");
    if (msg.contains("data")) {
        cmd["data"] = msg.value("data");
    }

    const QByteArray line = QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n";
    m_process->write(line);
}

void DriverLabWsConnection::handleCancelMessage() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->closeWriteChannel();
    }
}

void DriverLabWsConnection::onSocketDisconnected() {
    m_closing = true;
    stopDriver();
    emit closed(this);
}

void DriverLabWsConnection::onDriverStdoutReady() {
    if (!m_process) {
        return;
    }

    m_stdoutBuffer.append(m_process->readAllStandardOutput());

    while (true) {
        const int nlIndex = m_stdoutBuffer.indexOf('\n');
        if (nlIndex < 0) {
            break;
        }

        const QByteArray line = m_stdoutBuffer.left(nlIndex).trimmed();
        m_stdoutBuffer.remove(0, nlIndex + 1);

        if (line.isEmpty()) {
            continue;
        }

        // First ok response is treated as meta
        if (!m_metaSent) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject obj = doc.object();
                if (obj.value("status").toString() == "done") {
                    m_metaSent = true;
                    sendJson(QJsonObject{
                        {"type", "meta"},
                        {"driverId", m_driverId},
                        {"pid", m_process ? static_cast<qint64>(m_process->processId()) : 0},
                        {"runMode", m_runMode},
                        {"meta", obj.value("data")}
                    });
                    continue;
                }
            }
        }

        forwardStdoutLine(line);
    }
}

void DriverLabWsConnection::forwardStdoutLine(const QByteArray& line) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);

    QJsonObject msg;
    msg["type"] = QStringLiteral("stdout");
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
        msg["message"] = doc.object();
    } else {
        msg["message"] = QString::fromUtf8(line);
    }
    sendJson(msg);
}

void DriverLabWsConnection::onDriverFinished(int exitCode, QProcess::ExitStatus status) {
    if (m_closing) {
        return;
    }

    const QString exitStatus = (status == QProcess::NormalExit) ? "normal" : "crash";
    const QString reason = (status == QProcess::NormalExit)
                               ? "driver exited normally"
                               : "driver crashed";

    m_lastExitWasCrash = (status != QProcess::NormalExit || exitCode != 0);

    sendJson(QJsonObject{
        {"type", "driver.exited"},
        {"exitCode", exitCode},
        {"exitStatus", exitStatus},
        {"reason", reason}
    });

    if (m_runMode == "keepalive") {
        // KeepAlive: close WebSocket when driver exits
        if (m_socket) {
            m_socket->close(QWebSocketProtocol::CloseCodeNormal, reason);
        }
    }
    // OneShot: keep WebSocket open, wait for next exec to auto-restart
}

void DriverLabWsConnection::onDriverErrorOccurred(QProcess::ProcessError error) {
    if (m_closing) {
        return;
    }

    if (error == QProcess::FailedToStart) {
        sendJson(QJsonObject{
            {"type", "error"},
            {"message", "driver failed to start: " + m_process->errorString()}
        });
        if (m_socket) {
            m_socket->close(QWebSocketProtocol::CloseCodeAbnormalDisconnection,
                            "driver start failed");
        }
    }
}

void DriverLabWsConnection::restartDriverForOneShot() {
    // Check crash backoff — only count actual crashes, not normal OneShot exits
    if (m_lastExitWasCrash) {
        const qint64 elapsed = m_lastDriverStart.msecsTo(QDateTime::currentDateTimeUtc());
        if (elapsed < kRapidCrashWindowMs) {
            m_consecutiveFastCrashes++;
        } else {
            m_consecutiveFastCrashes = 0;
        }
    } else {
        m_consecutiveFastCrashes = 0;
    }

    if (m_consecutiveFastCrashes >= kMaxRapidCrashes) {
        m_restartSuppressed = true;
        sendJson(QJsonObject{
            {"type", "error"},
            {"message", "driver restart suppressed: too many rapid crashes"}
        });
        return;
    }

    // Clean up old process
    m_process.reset();

    // Start new driver (skip meta query — we already have it)
    startDriver(false);

    if (m_process && m_process->state() == QProcess::Running) {
        sendJson(QJsonObject{
            {"type", "driver.restarted"},
            {"pid", static_cast<qint64>(m_process->processId())},
            {"reason", "oneshot auto-restart"}
        });
    }
}

} // namespace stdiolink_server
