#include <gtest/gtest.h>

#include "stdiolink/protocol/meta_validator.h"

using namespace stdiolink::meta;

class MetaValidatorTest : public ::testing::Test {};

// 测试类型检查 - String
TEST_F(MetaValidatorTest, TypeCheckString) {
    FieldMeta field;
    field.name = "name";
    field.type = FieldType::String;

    auto r1 = MetaValidator::validateField(QJsonValue("hello"), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue(123), field);
    EXPECT_FALSE(r2.valid);

    auto r3 = MetaValidator::validateField(QJsonValue(true), field);
    EXPECT_FALSE(r3.valid);
}

// 测试类型检查 - Int
TEST_F(MetaValidatorTest, TypeCheckInt) {
    FieldMeta field;
    field.name = "count";
    field.type = FieldType::Int;

    auto r1 = MetaValidator::validateField(QJsonValue(42), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue(3.14), field);
    EXPECT_FALSE(r2.valid);

    auto r3 = MetaValidator::validateField(QJsonValue("42"), field);
    EXPECT_FALSE(r3.valid);
}

// 测试类型检查 - Int64
TEST_F(MetaValidatorTest, TypeCheckInt64) {
    FieldMeta field;
    field.name = "bignum";
    field.type = FieldType::Int64;

    auto r1 = MetaValidator::validateField(QJsonValue(1234567890), field);
    EXPECT_TRUE(r1.valid);

    // 超出安全范围 (使用明确超出 2^53 的值)
    auto r2 = MetaValidator::validateField(QJsonValue(1e16), field);
    EXPECT_FALSE(r2.valid);
}

// 测试类型检查 - Double
TEST_F(MetaValidatorTest, TypeCheckDouble) {
    FieldMeta field;
    field.name = "value";
    field.type = FieldType::Double;

    auto r1 = MetaValidator::validateField(QJsonValue(3.14), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue(42), field);
    EXPECT_TRUE(r2.valid);  // 整数也是有效的 double

    auto r3 = MetaValidator::validateField(QJsonValue("3.14"), field);
    EXPECT_FALSE(r3.valid);
}

// 测试类型检查 - Bool
TEST_F(MetaValidatorTest, TypeCheckBool) {
    FieldMeta field;
    field.name = "enabled";
    field.type = FieldType::Bool;

    auto r1 = MetaValidator::validateField(QJsonValue(true), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue(false), field);
    EXPECT_TRUE(r2.valid);

    auto r3 = MetaValidator::validateField(QJsonValue("true"), field);
    EXPECT_FALSE(r3.valid);

    auto r4 = MetaValidator::validateField(QJsonValue(1), field);
    EXPECT_FALSE(r4.valid);
}

// 测试类型检查 - Object
TEST_F(MetaValidatorTest, TypeCheckObject) {
    FieldMeta field;
    field.name = "data";
    field.type = FieldType::Object;

    auto r1 = MetaValidator::validateField(QJsonValue(QJsonObject{{"key", "value"}}), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue(QJsonArray{1, 2, 3}), field);
    EXPECT_FALSE(r2.valid);
}

// 测试类型检查 - Array
TEST_F(MetaValidatorTest, TypeCheckArray) {
    FieldMeta field;
    field.name = "items";
    field.type = FieldType::Array;

    auto r1 = MetaValidator::validateField(QJsonValue(QJsonArray{1, 2, 3}), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue(QJsonObject{{"key", "value"}}), field);
    EXPECT_FALSE(r2.valid);
}

// 测试类型检查 - Any
TEST_F(MetaValidatorTest, TypeCheckAny) {
    FieldMeta field;
    field.name = "anything";
    field.type = FieldType::Any;

    EXPECT_TRUE(MetaValidator::validateField(QJsonValue("string"), field).valid);
    EXPECT_TRUE(MetaValidator::validateField(QJsonValue(123), field).valid);
    EXPECT_TRUE(MetaValidator::validateField(QJsonValue(true), field).valid);
    EXPECT_TRUE(MetaValidator::validateField(QJsonValue(QJsonObject{}), field).valid);
}

// 测试数值范围约束
TEST_F(MetaValidatorTest, RangeConstraint) {
    FieldMeta field;
    field.name = "value";
    field.type = FieldType::Int;
    field.constraints.min = 0;
    field.constraints.max = 100;

    auto r1 = MetaValidator::validateField(QJsonValue(50), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue(0), field);
    EXPECT_TRUE(r2.valid);

    auto r3 = MetaValidator::validateField(QJsonValue(100), field);
    EXPECT_TRUE(r3.valid);

    auto r4 = MetaValidator::validateField(QJsonValue(-1), field);
    EXPECT_FALSE(r4.valid);

    auto r5 = MetaValidator::validateField(QJsonValue(101), field);
    EXPECT_FALSE(r5.valid);
}

// 测试字符串长度约束
TEST_F(MetaValidatorTest, StringLengthConstraint) {
    FieldMeta field;
    field.name = "username";
    field.type = FieldType::String;
    field.constraints.minLength = 3;
    field.constraints.maxLength = 20;

    auto r1 = MetaValidator::validateField(QJsonValue("alice"), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue("ab"), field);
    EXPECT_FALSE(r2.valid);

    auto r3 = MetaValidator::validateField(QJsonValue("this_is_a_very_long_username"), field);
    EXPECT_FALSE(r3.valid);
}

// 测试正则表达式约束
TEST_F(MetaValidatorTest, PatternConstraint) {
    FieldMeta field;
    field.name = "email";
    field.type = FieldType::String;
    field.constraints.pattern = R"(^[\w.-]+@[\w.-]+\.\w+$)";

    auto r1 = MetaValidator::validateField(QJsonValue("test@example.com"), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue("invalid-email"), field);
    EXPECT_FALSE(r2.valid);
}

// 测试枚举值约束
TEST_F(MetaValidatorTest, EnumConstraint) {
    FieldMeta field;
    field.name = "mode";
    field.type = FieldType::Enum;
    field.constraints.enumValues = QJsonArray{"fast", "normal", "slow"};

    auto r1 = MetaValidator::validateField(QJsonValue("fast"), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue("normal"), field);
    EXPECT_TRUE(r2.valid);

    auto r3 = MetaValidator::validateField(QJsonValue("invalid"), field);
    EXPECT_FALSE(r3.valid);
}

// 测试数组长度约束
TEST_F(MetaValidatorTest, ArrayLengthConstraint) {
    FieldMeta field;
    field.name = "tags";
    field.type = FieldType::Array;
    field.constraints.minItems = 1;
    field.constraints.maxItems = 5;

    auto r1 = MetaValidator::validateField(QJsonValue(QJsonArray{"a", "b"}), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue(QJsonArray{}), field);
    EXPECT_FALSE(r2.valid);

    auto r3 = MetaValidator::validateField(QJsonValue(QJsonArray{"a", "b", "c", "d", "e", "f"}), field);
    EXPECT_FALSE(r3.valid);
}

// 测试必填字段
TEST_F(MetaValidatorTest, RequiredField) {
    CommandMeta cmd;
    cmd.name = "test";
    FieldMeta f1, f2;
    f1.name = "required_field";
    f1.type = FieldType::String;
    f1.required = true;
    f2.name = "optional_field";
    f2.type = FieldType::String;
    f2.required = false;
    cmd.params = {f1, f2};

    QJsonObject data1{{"required_field", "value"}};
    auto r1 = MetaValidator::validateParams(data1, cmd);
    EXPECT_TRUE(r1.valid);

    QJsonObject data2{{"optional_field", "value"}};
    auto r2 = MetaValidator::validateParams(data2, cmd);
    EXPECT_FALSE(r2.valid);
    EXPECT_EQ(r2.errorField, "required_field");
}

// 测试允许未知字段
TEST_F(MetaValidatorTest, AllowUnknownFields) {
    CommandMeta cmd;
    cmd.name = "test";
    FieldMeta f1;
    f1.name = "known";
    f1.type = FieldType::String;
    cmd.params = {f1};

    QJsonObject data{{"known", "ok"}, {"extra", 1}};
    auto r1 = MetaValidator::validateParams(data, cmd, true);
    EXPECT_TRUE(r1.valid);
}

// 测试禁止未知字段
TEST_F(MetaValidatorTest, DisallowUnknownFields) {
    CommandMeta cmd;
    cmd.name = "test";
    FieldMeta f1;
    f1.name = "known";
    f1.type = FieldType::String;
    cmd.params = {f1};

    QJsonObject data{{"known", "ok"}, {"extra", 1}};
    auto r1 = MetaValidator::validateParams(data, cmd, false);
    EXPECT_FALSE(r1.valid);
    EXPECT_EQ(r1.errorField, "extra");
}

// 测试参数不是 object
TEST_F(MetaValidatorTest, ParamsMustBeObject) {
    CommandMeta cmd;
    cmd.name = "test";

    auto r1 = MetaValidator::validateParams(QJsonValue("bad"), cmd);
    EXPECT_FALSE(r1.valid);
}

// 测试嵌套 Object 验证
TEST_F(MetaValidatorTest, NestedObjectValidation) {
    FieldMeta addressField;
    addressField.name = "address";
    addressField.type = FieldType::Object;

    FieldMeta streetField;
    streetField.name = "street";
    streetField.type = FieldType::String;
    streetField.required = true;

    FieldMeta cityField;
    cityField.name = "city";
    cityField.type = FieldType::String;
    cityField.required = true;

    addressField.fields = {streetField, cityField};

    QJsonObject validAddr{{"street", "123 Main St"}, {"city", "Boston"}};
    auto r1 = MetaValidator::validateField(QJsonValue(validAddr), addressField);
    EXPECT_TRUE(r1.valid);

    QJsonObject invalidAddr{{"street", "123 Main St"}};
    auto r2 = MetaValidator::validateField(QJsonValue(invalidAddr), addressField);
    EXPECT_FALSE(r2.valid);
}

// 测试 requiredKeys
TEST_F(MetaValidatorTest, RequiredKeysValidation) {
    FieldMeta obj;
    obj.name = "settings";
    obj.type = FieldType::Object;
    obj.requiredKeys = QStringList({"mode", "level"});
    FieldMeta modeField;
    modeField.name = "mode";
    modeField.type = FieldType::String;
    obj.fields = {modeField};

    QJsonObject data{{"mode", "fast"}};
    auto r1 = MetaValidator::validateField(QJsonValue(data), obj);
    EXPECT_FALSE(r1.valid);
    EXPECT_EQ(r1.errorField, "settings.level");
}

// 测试数组元素验证
TEST_F(MetaValidatorTest, ArrayItemsValidation) {
    FieldMeta field;
    field.name = "numbers";
    field.type = FieldType::Array;
    field.items = std::make_shared<FieldMeta>();
    field.items->name = "item";
    field.items->type = FieldType::Int;

    auto r1 = MetaValidator::validateField(QJsonValue(QJsonArray{1, 2, 3}), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue(QJsonArray{1, "two", 3}), field);
    EXPECT_FALSE(r2.valid);
}

// 测试 validateConfig
TEST_F(MetaValidatorTest, ValidateConfig) {
    ConfigSchema schema;
    FieldMeta f;
    f.name = "timeout";
    f.type = FieldType::Int;
    f.required = true;
    schema.fields.append(f);

    QJsonObject ok{{"timeout", 10}};
    auto r1 = MetaValidator::validateConfig(ok, schema);
    EXPECT_TRUE(r1.valid);

    QJsonObject bad;
    auto r2 = MetaValidator::validateConfig(bad, schema);
    EXPECT_FALSE(r2.valid);
    EXPECT_EQ(r2.errorField, "timeout");
}

// DefaultFiller 测试
class DefaultFillerTest : public ::testing::Test {};

// 测试默认值填充
TEST_F(DefaultFillerTest, FillMissingDefaults) {
    CommandMeta cmd;
    cmd.name = "test";
    FieldMeta f1, f2;
    f1.name = "timeout";
    f1.type = FieldType::Int;
    f1.defaultValue = 5000;
    f2.name = "mode";
    f2.type = FieldType::String;
    f2.defaultValue = "normal";
    cmd.params = {f1, f2};

    QJsonObject data{{"timeout", 3000}};
    QJsonObject filled = DefaultFiller::fillDefaults(data, cmd);

    EXPECT_EQ(filled["timeout"].toInt(), 3000);  // 保留原值
    EXPECT_EQ(filled["mode"].toString(), "normal");  // 填充默认值
}

// 测试不覆盖已有值
TEST_F(DefaultFillerTest, PreserveExistingValues) {
    QVector<FieldMeta> fields;
    FieldMeta f;
    f.name = "value";
    f.type = FieldType::String;
    f.defaultValue = "default";
    fields.append(f);

    QJsonObject data{{"value", "custom"}};
    QJsonObject filled = DefaultFiller::fillDefaults(data, fields);

    EXPECT_EQ(filled["value"].toString(), "custom");
}

// 测试空默认值不填充
TEST_F(DefaultFillerTest, SkipNullDefaults) {
    QVector<FieldMeta> fields;
    FieldMeta f;
    f.name = "optional";
    f.type = FieldType::String;
    // defaultValue 默认为 null
    fields.append(f);

    QJsonObject data;
    QJsonObject filled = DefaultFiller::fillDefaults(data, fields);

    EXPECT_FALSE(filled.contains("optional"));
}
