#pragma once

#include <QJsonObject>
#include <QString>

namespace stdiolink_service {

struct ServiceManifest {
    QString manifestVersion;  // 固定 "1"
    QString id;               // 服务唯一标识
    QString name;             // 服务显示名称
    QString version;          // 语义化版本号
    QString description;      // 可选描述
    QString author;           // 可选作者

    static ServiceManifest fromJson(const QJsonObject& obj, QString& error);
    static ServiceManifest loadFromFile(const QString& filePath, QString& error);
    bool isValid(QString& error) const;
};

} // namespace stdiolink_service
