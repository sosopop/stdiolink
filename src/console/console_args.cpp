#include "console_args.h"
#include <QJsonDocument>
#include <QJsonArray>

namespace stdiolink {

QJsonValue inferType(const QString& value)
{
    // bool
    if (value == "true") return true;
    if (value == "false") return false;

    // null
    if (value == "null") return QJsonValue::Null;

    // number
    bool ok;
    if (!value.contains('.')) {
        int i = value.toInt(&ok);
        if (ok) return i;
    }
    double d = value.toDouble(&ok);
    if (ok) return d;

    // JSON object/array
    if (value.startsWith('{') || value.startsWith('[')) {
        QJsonDocument doc = QJsonDocument::fromJson(value.toUtf8());
        if (!doc.isNull()) {
            if (doc.isObject()) return doc.object();
            if (doc.isArray()) return doc.array();
        }
    }

    // string
    return value;
}

void setNestedValue(QJsonObject& root, const QString& path, const QJsonValue& value)
{
    QStringList parts = path.split('.');
    if (parts.isEmpty()) return;

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

bool ConsoleArgs::parse(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromUtf8(argv[i]);

        // 必须以 -- 开头
        if (!arg.startsWith("--")) {
            errorMessage = QString("Invalid argument: %1").arg(arg);
            return false;
        }

        arg = arg.mid(2);  // 去掉 --

        // 处理无值参数
        if (arg == "help") {
            showHelp = true;
            continue;
        }
        if (arg == "version") {
            showVersion = true;
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
    if (!showHelp && !showVersion && cmd.isEmpty()) {
        errorMessage = "Missing required argument: --cmd";
        return false;
    }

    return true;
}

bool ConsoleArgs::isFrameworkArg(const QString& key) const
{
    static const QStringList frameworkArgs = {"mode", "profile", "cmd"};
    return frameworkArgs.contains(key);
}

void ConsoleArgs::parseFrameworkArg(const QString& key, const QString& value)
{
    if (key == "mode") {
        mode = value;
    } else if (key == "profile") {
        profile = value;
    } else if (key == "cmd") {
        cmd = value;
    }
}

void ConsoleArgs::parseDataArg(const QString& key, const QString& value)
{
    QJsonValue jsonValue = inferType(value);
    setNestedValue(data, key, jsonValue);
}

} // namespace stdiolink
