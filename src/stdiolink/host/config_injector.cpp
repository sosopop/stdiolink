#include "config_injector.h"
#include <QFile>
#include <QJsonDocument>

namespace stdiolink {

QHash<QString, QString> ConfigInjector::toEnvVars(const QJsonObject& config,
                                                   const meta::ConfigApply& apply) {
    QHash<QString, QString> envVars;
    QString prefix = apply.envPrefix;

    for (auto it = config.begin(); it != config.end(); ++it) {
        QString envName = keyToEnvName(it.key(), prefix);
        QString envValue = valueToString(it.value());
        envVars[envName] = envValue;
    }

    return envVars;
}

QStringList ConfigInjector::toArgs(const QJsonObject& config,
                                   const meta::ConfigApply& apply) {
    Q_UNUSED(apply)
    QStringList args;

    for (auto it = config.begin(); it != config.end(); ++it) {
        QString value = valueToString(it.value());
        args << QString("--%1=%2").arg(it.key(), value);
    }

    return args;
}

bool ConfigInjector::toFile(const QJsonObject& config, const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QJsonDocument doc(config);
    QByteArray data = doc.toJson(QJsonDocument::Indented);
    qint64 written = file.write(data);
    file.close();

    return written == data.size();
}

bool ConfigInjector::fromFile(const QString& path, QJsonObject& config) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        return false;
    }

    if (!doc.isObject()) {
        return false;
    }

    config = doc.object();
    return true;
}

QString ConfigInjector::valueToString(const QJsonValue& value) {
    switch (value.type()) {
    case QJsonValue::Bool:
        return value.toBool() ? "true" : "false";
    case QJsonValue::Double:
        // 检查是否为整数
        if (value.toDouble() == static_cast<int>(value.toDouble())) {
            return QString::number(static_cast<int>(value.toDouble()));
        }
        return QString::number(value.toDouble());
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Array:
    case QJsonValue::Object:
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    default:
        return QString();
    }
}

QString ConfigInjector::keyToEnvName(const QString& key, const QString& prefix) {
    // 转换为大写，点号替换为下划线
    QString envName = key.toUpper();
    envName.replace('.', '_');
    envName.replace('-', '_');
    return prefix + envName;
}

} // namespace stdiolink
