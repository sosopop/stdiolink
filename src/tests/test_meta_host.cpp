#include <gtest/gtest.h>

#include "stdiolink/host/meta_cache.h"
#include "stdiolink/host/form_generator.h"
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class MetaCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        MetaCache::instance().clear();
    }
};

// 测试 MetaCache 存取
TEST_F(MetaCacheTest, StoreAndGet) {
    auto meta = std::make_shared<DriverMeta>();
    meta->info.id = "test.driver";
    meta->info.name = "Test";

    MetaCache::instance().store("test.driver", meta);
    auto retrieved = MetaCache::instance().get("test.driver");

    EXPECT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->info.id, "test.driver");
    EXPECT_EQ(retrieved->info.name, "Test");
}

// 测试获取不存在的缓存
TEST_F(MetaCacheTest, GetNonExistent) {
    auto retrieved = MetaCache::instance().get("non.existent");
    EXPECT_EQ(retrieved, nullptr);
}

// 测试缓存失效
TEST_F(MetaCacheTest, Invalidate) {
    auto meta = std::make_shared<DriverMeta>();
    meta->info.id = "test.driver2";

    MetaCache::instance().store("test.driver2", meta);
    MetaCache::instance().invalidate("test.driver2");

    auto retrieved = MetaCache::instance().get("test.driver2");
    EXPECT_EQ(retrieved, nullptr);
}

// 测试清空缓存
TEST_F(MetaCacheTest, Clear) {
    auto meta1 = std::make_shared<DriverMeta>();
    meta1->info.id = "driver1";
    auto meta2 = std::make_shared<DriverMeta>();
    meta2->info.id = "driver2";

    MetaCache::instance().store("driver1", meta1);
    MetaCache::instance().store("driver2", meta2);
    MetaCache::instance().clear();

    EXPECT_EQ(MetaCache::instance().get("driver1"), nullptr);
    EXPECT_EQ(MetaCache::instance().get("driver2"), nullptr);
}

// UiGenerator 测试
class UiGeneratorTest : public ::testing::Test {};

// 测试命令表单生成
TEST_F(UiGeneratorTest, GenerateCommandForm) {
    CommandMeta cmd;
    cmd.name = "scan";
    cmd.description = "执行扫描";

    FieldMeta param;
    param.name = "fps";
    param.type = FieldType::Int;
    param.description = "帧率";
    param.defaultValue = 10;
    param.constraints.min = 1;
    param.constraints.max = 60;
    cmd.params.append(param);

    FormDesc form = UiGenerator::generateCommandForm(cmd);

    EXPECT_EQ(form.title, "scan");
    EXPECT_EQ(form.description, "执行扫描");
    EXPECT_EQ(form.widgets.size(), 1);

    QJsonObject widget = form.widgets[0].toObject();
    EXPECT_EQ(widget["name"].toString(), "fps");
    EXPECT_EQ(widget["type"].toString(), "int");
    EXPECT_EQ(widget["default"].toInt(), 10);
    EXPECT_EQ(widget["min"].toInt(), 1);
    EXPECT_EQ(widget["max"].toInt(), 60);
}

// 测试带标题的命令表单
TEST_F(UiGeneratorTest, CommandFormWithTitle) {
    CommandMeta cmd;
    cmd.name = "scan";
    cmd.title = "扫描命令";
    cmd.description = "执行扫描操作";

    FormDesc form = UiGenerator::generateCommandForm(cmd);

    EXPECT_EQ(form.title, "扫描命令");
}

// 测试配置表单生成
TEST_F(UiGeneratorTest, GenerateConfigForm) {
    ConfigSchema config;

    FieldMeta f1;
    f1.name = "timeout";
    f1.type = FieldType::Int;
    f1.description = "超时时间";
    f1.defaultValue = 5000;
    f1.ui.unit = "ms";
    config.fields.append(f1);

    FieldMeta f2;
    f2.name = "verbose";
    f2.type = FieldType::Bool;
    f2.description = "详细输出";
    f2.defaultValue = false;
    config.fields.append(f2);

    FormDesc form = UiGenerator::generateConfigForm(config);

    EXPECT_EQ(form.title, "Configuration");
    EXPECT_EQ(form.widgets.size(), 2);

    QJsonObject w1 = form.widgets[0].toObject();
    EXPECT_EQ(w1["name"].toString(), "timeout");
    EXPECT_EQ(w1["unit"].toString(), "ms");

    QJsonObject w2 = form.widgets[1].toObject();
    EXPECT_EQ(w2["name"].toString(), "verbose");
    EXPECT_EQ(w2["widget"].toString(), "checkbox");
}

// 测试枚举字段控件
TEST_F(UiGeneratorTest, EnumFieldWidget) {
    CommandMeta cmd;
    cmd.name = "setMode";

    FieldMeta param;
    param.name = "mode";
    param.type = FieldType::Enum;
    param.description = "运行模式";
    param.constraints.enumValues = QJsonArray{"fast", "normal", "slow"};
    cmd.params.append(param);

    FormDesc form = UiGenerator::generateCommandForm(cmd);
    QJsonObject widget = form.widgets[0].toObject();

    EXPECT_EQ(widget["widget"].toString(), "select");
    EXPECT_EQ(widget["options"].toArray().size(), 3);
}

// 测试 toJson
TEST_F(UiGeneratorTest, ToJson) {
    FormDesc form;
    form.title = "Test Form";
    form.description = "A test form";
    form.widgets = QJsonArray{QJsonObject{{"name", "field1"}}};

    QJsonObject json = UiGenerator::toJson(form);

    EXPECT_EQ(json["title"].toString(), "Test Form");
    EXPECT_EQ(json["description"].toString(), "A test form");
    EXPECT_EQ(json["widgets"].toArray().size(), 1);
}

// 测试默认控件类型
TEST_F(UiGeneratorTest, DefaultWidgetTypes) {
    CommandMeta cmd;
    cmd.name = "test";

    FieldMeta stringField;
    stringField.name = "str";
    stringField.type = FieldType::String;
    cmd.params.append(stringField);

    FieldMeta intField;
    intField.name = "num";
    intField.type = FieldType::Int;
    cmd.params.append(intField);

    FieldMeta boolField;
    boolField.name = "flag";
    boolField.type = FieldType::Bool;
    cmd.params.append(boolField);

    FormDesc form = UiGenerator::generateCommandForm(cmd);

    EXPECT_EQ(form.widgets[0].toObject()["widget"].toString(), "text");
    EXPECT_EQ(form.widgets[1].toObject()["widget"].toString(), "number");
    EXPECT_EQ(form.widgets[2].toObject()["widget"].toString(), "checkbox");
}
