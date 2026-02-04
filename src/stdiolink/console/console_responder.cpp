#include "console_responder.h"
#include <QFile>
#include "stdiolink/protocol/jsonl_serializer.h"

namespace stdiolink {

void ConsoleResponder::event(int code, const QJsonValue& payload) {
    // Console 模式下 event 输出到 stderr
    writeToStderr("event", code, payload);
}

void ConsoleResponder::done(int code, const QJsonValue& payload) {
    m_exitCode = code;
    m_hasResult = true;
    writeToStdout("done", code, payload);
}

void ConsoleResponder::error(int code, const QJsonValue& payload) {
    m_exitCode = code != 0 ? code : 1;
    m_hasResult = true;
    writeToStdout("error", code, payload);
}

void ConsoleResponder::writeToStdout(const QString& status, int code, const QJsonValue& payload) {
    static QFile output;
    if (!output.isOpen()) {
        output.open(stdout, QIODevice::WriteOnly);
    }

    QByteArray data = serializeResponse(status, code, payload);
    output.write(data);
    output.flush();
}

void ConsoleResponder::writeToStderr(const QString& status, int code, const QJsonValue& payload) {
    static QFile errOutput;
    if (!errOutput.isOpen()) {
        errOutput.open(stderr, QIODevice::WriteOnly);
    }

    QByteArray data = serializeResponse(status, code, payload);
    errOutput.write(data);
    errOutput.flush();
}

} // namespace stdiolink
