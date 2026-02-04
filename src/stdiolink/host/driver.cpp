#include "driver.h"
#include <QJsonDocument>
#include "stdiolink/protocol/jsonl_serializer.h"

namespace stdiolink {

Driver::~Driver() {
    terminate();
}

bool Driver::start(const QString& program, const QStringList& args) {
    m_proc.setProgram(program);
    m_proc.setArguments(args);
    m_proc.setProcessChannelMode(QProcess::SeparateChannels);
    m_proc.start();
    return m_proc.waitForStarted(3000);
}

void Driver::terminate() {
    if (m_proc.state() != QProcess::NotRunning) {
        m_proc.terminate();
        if (!m_proc.waitForFinished(1000)) {
            m_proc.kill();
        }
    }
}

bool Driver::isRunning() const {
    return m_proc.state() == QProcess::Running;
}

Task Driver::request(const QString& cmd, const QJsonObject& data) {
    m_cur = std::make_shared<TaskState>();

    QJsonObject req;
    req["cmd"] = cmd;
    if (!data.isEmpty())
        req["data"] = data;

    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    line.append('\n');
    m_proc.write(line);
    m_proc.waitForBytesWritten(1000);

    m_waitingHeader = true;
    m_buf.clear();

    return {this, m_cur};
}

bool Driver::hasQueued() const {
    return m_cur && !m_cur->queue.empty();
}

bool Driver::isCurrentTerminal() const {
    return m_cur && m_cur->terminal;
}

bool Driver::tryReadLine(QByteArray& outLine) {
    int idx = m_buf.indexOf('\n');
    if (idx < 0)
        return false;
    outLine = m_buf.left(idx);
    m_buf.remove(0, idx + 1);
    return true;
}

void Driver::pushError(int code, const QJsonObject& payload) {
    Message msg{"error", code, payload};
    m_cur->queue.push_back(msg);
    m_cur->terminal = true;
    m_cur->exitCode = code;
    m_cur->finalPayload = payload;

    if (payload.contains("message")) {
        m_cur->errorText = payload["message"].toString();
    }
}

void Driver::pumpStdout() {
    if (!m_cur)
        return;

    m_buf.append(m_proc.readAllStandardOutput());

    QByteArray line;
    while (tryReadLine(line)) {
        if (m_waitingHeader) {
            if (!parseHeader(line, m_hdr)) {
                pushError(1000, QJsonObject{{"message", "invalid header"},
                                            {"raw", QString::fromUtf8(line)}});
                return;
            }
            m_waitingHeader = false;
        } else {
            QJsonValue payload = parsePayload(line);
            Message msg{m_hdr.status, m_hdr.code, payload};
            m_cur->queue.push_back(msg);

            if (m_hdr.status == "done" || m_hdr.status == "error") {
                m_cur->terminal = true;
                m_cur->exitCode = m_hdr.code;
                m_cur->finalPayload = payload;

                if (m_hdr.status == "error" && payload.isObject()) {
                    auto obj = payload.toObject();
                    if (obj.contains("message")) {
                        m_cur->errorText = obj["message"].toString();
                    }
                }
            }
            m_waitingHeader = true;
        }
    }
}

} // namespace stdiolink
