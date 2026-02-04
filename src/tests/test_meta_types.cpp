#include <QJsonArray>
#include <QJsonObject>
#include <gtest/gtest.h>
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink::meta;

// ============================================
// FieldType 转换测试
// ============================================

TEST(MetaTypes, FieldTypeToString) {
    EXPECT_EQ(fieldTypeToString(FieldType::String), "string");
    EXPECT_EQ(fieldTypeToString(FieldType::Int), "int");
    EXPECT_EQ(fieldTypeToString(FieldType::Int64), "int64");
    EXPECT_EQ(fieldTypeToString(FieldType::Double), "double");
    EXPECT_EQ(fieldTypeToString(FieldType::Bool), "bool");
    EXPECT_EQ(fieldTypeToString(FieldType::Object), "object");
    EXPECT_EQ(fieldTypeToString(FieldType::Array), "array");
    EXPECT_EQ(fieldTypeToString(FieldType::Enum), "enum");
    EXPECT_EQ(fieldTypeToString(FieldType::Any), "any");
}

TEST(MetaTypes, FieldTypeFromString) {
    EXPECT_EQ(fieldTypeFromString("string"), FieldType::String);
    EXPECT_EQ(fieldTypeFromString("int"), FieldType::Int);
    EXPECT_EQ(fieldTypeFromString("integer"), FieldType::Int);
    EXPECT_EQ(fieldTypeFromString("int64"), FieldType::Int64);
    EXPECT_EQ(fieldTypeFromString("double"), FieldType::Double);
    EXPECT_EQ(fieldTypeFromString("number"), FieldType::Double);
    EXPECT_EQ(fieldTypeFromString("bool"), FieldType::Bool);
    EXPECT_EQ(fieldTypeFromString("boolean"), FieldType::Bool);
    EXPECT_EQ(fieldTypeFromString("object"), FieldType::Object);
    EXPECT_EQ(fieldTypeFromString("array"), FieldType::Array);
    EXPECT_EQ(fieldTypeFromString("enum"), FieldType::Enum);
    EXPECT_EQ(fieldTypeFromString("any"), FieldType::Any);
    EXPECT_EQ(fieldTypeFromString("unknown"), FieldType::Any);
}

// ============================================
// UIHint 测试
// ============================================

TEST(MetaTypes, UIHintSerialization) {
    UIHint hint;
    hint.widget = "slider";
    hint.group = "性能";
    hint.order = 10;
    hint.unit = "ms";
    hint.advanced = true;
    hint.readonly = true;
    hint.visibleIf = "mode == 'fast'";
    hint.step = 0.5;

    QJsonObject json = hint.toJson();
    EXPECT_EQ(json["widget"].toString(), "slider");
    EXPECT_EQ(json["group"].toString(), "性能");
    EXPECT_EQ(json["order"].toInt(), 10);
    EXPECT_EQ(json["unit"].toString(), "ms");
    EXPECT_EQ(json["advanced"].toBool(), true);
    EXPECT_EQ(json["readonly"].toBool(), true);
    EXPECT_EQ(json["visibleIf"].toString(), "mode == 'fast'");
    EXPECT_DOUBLE_EQ(json["step"].toDouble(), 0.5);

    UIHint restored = UIHint::fromJson(json);
    EXPECT_EQ(restored.widget, hint.widget);
    EXPECT_EQ(restored.group, hint.group);
    EXPECT_EQ(restored.order, hint.order);
    EXPECT_EQ(restored.unit, hint.unit);
    EXPECT_EQ(restored.advanced, hint.advanced);
    EXPECT_EQ(restored.readonly, hint.readonly);
    EXPECT_EQ(restored.visibleIf, hint.visibleIf);
    EXPECT_DOUBLE_EQ(restored.step, hint.step);
}

TEST(MetaTypes, UIHintIsEmpty) {
    UIHint empty;
    EXPECT_TRUE(empty.isEmpty());

    UIHint notEmpty;
    notEmpty.widget = "text";
    EXPECT_FALSE(notEmpty.isEmpty());
}

// ============================================
// Constraints 测试
// ============================================

TEST(MetaTypes, ConstraintsSerialization) {
    Constraints c;
    c.min = 0;
    c.max = 100;
    c.minLength = 1;
    c.maxLength = 32;
    c.pattern = "^[a-z]+$";
    c.enumValues = QJsonArray{"a", "b", "c"};
    c.format = "email";
    c.minItems = 1;
    c.maxItems = 5;

    QJsonObject json = c.toJson();
    EXPECT_EQ(json["min"].toDouble(), 0);
    EXPECT_EQ(json["max"].toDouble(), 100);
    EXPECT_EQ(json["minLength"].toInt(), 1);
    EXPECT_EQ(json["maxLength"].toInt(), 32);
    EXPECT_EQ(json["pattern"].toString(), "^[a-z]+$");
    EXPECT_EQ(json["enum"].toArray().size(), 3);
    EXPECT_EQ(json["format"].toString(), "email");
    EXPECT_EQ(json["minItems"].toInt(), 1);
    EXPECT_EQ(json["maxItems"].toInt(), 5);

    Constraints restored = Constraints::fromJson(json);
    EXPECT_EQ(restored.min, c.min);
    EXPECT_EQ(restored.max, c.max);
    EXPECT_EQ(restored.pattern, c.pattern);
    EXPECT_EQ(restored.format, c.format);
    EXPECT_EQ(restored.minItems, c.minItems);
    EXPECT_EQ(restored.maxItems, c.maxItems);
}

// ============================================
// FieldMeta 测试
// ============================================

TEST(MetaTypes, FieldMetaBasic) {
    FieldMeta field;
    field.name = "timeout";
    field.type = FieldType::Int;
    field.required = false;
    field.defaultValue = 5000;
    field.description = "超时时间（毫秒）";
    field.constraints.min = 100;
    field.constraints.max = 60000;

    QJsonObject json = field.toJson();
    EXPECT_EQ(json["name"].toString(), "timeout");
    EXPECT_EQ(json["type"].toString(), "int");
    EXPECT_EQ(json["default"].toInt(), 5000);
    EXPECT_EQ(json["min"].toDouble(), 100);
    EXPECT_EQ(json["max"].toDouble(), 60000);

    FieldMeta restored = FieldMeta::fromJson(json);
    EXPECT_EQ(restored.name, field.name);
    EXPECT_EQ(restored.type, field.type);
    EXPECT_EQ(restored.defaultValue.toInt(), 5000);
}

TEST(MetaTypes, FieldMetaNestedObject) {
    FieldMeta roi;
    roi.name = "roi";
    roi.type = FieldType::Object;

    FieldMeta x, y;
    x.name = "x";
    x.type = FieldType::Int;
    x.required = true;
    y.name = "y";
    y.type = FieldType::Int;
    y.required = true;
    roi.fields = {x, y};

    QJsonObject json = roi.toJson();
    EXPECT_TRUE(json.contains("fields"));
    EXPECT_EQ(json["fields"].toArray().size(), 2);

    FieldMeta restored = FieldMeta::fromJson(json);
    EXPECT_EQ(restored.fields.size(), 2);
    EXPECT_EQ(restored.fields[0].name, "x");
}

TEST(MetaTypes, FieldMetaArrayItems) {
    FieldMeta tags;
    tags.name = "tags";
    tags.type = FieldType::Array;
    tags.items = std::make_shared<FieldMeta>();
    tags.items->type = FieldType::String;
    tags.constraints.minItems = 1;
    tags.constraints.maxItems = 10;

    QJsonObject json = tags.toJson();
    EXPECT_TRUE(json.contains("items"));
    EXPECT_EQ(json["minItems"].toInt(), 1);

    FieldMeta restored = FieldMeta::fromJson(json);
    EXPECT_NE(restored.items, nullptr);
    EXPECT_EQ(restored.items->type, FieldType::String);
}

TEST(MetaTypes, FieldMetaObjectProps) {
    FieldMeta obj;
    obj.name = "settings";
    obj.type = FieldType::Object;
    obj.requiredKeys = QStringList({"mode", "level"});
    obj.additionalProperties = false;
    obj.ui.readonly = true;

    QJsonObject json = obj.toJson();
    EXPECT_EQ(json["requiredKeys"].toArray().size(), 2);
    EXPECT_FALSE(json["additionalProperties"].toBool());
    EXPECT_TRUE(json["ui"].toObject()["readonly"].toBool());

    FieldMeta restored = FieldMeta::fromJson(json);
    EXPECT_EQ(restored.requiredKeys, QStringList({"mode", "level"}));
    EXPECT_FALSE(restored.additionalProperties);
    EXPECT_TRUE(restored.ui.readonly);
}

// ============================================
// EventMeta/ReturnMeta 测试
// ============================================

TEST(MetaTypes, EventMetaSerialization) {
    EventMeta e;
    e.name = "progress";
    e.description = "进度更新";
    FieldMeta pct;
    pct.name = "percent";
    pct.type = FieldType::Double;
    e.fields.append(pct);

    QJsonObject json = e.toJson();
    EXPECT_EQ(json["name"].toString(), "progress");
    EXPECT_EQ(json["fields"].toArray().size(), 1);

    EventMeta restored = EventMeta::fromJson(json);
    EXPECT_EQ(restored.name, "progress");
    EXPECT_EQ(restored.fields.size(), 1);
}

TEST(MetaTypes, ReturnMetaSerialization) {
    ReturnMeta r;
    r.type = FieldType::Object;
    r.description = "Result";
    FieldMeta f;
    f.name = "count";
    f.type = FieldType::Int;
    r.fields.append(f);

    QJsonObject json = r.toJson();
    EXPECT_EQ(json["type"].toString(), "object");
    EXPECT_EQ(json["fields"].toArray().size(), 1);

    ReturnMeta restored = ReturnMeta::fromJson(json);
    EXPECT_EQ(restored.type, FieldType::Object);
    EXPECT_EQ(restored.fields.size(), 1);
}

// ============================================
// ConfigApply/ConfigSchema 测试
// ============================================

TEST(MetaTypes, ConfigApplySerialization) {
    ConfigApply apply;
    apply.method = "env";
    apply.envPrefix = "SCAN_";
    apply.command = "meta.config.set";
    apply.fileName = "config.json";

    QJsonObject json = apply.toJson();
    EXPECT_EQ(json["method"].toString(), "env");
    EXPECT_EQ(json["envPrefix"].toString(), "SCAN_");
    EXPECT_EQ(json["command"].toString(), "meta.config.set");
    EXPECT_EQ(json["fileName"].toString(), "config.json");

    ConfigApply restored = ConfigApply::fromJson(json);
    EXPECT_EQ(restored.method, apply.method);
    EXPECT_EQ(restored.envPrefix, apply.envPrefix);
    EXPECT_EQ(restored.command, apply.command);
    EXPECT_EQ(restored.fileName, apply.fileName);
}

TEST(MetaTypes, ConfigSchemaSerialization) {
    ConfigSchema schema;
    FieldMeta f;
    f.name = "timeout";
    f.type = FieldType::Int;
    schema.fields.append(f);
    schema.apply.method = "env";

    QJsonObject json = schema.toJson();
    EXPECT_EQ(json["fields"].toArray().size(), 1);
    EXPECT_EQ(json["apply"].toObject()["method"].toString(), "env");

    ConfigSchema restored = ConfigSchema::fromJson(json);
    EXPECT_EQ(restored.fields.size(), 1);
    EXPECT_EQ(restored.apply.method, "env");
}

// ============================================
// CommandMeta 测试
// ============================================

TEST(MetaTypes, CommandMetaSerialization) {
    CommandMeta cmd;
    cmd.name = "scan";
    cmd.description = "执行扫描";
    cmd.title = "扫描";
    cmd.summary = "开始扫描";

    FieldMeta param;
    param.name = "mode";
    param.type = FieldType::Enum;
    param.required = true;
    param.constraints.enumValues = QJsonArray{"frame", "continuous"};
    cmd.params.append(param);
    cmd.errors.append(QJsonObject{{"code", 1001}});
    cmd.examples.append(QJsonObject{{"title", "example"}});
    cmd.ui.group = "Scan";

    QJsonObject json = cmd.toJson();
    EXPECT_EQ(json["name"].toString(), "scan");
    EXPECT_EQ(json["params"].toArray().size(), 1);
    EXPECT_EQ(json["title"].toString(), "扫描");
    EXPECT_EQ(json["summary"].toString(), "开始扫描");
    EXPECT_EQ(json["errors"].toArray().size(), 1);
    EXPECT_EQ(json["examples"].toArray().size(), 1);

    CommandMeta restored = CommandMeta::fromJson(json);
    EXPECT_EQ(restored.name, "scan");
    EXPECT_EQ(restored.params.size(), 1);
    EXPECT_EQ(restored.title, "扫描");
    EXPECT_EQ(restored.summary, "开始扫描");
    EXPECT_EQ(restored.errors.size(), 1);
    EXPECT_EQ(restored.examples.size(), 1);
    EXPECT_EQ(restored.ui.group, "Scan");
}

// ============================================
// DriverMeta 测试
// ============================================

TEST(MetaTypes, DriverMetaSerialization) {
    DriverMeta meta;
    meta.schemaVersion = "1.0";
    meta.info.id = "com.example.test";
    meta.info.name = "Test Driver";
    meta.info.version = "1.0.0";
    meta.info.entry = QJsonObject{{"program", "test.exe"}};
    meta.info.capabilities = QStringList({"streaming", "config"});
    meta.info.profiles = QStringList({"oneshot"});

    CommandMeta cmd;
    cmd.name = "echo";
    cmd.description = "回显";
    meta.commands.append(cmd);
    FieldMeta point;
    point.name = "x";
    point.type = FieldType::Int;
    meta.types["Point"] = point;
    meta.errors.append(QJsonObject{{"code", 1007}, {"name", "Invalid"}});
    meta.examples.append(QJsonObject{{"title", "demo"}});

    QJsonObject json = meta.toJson();
    EXPECT_EQ(json["schemaVersion"].toString(), "1.0");
    EXPECT_TRUE(json.contains("info"));
    EXPECT_EQ(json["commands"].toArray().size(), 1);
    EXPECT_EQ(json["types"].toObject().size(), 1);
    EXPECT_EQ(json["errors"].toArray().size(), 1);
    EXPECT_EQ(json["examples"].toArray().size(), 1);

    DriverMeta restored = DriverMeta::fromJson(json);
    EXPECT_EQ(restored.info.id, "com.example.test");
    EXPECT_EQ(restored.commands.size(), 1);
    EXPECT_EQ(restored.info.entry["program"].toString(), "test.exe");
    EXPECT_EQ(restored.info.capabilities, QStringList({"streaming", "config"}));
    EXPECT_EQ(restored.info.profiles, QStringList({"oneshot"}));
    EXPECT_TRUE(restored.types.contains("Point"));
    EXPECT_EQ(restored.errors.size(), 1);
    EXPECT_EQ(restored.examples.size(), 1);
}

TEST(MetaTypes, DriverMetaFindCommand) {
    DriverMeta meta;
    CommandMeta cmd1, cmd2;
    cmd1.name = "scan";
    cmd2.name = "stop";
    meta.commands = {cmd1, cmd2};

    EXPECT_NE(meta.findCommand("scan"), nullptr);
    EXPECT_EQ(meta.findCommand("scan")->name, "scan");
    EXPECT_NE(meta.findCommand("stop"), nullptr);
    EXPECT_EQ(meta.findCommand("unknown"), nullptr);
}

TEST(MetaTypes, DriverMetaCompatibility) {
    // 测试兼容 driver 字段名
    QJsonObject json;
    json["schemaVersion"] = "1.0";
    json["driver"] = QJsonObject{
        {"id", "test.id"},
        {"name", "Test"},
        {"version", "1.0.0"}
    };
    json["commands"] = QJsonArray{};

    DriverMeta meta = DriverMeta::fromJson(json);
    EXPECT_EQ(meta.info.id, "test.id");
}
