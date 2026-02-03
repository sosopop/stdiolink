#include "console_responder.h"
#include "stdiolink/protocol/jsonl_serializer.h"
#include <QFile>

namespace stdiolink {

void ConsoleResponder::event(int code, const QJsonValue& payload)
{
    // Console 模式下 event 输出到 stderr
    writeToStderr("event", code, payload);
}

void ConsoleResponder::done(int code, const QJsonValue& payload)
{
    exitCode_ = code;
    hasResult_ = true;
    writeToStdout("done", code, payload);
}

void ConsoleResponder::error(int code, const QJsonValue& payload)
{
    exitCode_ = code != 0 ? code : 1;
    hasResult_ = true;
    writeToStdout("error", code, payload);
}

void ConsoleResponder::writeToStdout(const QString& status, int code, const QJsonValue& payload)
{
    static QFile output;
    if (!output.isOpen()) {
        output.open(stdout, QIODevice::WriteOnly);
    }

    QByteArray data = serializeResponse(status, code, payload);
    output.write(data);
    output.flush();
}

void ConsoleResponder::writeToStderr(const QString& status, int code, const QJsonValue& payload)
{
    static QFile errOutput;
    if (!errOutput.isOpen()) {
        errOutput.open(stderr, QIODevice::WriteOnly);
    }

    QByteArray data = serializeResponse(status, code, payload);
    errOutput.write(data);
    errOutput.flush();
}

} // namespace stdiolink
