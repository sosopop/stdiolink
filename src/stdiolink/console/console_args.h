#pragma once

#include <QString>
#include <QJsonObject>

namespace stdiolink {

/**
 * 类型推断：将字符串值转换为合适的 JSON 类型
 */
QJsonValue inferType(const QString& value);

/**
 * 设置嵌套路径的值
 * 例如：setNestedValue(obj, "a.b.c", 42) 会设置 obj["a"]["b"]["c"] = 42
 */
void setNestedValue(QJsonObject& root, const QString& path, const QJsonValue& value);

/**
 * Console 模式参数解析器
 */
class ConsoleArgs {
public:
    bool parse(int argc, char* argv[]);

    // 框架参数
    bool showHelp = false;
    bool showVersion = false;
    QString mode;      // "console" | "stdio"
    QString profile;   // "oneshot" | "keepalive"
    QString cmd;       // 命令名

    // data 参数
    QJsonObject data;

    // 错误信息
    QString errorMessage;

private:
    static bool isFrameworkArg(const QString& key);
    void parseFrameworkArg(const QString& key, const QString& value);
    void parseDataArg(const QString& key, const QString& value);
};

} // namespace stdiolink
