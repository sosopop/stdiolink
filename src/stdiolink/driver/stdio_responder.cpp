#include "stdio_responder.h"
#include <QFile>
#include "stdiolink/protocol/jsonl_serializer.h"

namespace stdiolink {

void StdioResponder::event(int code, const QJsonValue& payload) {
    writeResponse("event", code, payload);
}

void StdioResponder::event(const QString& eventName, int code, const QJsonValue& data) {
    QJsonObject payload;
    payload["event"] = eventName;
    payload["data"] = data;
    writeResponse("event", code, payload);
}

void StdioResponder::done(int code, const QJsonValue& payload) {
    writeResponse("done", code, payload);
}

void StdioResponder::error(int code, const QJsonValue& payload) {
    writeResponse("error", code, payload);
}

void StdioResponder::writeResponse(const QString& status, int code, const QJsonValue& payload) {
    static QFile output;
    if (!output.isOpen()) {
        (void)output.open(stdout, QIODevice::WriteOnly);
    }

    QByteArray data = serializeResponse(status, code, payload);
    output.write(data);
    output.flush();
}

} // namespace stdiolink
