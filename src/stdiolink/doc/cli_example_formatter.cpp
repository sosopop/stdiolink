#include "cli_example_formatter.h"

#include "stdiolink/console/json_cli_codec.h"

#include <QJsonDocument>
#include <QStringList>

namespace stdiolink {

QString formatCliExampleCommand(const QString& cmdName, const QJsonObject& ex) {
    const QString mode = ex.value("mode").toString();
    const QJsonObject params = ex.value("params").toObject();

    if (mode == "stdio") {
        return "echo '<jsonl>' | <program> --mode=stdio";
    }

    QStringList parts;
    parts << "<program>" << ("--cmd=" + cmdName);

    if (!mode.isEmpty() && mode != "console") {
        parts << ("--mode=" + mode);
    }

    const QJsonValue profileValue = params.value("profile");
    const QString profile = profileValue.isString() ? profileValue.toString() : QString();
    if (!profile.isEmpty() && profile != "oneshot") {
        parts << ("--profile=" + profile);
    }

    QJsonObject dataParams = params;
    dataParams.remove("mode");
    dataParams.remove("profile");
    parts.append(JsonCliCodec::renderArgs(dataParams, CliRenderOptions{}));
    return parts.join(' ');
}

QString formatCliExampleStdinLine(const QString& cmdName, const QJsonObject& ex) {
    const QJsonObject req{
        {"cmd", cmdName},
        {"data", ex.value("params").toObject()},
    };
    return QString::fromUtf8(QJsonDocument(req).toJson(QJsonDocument::Compact));
}

} // namespace stdiolink
