#include "driver.h"
#include "protocol/jsonl_serializer.h"
#include <QJsonDocument>

namespace stdiolink {

Driver::~Driver()
{
    terminate();
}

bool Driver::start(const QString& program, const QStringList& args)
{
    proc.setProgram(program);
    proc.setArguments(args);
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start();
    return proc.waitForStarted(3000);
}

void Driver::terminate()
{
    if (proc.state() != QProcess::NotRunning) {
        proc.terminate();
        if (!proc.waitForFinished(1000)) {
            proc.kill();
        }
    }
}

bool Driver::isRunning() const
{
    return proc.state() == QProcess::Running;
}

Task Driver::request(const QString& cmd, const QJsonObject& data)
{
    cur = std::make_shared<TaskState>();

    QJsonObject req;
    req["cmd"] = cmd;
    if (!data.isEmpty()) req["data"] = data;

    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    line.append('\n');
    proc.write(line);
    proc.waitForBytesWritten(1000);

    waitingHeader = true;
    buf.clear();

    return Task(this, cur);
}

bool Driver::hasQueued() const
{
    return cur && !cur->queue.empty();
}

bool Driver::isCurrentTerminal() const
{
    return cur && cur->terminal;
}

bool Driver::tryReadLine(QByteArray& outLine)
{
    int idx = buf.indexOf('\n');
    if (idx < 0) return false;
    outLine = buf.left(idx);
    buf.remove(0, idx + 1);
    return true;
}

void Driver::pushError(int code, const QJsonObject& payload)
{
    Message msg{"error", code, payload};
    cur->queue.push_back(msg);
    cur->terminal = true;
    cur->exitCode = code;
    cur->finalPayload = payload;

    if (payload.contains("message")) {
        cur->errorText = payload["message"].toString();
    }
}

void Driver::pumpStdout()
{
    if (!cur) return;

    buf.append(proc.readAllStandardOutput());

    QByteArray line;
    while (tryReadLine(line)) {
        if (waitingHeader) {
            if (!parseHeader(line, hdr)) {
                pushError(1000, QJsonObject{
                    {"message", "invalid header"},
                    {"raw", QString::fromUtf8(line)}
                });
                return;
            }
            waitingHeader = false;
        } else {
            QJsonValue payload = parsePayload(line);
            Message msg{hdr.status, hdr.code, payload};
            cur->queue.push_back(msg);

            if (hdr.status == "done" || hdr.status == "error") {
                cur->terminal = true;
                cur->exitCode = hdr.code;
                cur->finalPayload = payload;

                if (hdr.status == "error" && payload.isObject()) {
                    auto obj = payload.toObject();
                    if (obj.contains("message")) {
                        cur->errorText = obj["message"].toString();
                    }
                }
            }
            waitingHeader = true;
        }
    }
}

} // namespace stdiolink
