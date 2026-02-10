#include "system_options.h"

namespace stdiolink {

const QVector<SystemOptionMeta>& SystemOptionRegistry::options() {
    static const QVector<SystemOptionMeta> opts = {
        {"help", "h", "", "Show help", {}, "", false},
        {"version", "v", "", "Show version", {}, "", false},
        {"mode", "m", "<mode>", "Run mode", {"stdio", "console"}, "stdio", true},
        {"profile", "", "<profile>", "Execution profile", {"oneshot", "keepalive"}, "oneshot", true},
        {"cmd", "c", "<command>", "Execute command", {}, "", true},
        {"export-meta", "E", "[=path]", "Export metadata as JSON", {}, "", false},
        {"export-doc", "D", "<fmt>[=path]", "Export documentation",
         {"markdown", "openapi", "html", "ts", "typescript", "dts"}, "", true}
    };
    return opts;
}

QVector<SystemOptionMeta> SystemOptionRegistry::list() {
    return options();
}

const SystemOptionMeta* SystemOptionRegistry::findLong(const QString& name) {
    for (const auto& opt : options()) {
        if (opt.longName == name) {
            return &opt;
        }
    }
    return nullptr;
}

const SystemOptionMeta* SystemOptionRegistry::findShort(const QString& name) {
    for (const auto& opt : options()) {
        if (!opt.shortName.isEmpty() && opt.shortName == name) {
            return &opt;
        }
    }
    return nullptr;
}

bool SystemOptionRegistry::isFrameworkArg(const QString& longName) {
    return findLong(longName) != nullptr;
}

bool SystemOptionRegistry::isFrameworkShortArg(const QString& shortName) {
    return findShort(shortName) != nullptr;
}

} // namespace stdiolink
