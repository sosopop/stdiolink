#pragma once

#include "stdiolink/stdiolink_export.h"

#include <QJsonObject>
#include <QString>

namespace stdiolink {

/**
 * 类型推断：将字符串值转换为合适的 JSON 类型
 */
STDIOLINK_API QJsonValue inferType(const QString& value);

/**
 * 设置嵌套路径的值
 */
STDIOLINK_API void setNestedValue(QJsonObject& root, const QString& path, const QJsonValue& value);

/**
 * Console 模式参数解析器
 */
class STDIOLINK_API ConsoleArgs {
public:
    bool parse(int argc, char* argv[]);

    /**
     * 检测 stdin 是否为交互终端（非管道/重定向）
     */
    static bool isInteractiveStdin();

    // 框架参数
    bool showHelp = false;
    bool showVersion = false;
    QString mode;    // "console" | "stdio"
    QString profile; // "oneshot" | "keepalive"
    QString cmd;     // 命令名

    // 导出参数 (M13)
    bool exportMeta = false;
    QString exportMetaPath;    // 可选，导出文件路径
    QString exportDocFormat;   // markdown|openapi|html
    QString exportDocPath;     // 可选，导出文件路径

    // data 参数
    QJsonObject data;

    // 错误信息
    QString errorMessage;

private:
    static bool isFrameworkArg(const QString& key);
    void parseFrameworkArg(const QString& key, const QString& value);
    void parseDataArg(const QString& key, const QString& value);
    bool parseShortArg(const QString& arg, int& index, int argc, char* argv[]);
    void parseExportDoc(const QString& value);
};

} // namespace stdiolink
