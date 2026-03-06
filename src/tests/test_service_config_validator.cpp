#include <gtest/gtest.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "config/service_config_validator.h"
#include "config/service_config_schema.h"

using namespace stdiolink_service;
using namespace stdiolink::meta;

namespace {

const char* kArrayObjectSchema = R"({
  "radars": {
    "type": "array",
    "required": true,
    "description": "激光雷达设备列表",
    "constraints": { "minItems": 1 },
    "items": {
      "type": "object",
      "description": "单个雷达配置",
      "fields": {
        "id":   { "type": "string", "required": true },
        "host": { "type": "string", "required": true },
        "port": { "type": "int", "required": true, "constraints": { "min": 1, "max": 65535 } }
      }
    }
  }
})";

} // namespace

class ServiceConfigValidatorTest : public ::testing::Test {
protected:
    ServiceConfigSchema makeSchema() {
        ServiceConfigSchema schema;

        FieldMeta port;
        port.name = "port";
        port.type = FieldType::Int;
        port.required = true;
        port.constraints.min = 1;
        port.constraints.max = 65535;

        FieldMeta debug;
        debug.name = "debug";
        debug.type = FieldType::Bool;
        debug.defaultValue = false;

        FieldMeta mode;
        mode.name = "mode";
        mode.type = FieldType::Enum;
        mode.defaultValue = QJsonValue("normal");
        mode.constraints.enumValues = QJsonArray{"fast", "normal", "slow"};

        FieldMeta name;
        name.name = "name";
        name.type = FieldType::String;
        name.required = true;
        name.constraints.minLength = 1;
        name.constraints.maxLength = 64;

        schema.fields = {port, debug, mode, name};
        return schema;
    }
};

TEST_F(ServiceConfigValidatorTest, RequiredFieldMissing) {
    auto schema = makeSchema();
    QJsonObject config{{"debug", true}};
    auto r = ServiceConfigValidator::validate(schema, config);
    EXPECT_FALSE(r.valid);
    EXPECT_FALSE(r.errorField.isEmpty());
}

TEST_F(ServiceConfigValidatorTest, TypeMismatch) {
    auto schema = makeSchema();
    QJsonObject config{{"port", "not_a_number"}, {"name", "test"}};
    auto r = ServiceConfigValidator::validate(schema, config);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.errorField, "port");
}

TEST_F(ServiceConfigValidatorTest, RangeConstraint) {
    auto schema = makeSchema();
    QJsonObject config{{"port", 99999}, {"name", "test"}};
    auto r = ServiceConfigValidator::validate(schema, config);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.errorField, "port");
}

TEST_F(ServiceConfigValidatorTest, StringLengthConstraint) {
    auto schema = makeSchema();
    QString longName(65, 'x');
    QJsonObject config{{"port", 6200}, {"name", longName}};
    auto r = ServiceConfigValidator::validate(schema, config);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.errorField, "name");
}

TEST_F(ServiceConfigValidatorTest, EnumConstraint) {
    auto schema = makeSchema();
    QJsonObject config{{"port", 6200}, {"name", "test"}, {"mode", "invalid"}};
    auto r = ServiceConfigValidator::validate(schema, config);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.errorField, "mode");
}

TEST_F(ServiceConfigValidatorTest, FillDefaults) {
    auto schema = makeSchema();
    QJsonObject config{{"port", 6200}, {"name", "test"}};
    auto filled = ServiceConfigValidator::fillDefaults(schema, config);
    EXPECT_EQ(filled["debug"].toBool(), false);
    EXPECT_EQ(filled["mode"].toString(), "normal");
    EXPECT_EQ(filled["port"].toInt(), 6200);
}

TEST_F(ServiceConfigValidatorTest, MergePriority) {
    auto schema = makeSchema();
    QJsonObject fileConfig{{"port", 3000}, {"name", "file"}, {"debug", true}};
    QJsonObject cliConfig{{"port", "6200"}};
    QJsonObject merged;
    auto r = ServiceConfigValidator::mergeAndValidate(
        schema, fileConfig, cliConfig, UnknownFieldPolicy::Reject, merged);
    EXPECT_TRUE(r.valid) << r.toString().toStdString();
    EXPECT_EQ(merged["port"].toInt(), 6200);
    EXPECT_EQ(merged["name"].toString(), "file");
    EXPECT_EQ(merged["debug"].toBool(), true);
    EXPECT_EQ(merged["mode"].toString(), "normal");
}

TEST_F(ServiceConfigValidatorTest, ValidConfigPasses) {
    auto schema = makeSchema();
    QJsonObject config{
        {"port", 6200}, {"name", "myService"},
        {"debug", false}, {"mode", "fast"}
    };
    auto r = ServiceConfigValidator::validate(schema, config);
    EXPECT_TRUE(r.valid);
}

TEST_F(ServiceConfigValidatorTest, DeepMergeObject) {
    ServiceConfigSchema schema;
    FieldMeta server;
    server.name = "server";
    server.type = FieldType::Object;

    FieldMeta host;
    host.name = "host";
    host.type = FieldType::String;
    host.defaultValue = QJsonValue("localhost");

    FieldMeta port;
    port.name = "port";
    port.type = FieldType::Int;

    server.fields = {host, port};
    schema.fields = {server};

    QJsonObject fileConfig{
        {"server", QJsonObject{{"host", "127.0.0.1"}, {"port", 3000}}}
    };
    QJsonObject cliConfig{
        {"server", QJsonObject{{"port", "6200"}}}
    };
    QJsonObject merged;
    auto r = ServiceConfigValidator::mergeAndValidate(
        schema, fileConfig, cliConfig, UnknownFieldPolicy::Allow, merged);
    EXPECT_TRUE(r.valid) << r.toString().toStdString();
    auto srv = merged["server"].toObject();
    EXPECT_EQ(srv["host"].toString(), "127.0.0.1");
    EXPECT_EQ(srv["port"].toInt(), 6200);
}

TEST_F(ServiceConfigValidatorTest, ArrayReplaceInsteadOfMerge) {
    ServiceConfigSchema schema;
    FieldMeta tags;
    tags.name = "tags";
    tags.type = FieldType::Array;
    tags.defaultValue = QJsonArray{};
    schema.fields = {tags};

    QJsonObject fileConfig{{"tags", QJsonArray{"a", "b"}}};
    QJsonObject cliConfig{{"tags", "[\"x\"]"}};
    QJsonObject merged;
    auto r = ServiceConfigValidator::mergeAndValidate(
        schema, fileConfig, cliConfig, UnknownFieldPolicy::Allow, merged);
    EXPECT_TRUE(r.valid) << r.toString().toStdString();
    auto arr = merged["tags"].toArray();
    ASSERT_EQ(arr.size(), 1);
    EXPECT_EQ(arr[0].toString(), "x");
}

TEST_F(ServiceConfigValidatorTest, RejectUnknownFieldByDefault) {
    auto schema = makeSchema();
    QJsonObject config{
        {"port", 6200}, {"name", "test"}, {"unknown", "value"}
    };
    QJsonObject merged;
    auto r = ServiceConfigValidator::mergeAndValidate(
        schema, QJsonObject{}, config, UnknownFieldPolicy::Reject, merged);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.errorField, "unknown");
}

TEST_F(ServiceConfigValidatorTest, RawStringConversion) {
    auto schema = makeSchema();
    QJsonObject rawCli{
        {"port", "6200"}, {"name", "test"}, {"debug", "true"}
    };
    QJsonObject merged;
    auto r = ServiceConfigValidator::mergeAndValidate(
        schema, QJsonObject{}, rawCli, UnknownFieldPolicy::Reject, merged);
    EXPECT_TRUE(r.valid) << r.toString().toStdString();
    EXPECT_EQ(merged["port"].toInt(), 6200);
    EXPECT_EQ(merged["debug"].toBool(), true);
}

TEST_F(ServiceConfigValidatorTest, AnyFieldParsesJsonLiteralFromCli) {
    ServiceConfigSchema schema;
    FieldMeta anyField;
    anyField.name = "anyv";
    anyField.type = FieldType::Any;
    anyField.required = true;
    schema.fields = {anyField};

    QJsonObject rawCli{{"anyv", "1"}};
    QJsonObject merged;
    auto r = ServiceConfigValidator::mergeAndValidate(
        schema, QJsonObject{}, rawCli, UnknownFieldPolicy::Reject, merged);
    EXPECT_TRUE(r.valid) << r.toString().toStdString();
    EXPECT_TRUE(merged["anyv"].isDouble());
    EXPECT_EQ(merged["anyv"].toInt(), 1);
}

TEST_F(ServiceConfigValidatorTest, RejectUnknownNestedField) {
    ServiceConfigSchema schema;
    FieldMeta server;
    server.name = "server";
    server.type = FieldType::Object;
    FieldMeta host;
    host.name = "host";
    host.type = FieldType::String;
    server.fields = {host};
    schema.fields = {server};

    QJsonObject rawCli{
        {"server", QJsonObject{{"bad", "1"}}}
    };
    QJsonObject merged;
    auto r = ServiceConfigValidator::mergeAndValidate(
        schema, QJsonObject{}, rawCli, UnknownFieldPolicy::Reject, merged);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.errorField, "server.bad");
}

class ValidatorArrayObjectTest : public ::testing::Test {
protected:
    void SetUp() override {
        QString error;
        m_schema = ServiceConfigSchema::fromJsonObject(
            QJsonDocument::fromJson(kArrayObjectSchema).object(), error);
        ASSERT_TRUE(error.isEmpty()) << error.toStdString();
    }

    ServiceConfigSchema m_schema;
};

TEST_F(ValidatorArrayObjectTest, R_CPP_05_RejectUnknownField_InsideArrayObjectItem_FirstItem) {
    const QJsonObject config = QJsonDocument::fromJson(R"({
      "radars": [
        { "id": "r1", "host": "192.168.1.1", "port": 2368, "bad_key": "value" }
      ]
    })").object();

    auto result = ServiceConfigValidator::rejectUnknownFields(m_schema, config, "");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorField, QString("radars[0].bad_key"));
}

TEST_F(ValidatorArrayObjectTest, R_CPP_06_RejectUnknownField_InsideArrayObjectItem_SecondItem) {
    const QJsonObject config = QJsonDocument::fromJson(R"({
      "radars": [
        { "id": "r1", "host": "192.168.1.1", "port": 2368 },
        { "id": "r2", "host": "192.168.1.2", "port": 2369, "bad_key": "x" }
      ]
    })").object();

    auto result = ServiceConfigValidator::rejectUnknownFields(m_schema, config, "");
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errorField, QString("radars[1].bad_key"));
}

TEST_F(ValidatorArrayObjectTest, R_CPP_08_RejectUnknownField_ArrayItemNotObject_Skipped) {
    const QJsonObject config = QJsonDocument::fromJson(R"({
      "radars": ["not-an-object"]
    })").object();

    auto result = ServiceConfigValidator::rejectUnknownFields(m_schema, config, "");
    EXPECT_TRUE(result.valid);
}

TEST_F(ValidatorArrayObjectTest, R_CPP_ValidArrayObject_AllFieldsKnown) {
    const QJsonObject config = QJsonDocument::fromJson(R"({
      "radars": [
        { "id": "r1", "host": "192.168.1.1", "port": 2368 }
      ]
    })").object();

    auto result = ServiceConfigValidator::rejectUnknownFields(m_schema, config, "");
    EXPECT_TRUE(result.valid);
}

TEST(ServiceConfigValidatorArrayObject, R_CPP_07_RejectUnknownField_ArrayOfPrimitive_Skipped) {
    ServiceConfigSchema schema;
    FieldMeta tagsMeta;
    tagsMeta.name = "tags";
    tagsMeta.type = FieldType::Array;
    auto itemsMeta = std::make_shared<FieldMeta>();
    itemsMeta->type = FieldType::String;
    tagsMeta.items = itemsMeta;
    schema.fields.append(tagsMeta);

    const QJsonObject config = QJsonDocument::fromJson(R"({"tags":["a","b"]})").object();
    auto result = ServiceConfigValidator::rejectUnknownFields(schema, config, "");
    EXPECT_TRUE(result.valid);
}

TEST(ServiceConfigValidatorArrayObject, R_CPP_10_RejectUnknownFields_OuterArrayItemsIsArray_NoRecurseNoCrash) {
    ServiceConfigSchema schema;
    FieldMeta outerArr;
    outerArr.name = "matrix";
    outerArr.type = FieldType::Array;
    auto innerArr = std::make_shared<FieldMeta>();
    innerArr->type = FieldType::Array;
    outerArr.items = innerArr;
    schema.fields.append(outerArr);

    const QJsonObject config = QJsonDocument::fromJson(R"({
      "matrix": [ [{"value": 1}], [{"value": 2, "bad_key": "x"}] ]
    })").object();
    auto result = ServiceConfigValidator::rejectUnknownFields(schema, config, "");
    EXPECT_TRUE(result.valid);
}

