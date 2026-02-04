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

    QJsonObject json = hint.toJson();
    EXPECT_EQ(json["widget"].toString(), "slider");
    EXPECT_EQ(json["group"].toString(), "性能");
    EXPECT_EQ(json["order"].toInt(), 10);
    EXPECT_EQ(json["unit"].toString(), "ms");

    UIHint restored = UIHint::fromJson(json);
    EXPECT_EQ(restored.widget, hint.widget);
    EXPECT_EQ(restored.group, hint.group);
    EXPECT_EQ(restored.order, hint.order);
    EXPECT_EQ(restored.unit, hint.unit);
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

    QJsonObject json = c.toJson();
    EXPECT_EQ(json["min"].toDouble(), 0);
    EXPECT_EQ(json["max"].toDouble(), 100);
    EXPECT_EQ(json["minLength"].toInt(), 1);
    EXPECT_EQ(json["maxLength"].toInt(), 32);
    EXPECT_EQ(json["pattern"].toString(), "^[a-z]+$");
    EXPECT_EQ(json["enum"].toArray().size(), 3);

    Constraints restored = Constraints::fromJson(json);
    EXPECT_EQ(restored.min, c.min);
    EXPECT_EQ(restored.max, c.max);
    EXPECT_EQ(restored.pattern, c.pattern);
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

// ============================================
// CommandMeta 测试
// ============================================

TEST(MetaTypes, CommandMetaSerialization) {
    CommandMeta cmd;
    cmd.name = "scan";
    cmd.description = "执行扫描";

    FieldMeta param;
    param.name = "mode";
    param.type = FieldType::Enum;
    param.required = true;
    param.constraints.enumValues = QJsonArray{"frame", "continuous"};
    cmd.params.append(param);

    QJsonObject json = cmd.toJson();
    EXPECT_EQ(json["name"].toString(), "scan");
    EXPECT_EQ(json["params"].toArray().size(), 1);

    CommandMeta restored = CommandMeta::fromJson(json);
    EXPECT_EQ(restored.name, "scan");
    EXPECT_EQ(restored.params.size(), 1);
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

    CommandMeta cmd;
    cmd.name = "echo";
    cmd.description = "回显";
    meta.commands.append(cmd);

    QJsonObject json = meta.toJson();
    EXPECT_EQ(json["schemaVersion"].toString(), "1.0");
    EXPECT_TRUE(json.contains("info"));
    EXPECT_EQ(json["commands"].toArray().size(), 1);

    DriverMeta restored = DriverMeta::fromJson(json);
    EXPECT_EQ(restored.info.id, "com.example.test");
    EXPECT_EQ(restored.commands.size(), 1);
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
