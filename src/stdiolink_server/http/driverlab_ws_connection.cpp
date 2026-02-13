#include "driverlab_ws_connection.h"

#include <QJsonDocument>
#include <QTimer>

namespace stdiolink_server {

DriverLabWsConnection::DriverLabWsConnection(std::unique_ptr<QWebSocket> socket,
                                             const QString& driverId, const QString& program,
                                             const QString& runMode, const QStringList& extraArgs,
                                             QObject* parent)
    : QObject(parent),
      m_socket(std::move(socket)),
      m_driverId(driverId),
      m_program(program),
      m_runMode(runMode),
      m_extraArgs(extraArgs) {
    connect(m_socket.get(), &QWebSocket::textMessageReceived, this,
            &DriverLabWsConnection::onTextMessageReceived);
    connect(m_socket.get(), &QWebSocket::disconnected, this,
            &DriverLabWsConnection::onSocketDisconnected);

    // Defer driver start to next event loop iteration so the WebSocket
    // handshake completes and the client can receive messages
    QTimer::singleShot(0, this, &DriverLabWsConnection::startDriver);
}

DriverLabWsConnection::~DriverLabWsConnection() {
    m_closing = true;
    stopDriver();
}

void DriverLabWsConnection::startDriver() {
    m_process = std::make_unique<QProcess>();
    m_stdoutBuffer.clear();
    m_metaSent = false;

    connect(m_process.get(), &QProcess::readyReadStandardOutput, this,
            &DriverLabWsConnection::onDriverStdoutReady);
    connect(m_process.get(), &QProcess::finished, this, &DriverLabWsConnection::onDriverFinished);
    connect(m_process.get(), &QProcess::errorOccurred, this,
            &DriverLabWsConnection::onDriverErrorOccurred);

    // When the process starts successfully, send driver.started and query meta
    connect(m_process.get(), &QProcess::started, this, [this]() {
        m_lastDriverStart = QDateTime::currentDateTimeUtc();

        QJsonObject started;
        started["type"] = QStringLiteral("driver.started");
        started["pid"] = static_cast<qint64>(m_process->processId());
        sendJson(started);

        // If this is a oneshot restart, also send driver.restarted
        if (m_restarting) {
            m_restarting = false;
            QJsonObject restarted;
            restarted["type"] = QStringLiteral("driver.restarted");
            restarted["pid"] = static_cast<qint64>(m_process->processId());
            restarted["reason"] = QStringLiteral("oneshot auto-restart on new exec");
            sendJson(restarted);
        }

        queryMeta();

        // If there's a pending exec command from the restart trigger, forward it now
        if (!m_pendingExecLine.isEmpty()) {
            m_process->write(m_pendingExecLine);
            m_pendingExecLine.clear();
        }
    });

    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    // Always start driver in keepalive profile — the WebSocket session manages lifecycle.
    // The driver's own oneshot/keepalive profile is separate from the WS runMode.
    QStringList args;
    args << QStringLiteral("--profile=keepalive");
    args << m_extraArgs;
    m_process->start(m_program, args);
}

void DriverLabWsConnection::queryMeta() {
    // Send meta.describe command to driver stdin
    QJsonObject cmd;
    cmd["cmd"] = QStringLiteral("meta.describe");
    cmd["data"] = QJsonObject();
    QByteArray line = QJsonDocument(cmd).toJson(QJsonDocument::Compact) + '\n';
    m_process->write(line);

    // Timeout: if meta not received in 5s, send error (don't close connection)
    QTimer::singleShot(5000, this, [this]() {
        if (!m_metaSent && m_socket && m_socket->isValid()) {
            QJsonObject err;
            err["type"] = QStringLiteral("error");
            err["message"] = QStringLiteral("meta query timed out");
            sendJson(err);
            m_metaSent = true; // prevent further meta processing
        }
    });
}

void DriverLabWsConnection::stopDriver() {
    if (!m_process)
        return;

    // Disconnect signals to avoid re-entrant handling
    m_process->disconnect(this);

    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
            m_process->waitForFinished(2000);
        }
    }
    m_process.reset();
}

void DriverLabWsConnection::sendJson(const QJsonObject& msg) {
    if (m_socket && m_socket->isValid()) {
        m_socket->sendTextMessage(
            QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    }
}

void DriverLabWsConnection::forwardStdoutLine(const QByteArray& line) {
    auto doc = QJsonDocument::fromJson(line);

    // If meta not yet sent, check if this is the meta.describe response.
    // JSONL protocol format: {"status":"done","code":0,"data":{...meta...}}
    // An error response has: {"status":"error","code":501,"data":{...}}
    if (!m_metaSent && doc.isObject()) {
        QJsonObject obj = doc.object();
        if (obj.value("status").toString() == QStringLiteral("done")) {
            m_metaSent = true;
            QJsonObject metaMsg;
            metaMsg["type"] = QStringLiteral("meta");
            metaMsg["driverId"] = m_driverId;
            metaMsg["pid"] = static_cast<qint64>(m_process ? m_process->processId() : 0);
            metaMsg["runMode"] = m_runMode;
            metaMsg["meta"] = obj.value("data");
            sendJson(metaMsg);
            return;
        }
        // If the driver doesn't support meta (status=error), mark meta as sent
        // and fall through to forward as stdout
        if (obj.value("status").toString() == QStringLiteral("error")) {
            m_metaSent = true;
        }
    }

    // Regular stdout forwarding
    QJsonObject msg;
    msg["type"] = QStringLiteral("stdout");
    msg["message"] =
        doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(QString::fromUtf8(line));
    sendJson(msg);
}

void DriverLabWsConnection::onDriverStdoutReady() {
    if (!m_process)
        return;

    m_stdoutBuffer.append(m_process->readAllStandardOutput());

    while (true) {
        int nlIndex = m_stdoutBuffer.indexOf('\n');
        if (nlIndex < 0)
            break;

        QByteArray line = m_stdoutBuffer.left(nlIndex).trimmed();
        m_stdoutBuffer.remove(0, nlIndex + 1);

        if (!line.isEmpty())
            forwardStdoutLine(line);
    }
}

void DriverLabWsConnection::onDriverFinished(int exitCode, QProcess::ExitStatus status) {
    if (m_closing)
        return;

    // Push driver.exited
    QJsonObject exitMsg;
    exitMsg["type"] = QStringLiteral("driver.exited");
    exitMsg["exitCode"] = exitCode;
    exitMsg["exitStatus"] =
        (status == QProcess::CrashExit) ? QStringLiteral("crash") : QStringLiteral("normal");
    exitMsg["reason"] = (status == QProcess::CrashExit) ? QStringLiteral("process crashed")
                                                        : QStringLiteral("process exited");
    sendJson(exitMsg);

    if (m_runMode == "keepalive") {
        // KeepAlive: Driver exits → close WebSocket
        m_socket->close();
        return;
    }

    // OneShot: Driver exits → keep WebSocket open, track crash backoff
    auto now = QDateTime::currentDateTimeUtc();
    if (m_lastDriverStart.isValid() && m_lastDriverStart.msecsTo(now) < kRapidCrashWindowMs) {
        m_consecutiveFastExits++;
    } else {
        m_consecutiveFastExits = 1;
    }

    if (m_consecutiveFastExits >= kMaxRapidCrashes) {
        m_restartSuppressed = true;
        QJsonObject err;
        err["type"] = QStringLiteral("error");
        err["message"] = QStringLiteral("driver crashed %1 times rapidly, auto-restart suppressed")
                             .arg(kMaxRapidCrashes);
        sendJson(err);
    }

    // Clean up process but keep socket open
    m_process.reset();
}

void DriverLabWsConnection::onDriverErrorOccurred(QProcess::ProcessError error) {
    if (m_closing)
        return;

    // Only handle FailedToStart here; other errors will come through onDriverFinished
    if (error == QProcess::FailedToStart) {
        QJsonObject err;
        err["type"] = QStringLiteral("error");
        err["message"] = QStringLiteral("driver failed to start: ") +
                         (m_process ? m_process->errorString() : QStringLiteral("unknown error"));
        sendJson(err);
        m_socket->close();
    }
}

void DriverLabWsConnection::onTextMessageReceived(const QString& message) {
    auto doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        QJsonObject err;
        err["type"] = QStringLiteral("error");
        err["message"] = QStringLiteral("invalid JSON");
        sendJson(err);
        return;
    }

    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    if (type == "exec") {
        handleExecMessage(obj);
    } else if (type == "cancel") {
        handleCancelMessage();
    } else {
        QJsonObject err;
        err["type"] = QStringLiteral("error");
        err["message"] = QStringLiteral("unknown message type: ") + type;
        sendJson(err);
    }
}

void DriverLabWsConnection::handleExecMessage(const QJsonObject& msg) {
    // Build the command JSON to write to driver stdin
    QJsonObject cmd;
    cmd["cmd"] = msg["cmd"];
    if (msg.contains("data")) {
        cmd["data"] = msg["data"];
    }
    QByteArray line = QJsonDocument(cmd).toJson(QJsonDocument::Compact) + '\n';

    // If driver is not running in oneshot mode, auto-restart
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        if (m_runMode == "oneshot") {
            if (m_restartSuppressed) {
                QJsonObject err;
                err["type"] = QStringLiteral("error");
                err["message"] = QStringLiteral(
                    "auto-restart suppressed due to rapid crashes, reconnect to reset");
                sendJson(err);
                return;
            }
            // Store the exec command to forward after restart completes
            m_pendingExecLine = line;
            restartDriverForOneShot();
            return;
        } else {
            QJsonObject err;
            err["type"] = QStringLiteral("error");
            err["message"] = QStringLiteral("driver is not running");
            sendJson(err);
            return;
        }
    }

    m_process->write(line);
}

void DriverLabWsConnection::handleCancelMessage() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->closeWriteChannel();
    }
}

void DriverLabWsConnection::restartDriverForOneShot() {
    // Clean up old process
    m_process.reset();
    m_stdoutBuffer.clear();
    m_restarting = true;

    startDriver();
}

void DriverLabWsConnection::onSocketDisconnected() {
    m_closing = true;
    stopDriver();
    emit closed(this);
}

} // namespace stdiolink_server
