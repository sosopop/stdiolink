#include <gtest/gtest.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include "config/service_config_schema.h"

using namespace stdiolink_service;
using namespace stdiolink::meta;

TEST(ServiceConfigSchema, ParseBasicTypes) {
    QJsonObject input{
        {"name", QJsonObject{{"type", "string"}, {"required", true}, {"description", "Name"}}},
        {"port", QJsonObject{{"type", "int"}, {"required", true}, {"description", "Port"}}},
        {"ratio", QJsonObject{{"type", "double"}, {"default", 0.5}}},
        {"debug", QJsonObject{{"type", "bool"}, {"default", false}}}
    };

    auto schema = ServiceConfigSchema::fromJsObject(input);
    ASSERT_EQ(schema.fields.size(), 4);

    const auto* name = schema.findField("name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->type, FieldType::String);
    EXPECT_TRUE(name->required);
    EXPECT_EQ(name->description, "Name");

    const auto* port = schema.findField("port");
    ASSERT_NE(port, nullptr);
    EXPECT_EQ(port->type, FieldType::Int);
    EXPECT_TRUE(port->required);

    const auto* ratio = schema.findField("ratio");
    ASSERT_NE(ratio, nullptr);
    EXPECT_EQ(ratio->type, FieldType::Double);
    EXPECT_DOUBLE_EQ(ratio->defaultValue.toDouble(), 0.5);

    const auto* debug = schema.findField("debug");
    ASSERT_NE(debug, nullptr);
    EXPECT_EQ(debug->type, FieldType::Bool);
    EXPECT_EQ(debug->defaultValue.toBool(), false);
}

TEST(ServiceConfigSchema, ParseEnumType) {
    QJsonObject input{
        {"mode", QJsonObject{
            {"type", "enum"},
            {"default", "normal"},
            {"constraints", QJsonObject{
                {"enumValues", QJsonArray{"fast", "normal", "slow"}}
            }}
        }}
    };

    auto schema = ServiceConfigSchema::fromJsObject(input);
    const auto* mode = schema.findField("mode");
    ASSERT_NE(mode, nullptr);
    EXPECT_EQ(mode->type, FieldType::Enum);
    EXPECT_EQ(mode->defaultValue.toString(), "normal");
    EXPECT_EQ(mode->constraints.enumValues.size(), 3);
}

TEST(ServiceConfigSchema, ParseArrayWithItems) {
    QJsonObject input{
        {"tags", QJsonObject{
            {"type", "array"},
            {"default", QJsonArray{}},
            {"items", QJsonObject{{"type", "string"}}},
            {"constraints", QJsonObject{{"maxItems", 20}}}
        }}
    };

    auto schema = ServiceConfigSchema::fromJsObject(input);
    const auto* tags = schema.findField("tags");
    ASSERT_NE(tags, nullptr);
    EXPECT_EQ(tags->type, FieldType::Array);
    ASSERT_NE(tags->items, nullptr);
    EXPECT_EQ(tags->items->type, FieldType::String);
    ASSERT_TRUE(tags->constraints.maxItems.has_value());
    EXPECT_EQ(*tags->constraints.maxItems, 20);
}

TEST(ServiceConfigSchema, ParseObjectType) {
    QJsonObject input{
        {"server", QJsonObject{{"type", "object"}}}
    };

    auto schema = ServiceConfigSchema::fromJsObject(input);
    const auto* server = schema.findField("server");
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->type, FieldType::Object);
}

TEST(ServiceConfigSchema, ParseConstraints) {
    QJsonObject input{
        {"port", QJsonObject{
            {"type", "int"},
            {"constraints", QJsonObject{{"min", 1}, {"max", 65535}}}
        }},
        {"name", QJsonObject{
            {"type", "string"},
            {"constraints", QJsonObject{
                {"minLength", 1}, {"maxLength", 64}, {"pattern", "^[a-z]+$"}
            }}
        }}
    };

    auto schema = ServiceConfigSchema::fromJsObject(input);

    const auto* port = schema.findField("port");
    ASSERT_NE(port, nullptr);
    ASSERT_TRUE(port->constraints.min.has_value());
    EXPECT_DOUBLE_EQ(*port->constraints.min, 1.0);
    ASSERT_TRUE(port->constraints.max.has_value());
    EXPECT_DOUBLE_EQ(*port->constraints.max, 65535.0);

    const auto* name = schema.findField("name");
    ASSERT_NE(name, nullptr);
    ASSERT_TRUE(name->constraints.minLength.has_value());
    EXPECT_EQ(*name->constraints.minLength, 1);
    ASSERT_TRUE(name->constraints.maxLength.has_value());
    EXPECT_EQ(*name->constraints.maxLength, 64);
    EXPECT_EQ(name->constraints.pattern, "^[a-z]+$");
}

TEST(ServiceConfigSchema, ParseDefaultValues) {
    QJsonObject input{
        {"count", QJsonObject{{"type", "int"}, {"default", 10}}},
        {"label", QJsonObject{{"type", "string"}, {"default", "hello"}}}
    };

    auto schema = ServiceConfigSchema::fromJsObject(input);
    EXPECT_EQ(schema.findField("count")->defaultValue.toInt(), 10);
    EXPECT_EQ(schema.findField("label")->defaultValue.toString(), "hello");
}

TEST(ServiceConfigSchema, ParseRequiredField) {
    QJsonObject input{
        {"key", QJsonObject{{"type", "string"}, {"required", true}}}
    };

    auto schema = ServiceConfigSchema::fromJsObject(input);
    EXPECT_TRUE(schema.findField("key")->required);
}

TEST(ServiceConfigSchema, FindFieldByName) {
    QJsonObject input{
        {"a", QJsonObject{{"type", "string"}}},
        {"b", QJsonObject{{"type", "int"}}}
    };

    auto schema = ServiceConfigSchema::fromJsObject(input);
    EXPECT_NE(schema.findField("a"), nullptr);
    EXPECT_NE(schema.findField("b"), nullptr);
    EXPECT_EQ(schema.findField("c"), nullptr);
}

TEST(ServiceConfigSchema, ToJsonRoundTrip) {
    QJsonObject input{
        {"port", QJsonObject{
            {"type", "int"}, {"required", true},
            {"description", "Port number"},
            {"constraints", QJsonObject{{"min", 1}, {"max", 65535}}}
        }}
    };

    auto schema = ServiceConfigSchema::fromJsObject(input);
    QJsonObject json = schema.toJson();
    EXPECT_TRUE(json.contains("fields"));
    EXPECT_TRUE(json["fields"].isArray());
    EXPECT_EQ(json["fields"].toArray().size(), 1);
}

TEST(ServiceConfigSchema, EmptySchema) {
    auto schema = ServiceConfigSchema::fromJsObject(QJsonObject{});
    EXPECT_TRUE(schema.fields.isEmpty());
    QJsonObject json = schema.toJson();
    EXPECT_EQ(json["fields"].toArray().size(), 0);
}

TEST(ServiceConfigSchema, FromJsonFileValid) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString path = tmpDir.filePath("config.schema.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(R"({
        "port": { "type": "int", "required": true, "description": "listen port" },
        "debug": { "type": "bool", "default": false }
    })");
    f.close();

    QString err;
    auto schema = ServiceConfigSchema::fromJsonFile(path, err);
    EXPECT_TRUE(err.isEmpty()) << err.toStdString();
    EXPECT_EQ(schema.fields.size(), 2);
    EXPECT_NE(schema.findField("port"), nullptr);
    EXPECT_NE(schema.findField("debug"), nullptr);
}

TEST(ServiceConfigSchema, FromJsonFileNotFound) {
    QString err;
    auto schema = ServiceConfigSchema::fromJsonFile("nonexistent.json", err);
    EXPECT_FALSE(err.isEmpty());
    EXPECT_TRUE(schema.fields.isEmpty());
}

TEST(ServiceConfigSchema, FromJsonFileMalformedJson) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString path = tmpDir.filePath("bad.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("{invalid json");
    f.close();

    QString err;
    auto schema = ServiceConfigSchema::fromJsonFile(path, err);
    EXPECT_FALSE(err.isEmpty());
}

TEST(ServiceConfigSchema, FromJsonFileNotObject) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString path = tmpDir.filePath("array.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("[]");
    f.close();

    QString err;
    auto schema = ServiceConfigSchema::fromJsonFile(path, err);
    EXPECT_FALSE(err.isEmpty());
}

TEST(ServiceConfigSchema, FromJsonFileUnknownFieldType) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString path = tmpDir.filePath("bad_type.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(R"({"port": {"type": "integr"}})");
    f.close();

    QString err;
    auto schema = ServiceConfigSchema::fromJsonFile(path, err);
    EXPECT_FALSE(err.isEmpty());
    EXPECT_TRUE(err.contains("unknown field type"));
    EXPECT_TRUE(err.contains("port"));
}

TEST(ServiceConfigSchema, FromJsonFileUnknownNestedFieldType) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString path = tmpDir.filePath("bad_nested.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(R"({"server": {"type": "object", "fields": {"host": {"type": "strng"}}}})");
    f.close();

    QString err;
    auto schema = ServiceConfigSchema::fromJsonFile(path, err);
    EXPECT_FALSE(err.isEmpty());
    EXPECT_TRUE(err.contains("server.host"));
}

TEST(ServiceConfigSchema, FromJsonFileFieldDescriptorNotObject) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString path = tmpDir.filePath("bad_desc.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(R"({"port": 123})");
    f.close();

    QString err;
    auto schema = ServiceConfigSchema::fromJsonFile(path, err);
    EXPECT_FALSE(err.isEmpty());
    EXPECT_TRUE(err.contains("must be a JSON object"));
}

TEST(ServiceConfigSchema, FromJsonFileItemsNotObject) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString path = tmpDir.filePath("bad_items.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(R"({"tags": {"type": "array", "items": "string"}})");
    f.close();

    QString err;
    auto schema = ServiceConfigSchema::fromJsonFile(path, err);
    EXPECT_FALSE(err.isEmpty());
    EXPECT_TRUE(err.contains("items"));
}

TEST(ServiceConfigSchema, FromJsonFileFieldsNotObject) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString path = tmpDir.filePath("bad_fields.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(R"({"server": {"type": "object", "fields": [1,2,3]}})");
    f.close();

    QString err;
    auto schema = ServiceConfigSchema::fromJsonFile(path, err);
    EXPECT_FALSE(err.isEmpty());
    EXPECT_TRUE(err.contains("fields"));
}

TEST(ServiceConfigSchema, FromJsonFileEmptyObject) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString path = tmpDir.filePath("empty.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("{}");
    f.close();

    QString err;
    auto schema = ServiceConfigSchema::fromJsonFile(path, err);
    EXPECT_TRUE(err.isEmpty()) << err.toStdString();
    EXPECT_TRUE(schema.fields.isEmpty());
}
