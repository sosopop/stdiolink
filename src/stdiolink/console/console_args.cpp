#include "console_args.h"
#include "system_options.h"
#include <QJsonArray>
#include <QJsonDocument>

#ifdef Q_OS_WIN
#include <io.h>
#include <cstdio>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace stdiolink {

QJsonValue inferType(const QString& value) {
    // bool
    if (value == "true")
        return true;
    if (value == "false")
        return false;

    // null
    if (value == "null")
        return QJsonValue::Null;

    // number
    bool ok;
    if (!value.contains('.')) {
        int i = value.toInt(&ok);
        if (ok)
            return i;
    }
    double d = value.toDouble(&ok);
    if (ok)
        return d;

    // JSON object/array
    if (value.startsWith('{') || value.startsWith('[')) {
        QJsonDocument doc = QJsonDocument::fromJson(value.toUtf8());
        if (!doc.isNull()) {
            if (doc.isObject())
                return doc.object();
            if (doc.isArray())
                return doc.array();
        }
    }

    // string
    return value;
}

void setNestedValue(QJsonObject& root, const QString& path, const QJsonValue& value) {
    QStringList parts = path.split('.');
    if (parts.isEmpty())
        return;

    if (parts.size() == 1) {
        root[parts[0]] = value;
        return;
    }

    // 递归处理嵌套路径
    QString firstKey = parts[0];
    parts.removeFirst();
    QString remainingPath = parts.join('.');

    QJsonObject nested = root[firstKey].toObject();
    setNestedValue(nested, remainingPath, value);
    root[firstKey] = nested;
}

bool ConsoleArgs::parse(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromUtf8(argv[i]);

        // 处理短参数
        if (arg.startsWith("-") && !arg.startsWith("--")) {
            if (!parseShortArg(arg.mid(1), i, argc, argv)) {
                return false;
            }
            continue;
        }

        // 必须以 -- 开头
        if (!arg.startsWith("--")) {
            errorMessage = QString("Invalid argument: %1").arg(arg);
            return false;
        }

        arg = arg.mid(2); // 去掉 --

        // 处理无值参数
        if (arg == "help") {
            showHelp = true;
            continue;
        }
        if (arg == "version") {
            showVersion = true;
            continue;
        }
        if (arg == "export-meta") {
            exportMeta = true;
            continue;
        }

        // 解析 key=value
        int eqPos = arg.indexOf('=');
        if (eqPos < 0) {
            errorMessage = QString("Missing value for argument: --%1").arg(arg);
            return false;
        }

        QString key = arg.left(eqPos);
        QString value = arg.mid(eqPos + 1);

        // 处理导出参数
        if (key == "export-meta") {
            exportMeta = true;
            exportMetaPath = value;
            continue;
        }
        if (key == "export-doc") {
            parseExportDoc(value);
            continue;
        }

        // 处理 --arg- 前缀（用于避免与框架参数冲突）
        if (key.startsWith("arg-")) {
            key = key.mid(4);
            parseDataArg(key, value);
        } else if (isFrameworkArg(key)) {
            parseFrameworkArg(key, value);
        } else {
            parseDataArg(key, value);
        }
    }

    // 验证必需参数
    // 如果显式指定 stdio 模式，不需要 --cmd
    if (mode == "stdio") {
        return true;
    }

    // 如果只有 --help 或 --version，不需要 --cmd
    if (showHelp || showVersion) {
        return true;
    }

    // 如果是导出命令，不需要 --cmd
    if (exportMeta || !exportDocFormat.isEmpty()) {
        return true;
    }

    // 显式 console 模式必须提供命令
    if (mode == "console" && cmd.isEmpty()) {
        errorMessage = "Console mode requires --cmd";
        return false;
    }

    // 传入 data 参数时必须提供命令
    if (!data.isEmpty() && cmd.isEmpty()) {
        errorMessage = "Data arguments require --cmd";
        return false;
    }

    // 默认允许进入 stdio 模式（可能只指定了 --profile=keepalive）
    return true;
}

bool ConsoleArgs::isFrameworkArg(const QString& key) {
    // 使用 SystemOptionRegistry 作为单一来源 (M20)
    return SystemOptionRegistry::isFrameworkArg(key);
}

void ConsoleArgs::parseFrameworkArg(const QString& key, const QString& value) {
    if (key == "mode") {
        mode = value;
    } else if (key == "profile") {
        profile = value;
    } else if (key == "cmd") {
        cmd = value;
    } else if (key == "log") {
        logPath = value;
    }
}

void ConsoleArgs::parseDataArg(const QString& key, const QString& value) {
    QJsonValue jsonValue = inferType(value);
    setNestedValue(data, key, jsonValue);
}

bool ConsoleArgs::isInteractiveStdin() {
    return isatty(fileno(stdin)) != 0;
}

bool ConsoleArgs::parseShortArg(const QString& arg, int& index, int argc, char* argv[]) {
    // 短参数映射: -h, -v, -m, -c, -E, -D
    if (arg == "h") {
        showHelp = true;
        return true;
    }
    if (arg == "v") {
        showVersion = true;
        return true;
    }
    if (arg == "E") {
        exportMeta = true;
        return true;
    }

    // 需要值的短参数
    QString value;
    if (index + 1 < argc) {
        value = QString::fromUtf8(argv[index + 1]);
        ++index;
    } else {
        errorMessage = QString("Missing value for argument: -%1").arg(arg);
        return false;
    }

    if (arg == "m") {
        mode = value;
    } else if (arg == "c") {
        cmd = value;
    } else if (arg == "D") {
        parseExportDoc(value);
    } else if (arg == "L") {
        logPath = value;
    } else {
        errorMessage = QString("Unknown short argument: -%1").arg(arg);
        return false;
    }
    return true;
}

void ConsoleArgs::parseExportDoc(const QString& value) {
    // 格式: format 或 format=path
    int eqPos = value.indexOf('=');
    if (eqPos < 0) {
        exportDocFormat = value;
    } else {
        exportDocFormat = value.left(eqPos);
        exportDocPath = value.mid(eqPos + 1);
    }
}

} // namespace stdiolink
