#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <optional>

namespace stdiolink::meta {

/**
 * 字段类型枚举
 */
enum class FieldType {
    String,  // 字符串
    Int,     // 32位整数
    Int64,   // 64位整数
    Double,  // 浮点数
    Bool,    // 布尔值
    Object,  // 嵌套对象
    Array,   // 数组
    Enum,    // 枚举（字符串限定值）
    Any      // 任意类型
};

QString fieldTypeToString(FieldType type);
FieldType fieldTypeFromString(const QString& str);

/**
 * UI 渲染提示
 */
struct UIHint {
    QString widget;
    QString group;
    int order = 0;
    QString placeholder;
    bool advanced = false;
    bool readonly = false;
    QString visibleIf;
    QString unit;
    double step = 0;

    QJsonObject toJson() const;
    static UIHint fromJson(const QJsonObject& obj);
    bool isEmpty() const;
};

/**
 * 字段约束条件
 */
struct Constraints {
    std::optional<double> min;
    std::optional<double> max;
    std::optional<int> minLength;
    std::optional<int> maxLength;
    QString pattern;
    QJsonArray enumValues;
    QString format;
    std::optional<int> minItems;
    std::optional<int> maxItems;

    QJsonObject toJson() const;
    static Constraints fromJson(const QJsonObject& obj);
    bool isEmpty() const;
};

/**
 * 字段元数据
 */
struct FieldMeta {
    QString name;
    FieldType type = FieldType::Any;
    bool required = false;
    QJsonValue defaultValue;
    QString description;
    Constraints constraints;
    UIHint ui;
    QVector<FieldMeta> fields;            // Object 嵌套字段
    std::shared_ptr<FieldMeta> items;     // Array 元素 schema
    QStringList requiredKeys;             // Object 必填键
    bool additionalProperties = true;

    QJsonObject toJson() const;
    static FieldMeta fromJson(const QJsonObject& obj);
};

/**
 * 事件元数据
 */
struct EventMeta {
    QString name;
    QString description;
    QVector<FieldMeta> fields;

    QJsonObject toJson() const;
    static EventMeta fromJson(const QJsonObject& obj);
};

/**
 * 返回值元数据
 */
struct ReturnMeta {
    FieldType type = FieldType::Object;
    QString description;
    QVector<FieldMeta> fields;

    QJsonObject toJson() const;
    static ReturnMeta fromJson(const QJsonObject& obj);
};

/**
 * 命令元数据
 */
struct CommandMeta {
    QString name;
    QString description;
    QString title;
    QString summary;
    QVector<FieldMeta> params;
    ReturnMeta returns;
    QVector<EventMeta> events;
    QVector<QJsonObject> errors;
    QVector<QJsonObject> examples;
    UIHint ui;

    QJsonObject toJson() const;
    static CommandMeta fromJson(const QJsonObject& obj);
};

/**
 * 配置注入方式
 */
struct ConfigApply {
    QString method;  // startupArgs|env|command|file
    QString envPrefix;
    QString command;
    QString fileName;

    QJsonObject toJson() const;
    static ConfigApply fromJson(const QJsonObject& obj);
};

/**
 * 配置模式
 */
struct ConfigSchema {
    QVector<FieldMeta> fields;
    ConfigApply apply;

    QJsonObject toJson() const;
    static ConfigSchema fromJson(const QJsonObject& obj);
};

/**
 * 驱动基本信息
 */
struct DriverInfo {
    QString id;
    QString name;
    QString version;
    QString description;
    QString vendor;
    QJsonObject entry;
    QStringList capabilities;
    QStringList profiles;

    QJsonObject toJson() const;
    static DriverInfo fromJson(const QJsonObject& obj);
};

/**
 * 驱动元数据（顶层结构）
 */
struct DriverMeta {
    QString schemaVersion = "1.0";
    DriverInfo info;
    ConfigSchema config;
    QVector<CommandMeta> commands;
    QHash<QString, FieldMeta> types;
    QVector<QJsonObject> errors;
    QVector<QJsonObject> examples;

    QJsonObject toJson() const;
    static DriverMeta fromJson(const QJsonObject& obj);

    const CommandMeta* findCommand(const QString& name) const;
};

} // namespace stdiolink::meta
