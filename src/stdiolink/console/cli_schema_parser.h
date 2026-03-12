#pragma once

#include "stdiolink/stdiolink_export.h"

#include <QJsonObject>
#include <QVector>

#include "stdiolink/console/json_cli_codec.h"
#include "stdiolink/protocol/meta_types.h"

namespace stdiolink {

class STDIOLINK_API CliSchemaParser {
public:
    static bool parseArgs(const QList<RawCliArg>& rawArgs,
                          const QVector<meta::FieldMeta>& rootFields,
                          QJsonObject& out,
                          QString* error,
                          int* errorArgIndex = nullptr);
};

} // namespace stdiolink
