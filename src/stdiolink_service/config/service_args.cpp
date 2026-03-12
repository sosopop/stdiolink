#include "service_args.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <cstdio>
#include "stdiolink/console/json_cli_codec.h"

namespace stdiolink_service {

ServiceArgs::ParseResult ServiceArgs::parse(const QStringList& appArgs) {
    ParseResult result;

    // appArgs[0] is the executable name, skip it
    const int count = appArgs.size();
    if (count < 2) {
        result.error = "no service directory provided";
        return result;
    }

    QString serviceDir;

    for (int i = 1; i < count; ++i) {
        const QString& arg = appArgs[i];

        if (arg == "--help" || arg == "-h") {
            result.help = true;
            continue;
        }
        if (arg == "--version" || arg == "-v") {
            result.version = true;
            return result;
        }
        if (arg == "--dump-config-schema") {
            result.dumpSchema = true;
            continue;
        }
        if (arg.startsWith("--guard=")) {
            result.guardName = arg.mid(8); // len("--guard=") == 8
            continue;
        }
        if (arg.startsWith("--config-file=")) {
            result.configFilePath = arg.mid(14); // len("--config-file=") == 14
            continue;
        }
        if (arg.startsWith("--data-root=")) {
            result.dataRoot = arg.mid(QString("--data-root=").length());
            continue;
        }
        if (arg.startsWith("--config.")) {
            const int eqPos = arg.indexOf('=');
            if (eqPos < 0) {
                result.error = QString("missing '=' in config argument: %1").arg(arg);
                return result;
            }
            const QString keyPath = arg.mid(9, eqPos - 9); // len("--config.") == 9
            const QString rawValue = arg.mid(eqPos + 1);

            result.rawCliConfigArgs.append(stdiolink::RawCliArg{keyPath, rawValue});
            QString err;
            QJsonObject parsedForValidation;
            if (!stdiolink::JsonCliCodec::parseArgs(
                    result.rawCliConfigArgs,
                    stdiolink::CliParseOptions{stdiolink::CliValueMode::Friendly},
                    parsedForValidation,
                    &err)) {
                result.error = QString("invalid config argument '%1': %2").arg(arg, err);
                return result;
            }
            continue;
        }

        // Non-option argument: treat as service directory
        if (serviceDir.isEmpty() && !arg.startsWith("--")) {
            serviceDir = arg;
            continue;
        }

        // Unknown option
        if (arg.startsWith("--")) {
            result.error = QString("unknown option: %1").arg(arg);
            return result;
        }

        // Extra positional argument
        result.error = QString("unexpected argument: %1").arg(arg);
        return result;
    }

    if (serviceDir.isEmpty() && !result.help) {
        result.error = "no service directory provided";
        return result;
    }

    result.serviceDir = serviceDir;
    return result;
}

namespace {
constexpr qint64 kMaxConfigFileBytes = 1 * 1024 * 1024; // 1MB
} // namespace

QJsonObject ServiceArgs::loadConfigFile(const QString& filePath, QString& error) {
    QFile file;
    if (filePath == "-") {
        if (!file.open(stdin, QIODevice::ReadOnly)) {
            error = "cannot read config from stdin";
            return {};
        }
    } else {
        file.setFileName(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            error = QString("cannot open config file: %1").arg(filePath);
            return {};
        }
    }

    const qint64 fileSize = file.size();
    if (fileSize > kMaxConfigFileBytes) {
        if (filePath == "-") {
            error = QString("config from stdin too large (limit %1 bytes)")
                        .arg(kMaxConfigFileBytes);
        } else {
            error = QString("config file too large (%1 bytes, limit %2)")
                        .arg(fileSize)
                        .arg(kMaxConfigFileBytes);
        }
        return {};
    }

    // For sequential devices (pipes, stdin) file.size() is unreliable
    // (typically returns 0). Use incremental reading with a hard cap
    // to avoid unbounded memory allocation.
    QByteArray data;
    if (file.isSequential()) {
        constexpr int kChunkSize = 64 * 1024;
        while (!file.atEnd()) {
            data.append(file.read(kChunkSize));
            if (data.size() > kMaxConfigFileBytes) {
                file.close();
                error = QString("config input too large (limit %1 bytes)")
                            .arg(kMaxConfigFileBytes);
                return {};
            }
        }
    } else {
        data = file.readAll();
    }
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = QString("config file JSON parse error: %1").arg(parseError.errorString());
        return {};
    }

    if (!doc.isObject()) {
        error = "config file must contain a JSON object";
        return {};
    }

    return doc.object();
}

} // namespace stdiolink_service
