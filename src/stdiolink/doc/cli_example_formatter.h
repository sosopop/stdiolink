#pragma once

#include "stdiolink/stdiolink_export.h"

#include <QJsonObject>
#include <QString>

namespace stdiolink {

STDIOLINK_API QString formatCliExampleCommand(const QString& cmdName, const QJsonObject& ex);

STDIOLINK_API QString formatCliExampleStdinLine(const QString& cmdName, const QJsonObject& ex);

} // namespace stdiolink
