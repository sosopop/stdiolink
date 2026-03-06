#pragma once

#include "stdiolink/stdiolink_export.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace stdiolink {

enum class CliValueMode {
    Canonical,
    Friendly
};

struct RawCliArg {
    QString path;
    QString rawValue;
};

struct CliParseOptions {
    CliValueMode mode = CliValueMode::Friendly;
};

struct CliRenderOptions {
    CliValueMode mode = CliValueMode::Canonical;
    bool useEquals = true;
};

struct CliPathSegment {
    enum class Kind {
        Key,
        Index,
        Append
    };

    Kind kind = Kind::Key;
    QString key;
    int index = -1;
};

using CliPath = QVector<CliPathSegment>;

class STDIOLINK_API JsonCliCodec {
public:
    static bool parsePath(const QString& path, CliPath& out, QString* error);

    static bool parseArgs(const QList<RawCliArg>& args,
                          const CliParseOptions& opts,
                          QJsonObject& out,
                          QString* error);

    static QStringList renderArgs(const QJsonObject& data,
                                  const CliRenderOptions& opts = CliRenderOptions{});
};

} // namespace stdiolink
