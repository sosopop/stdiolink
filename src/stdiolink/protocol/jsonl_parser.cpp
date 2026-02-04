#include "jsonl_serializer.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>

namespace stdiolink {

bool parseRequest(const QByteArray& line, Request& out)
{
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(line, &err);

    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    QJsonObject obj = doc.object();

    // cmd 是必填字段
    if (!obj.contains("cmd") || !obj["cmd"].isString()) {
        return false;
    }

    out.cmd = obj["cmd"].toString();
    out.data = obj.value("data");

    return true;
}

bool parseHeader(const QByteArray& line, FrameHeader& out)
{
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(line, &err);

    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    QJsonObject obj = doc.object();

    // status 和 code 都是必填字段
    if (!obj.contains("status") || !obj.contains("code")) {
        return false;
    }

    out.status = obj["status"].toString();
    out.code = obj["code"].toInt();

    // 验证 status 值
    return out.status == "event" || out.status == "done" || out.status == "error";
}

QJsonValue parsePayload(const QByteArray& line)
{
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(line, &err);

    if (err.error == QJsonParseError::NoError) {
        if (doc.isObject()) return doc.object();
        if (doc.isArray()) return doc.array();
    }

    // 尝试解析基本类型
    QString str = QString::fromUtf8(line).trimmed();

    // null
    if (str == "null") {
        return QJsonValue::Null;
    }

    // bool
    if (str == "true") return true;
    if (str == "false") return false;

    // number
    bool ok;
    double d = str.toDouble(&ok);
    if (ok) return d;

    // 作为字符串返回
    return str;
}

} // namespace stdiolink
