#include "meta_builder.h"

namespace stdiolink::meta {

// FieldBuilder 实现
FieldBuilder::FieldBuilder(const QString& name, FieldType type) {
    m_field.name = name;
    m_field.type = type;
}

FieldBuilder& FieldBuilder::required(bool req) {
    m_field.required = req;
    return *this;
}

FieldBuilder& FieldBuilder::defaultValue(const QJsonValue& val) {
    m_field.defaultValue = val;
    return *this;
}

FieldBuilder& FieldBuilder::description(const QString& desc) {
    m_field.description = desc;
    return *this;
}

FieldBuilder& FieldBuilder::range(double minVal, double maxVal) {
    m_field.constraints.min = minVal;
    m_field.constraints.max = maxVal;
    return *this;
}

FieldBuilder& FieldBuilder::min(double val) {
    m_field.constraints.min = val;
    return *this;
}

FieldBuilder& FieldBuilder::max(double val) {
    m_field.constraints.max = val;
    return *this;
}

FieldBuilder& FieldBuilder::minLength(int len) {
    m_field.constraints.minLength = len;
    return *this;
}

FieldBuilder& FieldBuilder::maxLength(int len) {
    m_field.constraints.maxLength = len;
    return *this;
}

FieldBuilder& FieldBuilder::pattern(const QString& regex) {
    m_field.constraints.pattern = regex;
    return *this;
}

FieldBuilder& FieldBuilder::enumValues(const QJsonArray& values) {
    m_field.constraints.enumValues = values;
    return *this;
}

FieldBuilder& FieldBuilder::enumValues(const QStringList& values) {
    QJsonArray arr;
    for (const auto& v : values) {
        arr.append(v);
    }
    m_field.constraints.enumValues = arr;
    return *this;
}

FieldBuilder& FieldBuilder::format(const QString& fmt) {
    m_field.constraints.format = fmt;
    return *this;
}

FieldBuilder& FieldBuilder::widget(const QString& w) {
    m_field.ui.widget = w;
    return *this;
}

FieldBuilder& FieldBuilder::group(const QString& g) {
    m_field.ui.group = g;
    return *this;
}

FieldBuilder& FieldBuilder::order(int o) {
    m_field.ui.order = o;
    return *this;
}

FieldBuilder& FieldBuilder::placeholder(const QString& p) {
    m_field.ui.placeholder = p;
    return *this;
}

FieldBuilder& FieldBuilder::unit(const QString& u) {
    m_field.ui.unit = u;
    return *this;
}

FieldBuilder& FieldBuilder::advanced(bool adv) {
    m_field.ui.advanced = adv;
    return *this;
}

FieldBuilder& FieldBuilder::readonly(bool ro) {
    m_field.ui.readonly = ro;
    return *this;
}

FieldBuilder& FieldBuilder::addField(const FieldBuilder& field) {
    m_field.fields.append(field.build());
    return *this;
}

FieldBuilder& FieldBuilder::requiredKeys(const QStringList& keys) {
    m_field.requiredKeys = keys;
    return *this;
}

FieldBuilder& FieldBuilder::additionalProperties(bool allowed) {
    m_field.additionalProperties = allowed;
    return *this;
}

FieldBuilder& FieldBuilder::items(const FieldBuilder& item) {
    m_field.items = std::make_shared<FieldMeta>(item.build());
    return *this;
}

FieldBuilder& FieldBuilder::minItems(int n) {
    m_field.constraints.minItems = n;
    return *this;
}

FieldBuilder& FieldBuilder::maxItems(int n) {
    m_field.constraints.maxItems = n;
    return *this;
}

FieldMeta FieldBuilder::build() const {
    return m_field;
}

// CommandBuilder 实现
CommandBuilder::CommandBuilder(const QString& name) {
    m_cmd.name = name;
}

CommandBuilder& CommandBuilder::description(const QString& desc) {
    m_cmd.description = desc;
    return *this;
}

CommandBuilder& CommandBuilder::title(const QString& t) {
    m_cmd.title = t;
    return *this;
}

CommandBuilder& CommandBuilder::summary(const QString& s) {
    m_cmd.summary = s;
    return *this;
}

CommandBuilder& CommandBuilder::param(const FieldBuilder& field) {
    m_cmd.params.append(field.build());
    return *this;
}

CommandBuilder& CommandBuilder::returns(FieldType type, const QString& desc) {
    m_cmd.returns.type = type;
    m_cmd.returns.description = desc;
    return *this;
}

CommandBuilder& CommandBuilder::returnField(const FieldBuilder& field) {
    m_cmd.returns.type = field.build().type;
    m_cmd.returns.description = field.build().description;
    m_cmd.returns.fields = field.build().fields;
    return *this;
}

CommandBuilder& CommandBuilder::event(const QString& name, const QString& desc) {
    EventMeta ev;
    ev.name = name;
    ev.description = desc;
    m_cmd.events.append(ev);
    return *this;
}

CommandBuilder& CommandBuilder::group(const QString& g) {
    m_cmd.ui.group = g;
    return *this;
}

CommandBuilder& CommandBuilder::order(int o) {
    m_cmd.ui.order = o;
    return *this;
}

CommandMeta CommandBuilder::build() const {
    return m_cmd;
}

// DriverMetaBuilder 实现
DriverMetaBuilder& DriverMetaBuilder::schemaVersion(const QString& ver) {
    m_meta.schemaVersion = ver;
    return *this;
}

DriverMetaBuilder& DriverMetaBuilder::info(const QString& id,
                                           const QString& name,
                                           const QString& version,
                                           const QString& desc) {
    m_meta.info.id = id;
    m_meta.info.name = name;
    m_meta.info.version = version;
    m_meta.info.description = desc;
    return *this;
}

DriverMetaBuilder& DriverMetaBuilder::vendor(const QString& v) {
    m_meta.info.vendor = v;
    return *this;
}

DriverMetaBuilder& DriverMetaBuilder::entry(const QString& program,
                                            const QStringList& defaultArgs) {
    m_meta.info.entry["program"] = program;
    QJsonArray args;
    for (const auto& arg : defaultArgs) {
        args.append(arg);
    }
    m_meta.info.entry["defaultArgs"] = args;
    return *this;
}

DriverMetaBuilder& DriverMetaBuilder::capability(const QString& cap) {
    m_meta.info.capabilities.append(cap);
    return *this;
}

DriverMetaBuilder& DriverMetaBuilder::profile(const QString& prof) {
    m_meta.info.profiles.append(prof);
    return *this;
}

DriverMetaBuilder& DriverMetaBuilder::configField(const FieldBuilder& field) {
    m_meta.config.fields.append(field.build());
    return *this;
}

DriverMetaBuilder& DriverMetaBuilder::configApply(const QString& method,
                                                  const QString& command) {
    m_meta.config.apply.method = method;
    m_meta.config.apply.command = command;
    return *this;
}

DriverMetaBuilder& DriverMetaBuilder::command(const CommandBuilder& cmd) {
    m_meta.commands.append(cmd.build());
    return *this;
}

DriverMeta DriverMetaBuilder::build() const {
    return m_meta;
}

} // namespace stdiolink::meta
