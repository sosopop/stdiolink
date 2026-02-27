#pragma once

#include <QDir>
#include <QJsonObject>
#include <QStringList>

namespace stdiolink_service {

/// 将 dataRoot 规范化为绝对路径；空输入返回空 QString
inline QString normalizeDataRoot(const QString &raw) {
    return raw.isEmpty() ? QString() : QDir(raw).absolutePath();
}

class ServiceArgs {
public:
    struct ParseResult {
        QString serviceDir;           // 服务目录路径
        QJsonObject rawConfigValues;  // --config.* 解析结果（叶子值均为 raw string）
        QString configFilePath;       // --config-file 路径
        QString guardName;            // --guard=<name>，为空表示未指定
        QString dataRoot;             // --data-root=<path>，空表示未指定
        bool dumpSchema = false;      // --dump-config-schema
        bool help = false;
        bool version = false;
        QString error;                // 解析错误信息
    };

    static ParseResult parse(const QStringList& appArgs);
    static QJsonObject loadConfigFile(const QString& filePath, QString& error);

private:
    static bool setNestedRawValue(QJsonObject& root,
                                  const QStringList& path,
                                  const QString& rawValue,
                                  QString& error);
};

} // namespace stdiolink_service
