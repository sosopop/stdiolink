#pragma once

#include "stdiolink/stdiolink_export.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace stdiolink {

struct STDIOLINK_API Request {
    QString cmd;
    QJsonValue data;
};

struct STDIOLINK_API FrameHeader {
    QString status;
    int code = 0;
};

struct STDIOLINK_API Message {
    QString status;
    int code = 0;
    QJsonValue payload;
};

} // namespace stdiolink
