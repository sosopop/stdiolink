#include "stdio_responder.h"
#include "protocol/jsonl_serializer.h"
#include <QFile>

namespace stdiolink {

void StdioResponder::event(int code, const QJsonValue& payload)
{
    writeResponse("event", code, payload);
}

void StdioResponder::done(int code, const QJsonValue& payload)
{
    writeResponse("done", code, payload);
}

void StdioResponder::error(int code, const QJsonValue& payload)
{
    writeResponse("error", code, payload);
}

void StdioResponder::writeResponse(const QString& status, int code, const QJsonValue& payload)
{
    static QFile output;
    if (!output.isOpen()) {
        output.open(stdout, QIODevice::WriteOnly);
    }

    QByteArray data = serializeResponse(status, code, payload);
    output.write(data);
    output.flush();
}

} // namespace stdiolink
