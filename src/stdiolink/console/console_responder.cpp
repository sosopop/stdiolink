#include "console_responder.h"
#include <QFile>
#include "stdiolink/protocol/jsonl_serializer.h"

namespace stdiolink {

namespace {

constexpr int kPortableProcessErrorExitCode = 3;

int normalizeExitCode(int code) {
    if (code == 0) {
        return 1;
    }
    if (code > 255) {
        return kPortableProcessErrorExitCode;
    }
    return code;
}

} // namespace

void ConsoleResponder::event(int code, const QJsonValue& payload) {
    writeToStdout("event", code, payload);
}

void ConsoleResponder::event(const QString& eventName, int code, const QJsonValue& data) {
    QJsonObject payload;
    payload["event"] = eventName;
    payload["data"] = data;
    writeToStdout("event", code, payload);
}

void ConsoleResponder::done(int code, const QJsonValue& payload) {
    m_exitCode = code;
    m_hasResult = true;
    writeToStdout("done", code, payload);
}

void ConsoleResponder::error(int code, const QJsonValue& payload) {
    m_exitCode = normalizeExitCode(code);
    m_hasResult = true;
    writeToStdout("error", code, payload);
}

void ConsoleResponder::writeToStdout(const QString& status, int code, const QJsonValue& payload) {
    static QFile output;
    if (!output.isOpen()) {
        (void)output.open(stdout, QIODevice::WriteOnly);
    }

    QByteArray data = serializeResponse(status, code, payload);
    output.write(data);
    output.flush();
}

} // namespace stdiolink
