#include "jsonl_serializer.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>

namespace stdiolink {

QByteArray serializeRequest(const QString& cmd, const QJsonValue& data) {
    QJsonObject req;
    req["cmd"] = cmd;

    if (!data.isNull() && !data.isUndefined()) {
        req["data"] = data;
    }

    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    line.append('\n');
    return line;
}

QByteArray serializeResponse(const QString& status, int code, const QJsonValue& payload) {
    QJsonObject resp;
    resp["status"] = status;
    resp["code"] = code;
    resp["data"] = payload;

    QByteArray result = QJsonDocument(resp).toJson(QJsonDocument::Compact);
    result.append('\n');
    return result;
}

} // namespace stdiolink
