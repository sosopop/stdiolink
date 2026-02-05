#pragma once

#include "stdiolink/stdiolink_export.h"

#include <QByteArray>
#include <QJsonDocument>
#include "jsonl_types.h"

namespace stdiolink {

STDIOLINK_API QByteArray serializeRequest(const QString& cmd, const QJsonValue& data = QJsonValue());

STDIOLINK_API QByteArray serializeResponse(const QString& status, int code, const QJsonValue& payload);

STDIOLINK_API bool parseRequest(const QByteArray& line, Request& out);

STDIOLINK_API bool parseHeader(const QByteArray& line, FrameHeader& out);

STDIOLINK_API QJsonValue parsePayload(const QByteArray& line);

} // namespace stdiolink
