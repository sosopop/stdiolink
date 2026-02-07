#pragma once

#include <QJsonObject>
#include <QStringList>

namespace stdiolink_service {

class ServiceArgs {
public:
    struct ParseResult {
        QString serviceDir;           // 服务目录路径
        QJsonObject rawConfigValues;  // --config.* 解析结果（叶子值均为 raw string）
        QString configFilePath;       // --config-file 路径
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
