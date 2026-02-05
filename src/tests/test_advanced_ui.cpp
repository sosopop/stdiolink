#include <gtest/gtest.h>
#include "stdiolink/host/form_generator.h"
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink;
using namespace stdiolink::meta;

// ============================================
// 嵌套对象表单生成测试
// ============================================

TEST(AdvancedUi, NestedObjectForm) {
    CommandMeta cmd;
    cmd.name = "configure";

    FieldMeta outer;
    outer.name = "config";
    outer.type = FieldType::Object;

    FieldMeta inner;
    inner.name = "timeout";
    inner.type = FieldType::Int;
    outer.fields.append(inner);

    cmd.params.append(outer);

    FormDesc form = UiGenerator::generateCommandForm(cmd);
    EXPECT_EQ(form.widgets.size(), 1);

    auto widget = form.widgets[0].toObject();
    EXPECT_EQ(widget["name"].toString(), "config");
    EXPECT_EQ(widget["widget"].toString(), "object");
    EXPECT_TRUE(widget.contains("fields"));
    EXPECT_EQ(widget["fields"].toArray().size(), 1);
}

TEST(AdvancedUi, DeepNestedObject) {
    FieldMeta level1;
    level1.name = "level1";
    level1.type = FieldType::Object;

    FieldMeta level2;
    level2.name = "level2";
    level2.type = FieldType::Object;

    FieldMeta level3;
    level3.name = "value";
    level3.type = FieldType::String;
    level2.fields.append(level3);

    level1.fields.append(level2);

    CommandMeta cmd;
    cmd.params.append(level1);

    FormDesc form = UiGenerator::generateCommandForm(cmd);
    auto w1 = form.widgets[0].toObject();
    auto w2 = w1["fields"].toArray()[0].toObject();
    EXPECT_TRUE(w2.contains("fields"));
}

// ============================================
// 数组表单生成测试
// ============================================

TEST(AdvancedUi, ArrayForm) {
    FieldMeta field;
    field.name = "tags";
    field.type = FieldType::Array;
    field.items = std::make_shared<FieldMeta>();
    field.items->type = FieldType::String;

    CommandMeta cmd;
    cmd.params.append(field);

    FormDesc form = UiGenerator::generateCommandForm(cmd);
    auto widget = form.widgets[0].toObject();

    EXPECT_EQ(widget["widget"].toString(), "array");
    EXPECT_TRUE(widget.contains("items"));
    EXPECT_EQ(widget["items"].toObject()["type"].toString(), "string");
}

TEST(AdvancedUi, ArrayOfObjects) {
    FieldMeta field;
    field.name = "users";
    field.type = FieldType::Array;
    field.items = std::make_shared<FieldMeta>();
    field.items->type = FieldType::Object;

    FieldMeta nameField;
    nameField.name = "name";
    nameField.type = FieldType::String;
    field.items->fields.append(nameField);

    CommandMeta cmd;
    cmd.params.append(field);

    FormDesc form = UiGenerator::generateCommandForm(cmd);
    auto widget = form.widgets[0].toObject();
    auto items = widget["items"].toObject();

    EXPECT_TRUE(items.contains("fields"));
}

// ============================================
// 条件显示测试
// ============================================

TEST(AdvancedUi, VisibleIfInWidget) {
    FieldMeta field;
    field.name = "advanced_option";
    field.type = FieldType::String;
    field.ui.visibleIf = "mode == 'advanced'";

    CommandMeta cmd;
    cmd.params.append(field);

    FormDesc form = UiGenerator::generateCommandForm(cmd);
    auto widget = form.widgets[0].toObject();

    EXPECT_EQ(widget["visibleIf"].toString(), "mode == 'advanced'");
}

TEST(AdvancedUi, VisibleIfEvaluationEqual) {
    QString condition = "mode == 'advanced'";
    QJsonObject context{{"mode", "advanced"}};

    EXPECT_TRUE(ConditionEvaluator::evaluate(condition, context));

    context["mode"] = "simple";
    EXPECT_FALSE(ConditionEvaluator::evaluate(condition, context));
}

TEST(AdvancedUi, VisibleIfEvaluationNotEqual) {
    QString condition = "mode != 'simple'";
    QJsonObject context{{"mode", "advanced"}};

    EXPECT_TRUE(ConditionEvaluator::evaluate(condition, context));

    context["mode"] = "simple";
    EXPECT_FALSE(ConditionEvaluator::evaluate(condition, context));
}

TEST(AdvancedUi, VisibleIfNumericComparison) {
    QJsonObject context{{"count", 10}};

    EXPECT_TRUE(ConditionEvaluator::evaluate("count > 5", context));
    EXPECT_FALSE(ConditionEvaluator::evaluate("count > 15", context));
    EXPECT_TRUE(ConditionEvaluator::evaluate("count >= 10", context));
    EXPECT_TRUE(ConditionEvaluator::evaluate("count <= 10", context));
}

TEST(AdvancedUi, VisibleIfBooleanField) {
    QJsonObject context{{"enabled", true}};

    EXPECT_TRUE(ConditionEvaluator::evaluate("enabled", context));
    EXPECT_FALSE(ConditionEvaluator::evaluate("!enabled", context));

    context["enabled"] = false;
    EXPECT_FALSE(ConditionEvaluator::evaluate("enabled", context));
    EXPECT_TRUE(ConditionEvaluator::evaluate("!enabled", context));
}

// ============================================
// 分组排序测试
// ============================================

TEST(AdvancedUi, FieldGrouping) {
    QVector<FieldMeta> fields;

    FieldMeta f1, f2, f3;
    f1.name = "a"; f1.ui.group = "basic";
    f2.name = "b"; f2.ui.group = "advanced";
    f3.name = "c"; f3.ui.group = "basic";
    fields = {f1, f2, f3};

    auto grouped = UiGenerator::groupFields(fields);
    EXPECT_EQ(grouped["basic"].size(), 2);
    EXPECT_EQ(grouped["advanced"].size(), 1);
}

TEST(AdvancedUi, FieldGroupingDefault) {
    QVector<FieldMeta> fields;

    FieldMeta f1, f2;
    f1.name = "a";  // 无 group，应归入 default
    f2.name = "b"; f2.ui.group = "custom";
    fields = {f1, f2};

    auto grouped = UiGenerator::groupFields(fields);
    EXPECT_EQ(grouped["default"].size(), 1);
    EXPECT_EQ(grouped["custom"].size(), 1);
}

TEST(AdvancedUi, FieldOrdering) {
    QVector<FieldMeta> fields;

    FieldMeta f1, f2, f3;
    f1.name = "c"; f1.ui.order = 3;
    f2.name = "a"; f2.ui.order = 1;
    f3.name = "b"; f3.ui.order = 2;
    fields = {f1, f2, f3};

    auto sorted = UiGenerator::sortFields(fields);
    EXPECT_EQ(sorted[0].name, "a");
    EXPECT_EQ(sorted[1].name, "b");
    EXPECT_EQ(sorted[2].name, "c");
}

TEST(AdvancedUi, FieldOrderingStable) {
    QVector<FieldMeta> fields;

    FieldMeta f1, f2, f3;
    f1.name = "x"; f1.ui.order = 0;
    f2.name = "y"; f2.ui.order = 0;
    f3.name = "z"; f3.ui.order = 0;
    fields = {f1, f2, f3};

    auto sorted = UiGenerator::sortFields(fields);
    // 相同 order 时保持原顺序
    EXPECT_EQ(sorted.size(), 3);
}
