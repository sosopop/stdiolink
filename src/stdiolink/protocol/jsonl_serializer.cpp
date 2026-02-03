#include "jsonl_serializer.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>

namespace stdiolink {

QByteArray serializeRequest(const QString& cmd, const QJsonValue& data)
{
    QJsonObject req;
    req["cmd"] = cmd;

    if (!data.isNull() && !data.isUndefined()) {
        req["data"] = data;
    }

    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    line.append('\n');
    return line;
}

QByteArray serializeResponse(const QString& status, int code, const QJsonValue& payload)
{
    // Header 行
    QJsonObject header;
    header["status"] = status;
    header["code"] = code;

    QByteArray result = QJsonDocument(header).toJson(QJsonDocument::Compact);
    result.append('\n');

    // Payload 行
    if (payload.isObject()) {
        result.append(QJsonDocument(payload.toObject()).toJson(QJsonDocument::Compact));
    } else if (payload.isArray()) {
        result.append(QJsonDocument(payload.toArray()).toJson(QJsonDocument::Compact));
    } else {
        // 对于基本类型，包装成单值数组再提取
        QJsonArray arr;
        arr.append(payload);
        QByteArray tmp = QJsonDocument(arr).toJson(QJsonDocument::Compact);
        // 去掉 [ 和 ]
        result.append(tmp.mid(1, tmp.size() - 2));
    }
    result.append('\n');

    return result;
}

} // namespace stdiolink
