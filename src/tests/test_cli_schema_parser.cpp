#include <gtest/gtest.h>

#include <QJsonDocument>
#include <QJsonObject>

#include "config/service_config_schema.h"
#include "stdiolink/console/cli_schema_parser.h"

using namespace stdiolink;
using namespace stdiolink_service;

namespace {

ServiceConfigSchema loadSchema(const char* jsonText) {
    QString error;
    const auto schema = ServiceConfigSchema::fromJsonObject(
        QJsonDocument::fromJson(QByteArray(jsonText)).object(),
        error);
    EXPECT_TRUE(error.isEmpty()) << error.toStdString();
    return schema;
}

} // namespace

TEST(CliSchemaParserTest, ServiceStringFieldKeepsNumericLikeString) {
    const auto schema = loadSchema(R"({
      "vision": {
        "type": "object",
        "fields": {
          "password": { "type": "string", "required": true },
          "mode_code": { "type": "enum", "constraints": { "enumValues": ["1", "2"] } }
        }
      }
    })");

    QJsonObject out;
    QString error;
    ASSERT_TRUE(CliSchemaParser::parseArgs(
        {
            {"vision.password", "123456"},
            {"vision.mode_code", "1"},
        },
        schema.fields,
        out,
        &error)) << error.toStdString();

    const auto vision = out["vision"].toObject();
    EXPECT_TRUE(vision["password"].isString());
    EXPECT_EQ(vision["password"].toString(), "123456");
    EXPECT_TRUE(vision["mode_code"].isString());
    EXPECT_EQ(vision["mode_code"].toString(), "1");
}

TEST(CliSchemaParserTest, ServiceArrayObjectPathHitsLeafFieldTypes) {
    const auto schema = loadSchema(R"({
      "cranes": {
        "type": "array",
        "items": {
          "type": "object",
          "fields": {
            "host": { "type": "string", "required": true },
            "port": { "type": "int", "required": true },
            "unit_id": { "type": "int", "required": true }
          }
        }
      }
    })");

    QJsonObject out;
    QString error;
    ASSERT_TRUE(CliSchemaParser::parseArgs(
        {
            {"cranes[0].host", "127.0.0.1"},
            {"cranes[0].port", "502"},
            {"cranes[0].unit_id", "1"},
        },
        schema.fields,
        out,
        &error)) << error.toStdString();

    const auto cranes = out["cranes"].toArray();
    ASSERT_EQ(cranes.size(), 1);
    const auto crane = cranes[0].toObject();
    EXPECT_EQ(crane["host"].toString(), "127.0.0.1");
    EXPECT_EQ(crane["port"].toInt(), 502);
    EXPECT_EQ(crane["unit_id"].toInt(), 1);
}

TEST(CliSchemaParserTest, UnknownFieldFallsBackToFriendlyInference) {
    QJsonObject out;
    QString error;
    ASSERT_TRUE(CliSchemaParser::parseArgs(
        {
            {"threshold", "3.5"},
            {"enabled", "true"},
        },
        {},
        out,
        &error)) << error.toStdString();

    EXPECT_TRUE(out["threshold"].isDouble());
    EXPECT_DOUBLE_EQ(out["threshold"].toDouble(), 3.5);
    EXPECT_TRUE(out["enabled"].toBool());
}

TEST(CliSchemaParserTest, Int64OutOfSafeRangeFails) {
    const auto schema = loadSchema(R"({
      "safe_counter": { "type": "int64", "required": true }
    })");

    QJsonObject out;
    QString error;
    int errorIndex = -1;
    EXPECT_FALSE(CliSchemaParser::parseArgs(
        {
            {"safe_counter", "9007199254740993"},
        },
        schema.fields,
        out,
        &error,
        &errorIndex));
    EXPECT_EQ(errorIndex, 0);
    EXPECT_TRUE(error.contains("safe range"));
}

TEST(CliSchemaParserTest, ArrayAndObjectFieldsRequireMatchingLiterals) {
    const auto schema = loadSchema(R"({
      "tags": { "type": "array", "required": true },
      "vision": { "type": "object", "required": true }
    })");

    QJsonObject out;
    QString error;
    EXPECT_FALSE(CliSchemaParser::parseArgs(
        {
            {"tags", "oops"},
        },
        schema.fields,
        out,
        &error));
    EXPECT_TRUE(error.contains("expected array literal"));

    error.clear();
    EXPECT_FALSE(CliSchemaParser::parseArgs(
        {
            {"vision", "oops"},
        },
        schema.fields,
        out,
        &error));
    EXPECT_TRUE(error.contains("expected object literal"));
}

TEST(CliSchemaParserTest, ScalarFieldChildPathFailsEarly) {
    const auto schema = loadSchema(R"({
      "vision": {
        "type": "object",
        "fields": {
          "password": { "type": "string", "required": true }
        }
      }
    })");

    QJsonObject out;
    QString error;
    EXPECT_FALSE(CliSchemaParser::parseArgs(
        {
            {"vision.password.value", "123456"},
        },
        schema.fields,
        out,
        &error));
    EXPECT_TRUE(error.contains("path exceeds scalar field: password"));
}

TEST(CliSchemaParserTest, ContainerLiteralAndChildPathConflictFails) {
    const auto schema = loadSchema(R"({
      "units": {
        "type": "array",
        "items": {
          "type": "object",
          "fields": {
            "id": { "type": "int", "required": true },
            "size": { "type": "int", "required": true }
          }
        }
      }
    })");

    QJsonObject out;
    QString error;
    EXPECT_FALSE(CliSchemaParser::parseArgs(
        {
            {"units", R"([{"id":1}])"},
            {"units[0].size", "2"},
        },
        schema.fields,
        out,
        &error));
    EXPECT_TRUE(error.contains("container literal vs child path"));
}
