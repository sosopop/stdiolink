#pragma once

#include "json_cli_codec.h"
#include "stdiolink/stdiolink_export.h"

#include <QJsonObject>
#include <QString>

namespace stdiolink {

/**
 * 兼容性辅助函数：将单个 CLI 文本按 Friendly 规则推断为 JSON 值。
 * 新的结构化 CLI 解析应优先使用 JsonCliCodec。
 */
STDIOLINK_API QJsonValue inferType(const QString& value);

/**
 * 兼容性辅助函数：仅支持旧式 a.b 路径。
 * 新的结构化路径写入应优先使用 JsonCliCodec。
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
    QList<RawCliArg> rawDataArgs;

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
