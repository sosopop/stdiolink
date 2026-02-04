#pragma once

#include "stdiolink/protocol/meta_types.h"

namespace stdiolink::meta {

/**
 * 字段构建器
 */
class FieldBuilder {
public:
    explicit FieldBuilder(const QString& name, FieldType type);

    FieldBuilder& required(bool req = true);
    FieldBuilder& defaultValue(const QJsonValue& val);
    FieldBuilder& description(const QString& desc);

    // 数值约束
    FieldBuilder& range(double minVal, double maxVal);
    FieldBuilder& min(double val);
    FieldBuilder& max(double val);

    // 字符串约束
    FieldBuilder& minLength(int len);
    FieldBuilder& maxLength(int len);
    FieldBuilder& pattern(const QString& regex);

    // 枚举值
    FieldBuilder& enumValues(const QJsonArray& values);
    FieldBuilder& enumValues(const QStringList& values);

    // 格式提示
    FieldBuilder& format(const QString& fmt);

    // UI 提示
    FieldBuilder& widget(const QString& w);
    FieldBuilder& group(const QString& g);
    FieldBuilder& order(int o);
    FieldBuilder& placeholder(const QString& p);
    FieldBuilder& unit(const QString& u);
    FieldBuilder& advanced(bool adv = true);
    FieldBuilder& readonly(bool ro = true);

    // Object 类型嵌套字段
    FieldBuilder& addField(const FieldBuilder& field);
    FieldBuilder& requiredKeys(const QStringList& keys);
    FieldBuilder& additionalProperties(bool allowed);

    // Array 类型元素
    FieldBuilder& items(const FieldBuilder& item);
    FieldBuilder& minItems(int n);
    FieldBuilder& maxItems(int n);

    FieldMeta build() const;

private:
    FieldMeta m_field;
};

/**
 * 命令构建器
 */
class CommandBuilder {
public:
    explicit CommandBuilder(const QString& name);

    CommandBuilder& description(const QString& desc);
    CommandBuilder& title(const QString& t);
    CommandBuilder& summary(const QString& s);

    // 参数
    CommandBuilder& param(const FieldBuilder& field);

    // 返回值
    CommandBuilder& returns(FieldType type, const QString& desc = {});
    CommandBuilder& returnField(const FieldBuilder& field);

    // 事件
    CommandBuilder& event(const QString& name, const QString& desc = {});

    // UI
    CommandBuilder& group(const QString& g);
    CommandBuilder& order(int o);

    CommandMeta build() const;

private:
    CommandMeta m_cmd;
};

/**
 * 驱动元数据构建器
 */
class DriverMetaBuilder {
public:
    DriverMetaBuilder& schemaVersion(const QString& ver);

    DriverMetaBuilder& info(const QString& id,
                            const QString& name,
                            const QString& version,
                            const QString& desc = {});
    DriverMetaBuilder& vendor(const QString& v);
    DriverMetaBuilder& entry(const QString& program,
                             const QStringList& defaultArgs = {});

    DriverMetaBuilder& capability(const QString& cap);
    DriverMetaBuilder& profile(const QString& prof);

    DriverMetaBuilder& configField(const FieldBuilder& field);
    DriverMetaBuilder& configApply(const QString& method,
                                   const QString& command = {});

    DriverMetaBuilder& command(const CommandBuilder& cmd);

    DriverMeta build() const;

private:
    DriverMeta m_meta;
};

} // namespace stdiolink::meta
