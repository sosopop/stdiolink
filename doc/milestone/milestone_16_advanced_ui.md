# 里程碑 16：高级 UI 生成策略

## 1. 目标

UiGenerator 从"基础表单"进化为"结构化交互引擎"。

## 2. 对应需求

- **需求4**: 复杂 UI 自动映射策略 (Advanced UI Generation)

## 3. 支持特性

| 特性 | 说明 |
|------|------|
| 嵌套对象 | Object 类型递归渲染 |
| 动态数组 | Array 可增删元素 |
| 条件显示 | visibleIf 依赖逻辑 |
| 分组排序 | group + order 属性 |
| 事件展示 | 根据 EventSchema 自动生成事件输出视图 |

## 4. 条件显示语法

```cpp
struct UIHint {
    QString visibleIf;  // "mode == 'advanced'"
};
```

### 4.1 UiGenerator 扩展接口（补充）

```cpp
class UiGenerator {
public:
    static QHash<QString, QVector<meta::FieldMeta>> groupFields(const QVector<meta::FieldMeta>& fields);
    static QVector<meta::FieldMeta> sortFields(const QVector<meta::FieldMeta>& fields);
};
```

## 5. 验收标准

1. 嵌套 Object 递归生成表单
2. Array 支持动态增删
3. visibleIf 条件正确解析
4. 字段按 group/order 排序
5. 事件流可根据 schema 结构化展示

## 6. 单元测试用例

### 6.1 测试文件：tests/test_advanced_ui.cpp

```cpp
#include <gtest/gtest.h>
#include "stdiolink/host/form_generator.h"  // 扩展 UiGenerator 支持嵌套/数组/条件显示
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class AdvancedUiTest : public ::testing::Test {};

// 测试嵌套对象表单生成
TEST_F(AdvancedUiTest, NestedObjectForm) {
    FieldMeta outer;
    outer.name = "config";
    outer.type = FieldType::Object;

    FieldMeta inner;
    inner.name = "timeout";
    inner.type = FieldType::Int;
    outer.fields.append(inner);

    FormDesc form = UiGenerator::generateCommandForm(CommandMeta{});
    // 伪代码：期望 UiGenerator 能递归输出嵌套字段 widgets
}

// 测试数组表单生成
TEST_F(AdvancedUiTest, ArrayForm) {
    FieldMeta field;
    field.name = "tags";
    field.type = FieldType::Array;
    field.items = std::make_shared<FieldMeta>();
    field.items->type = FieldType::String;

    FormDesc form = UiGenerator::generateCommandForm(CommandMeta{});
    // 伪代码：期望 array 字段生成可增删 UI 组件
}
```

### 6.2 条件显示测试

```cpp
// 测试 visibleIf 解析
TEST_F(AdvancedUiTest, VisibleIfParsing) {
    FieldMeta field;
    field.name = "advanced_option";
    field.ui.visibleIf = "mode == 'advanced'";

    // 伪代码：期望 widget 中包含 visibleIf 字段
}

// 测试条件求值
TEST_F(AdvancedUiTest, VisibleIfEvaluation) {
    QString condition = "mode == 'advanced'";
    QJsonObject context{{"mode", "advanced"}};

    EXPECT_TRUE(ConditionEvaluator::evaluate(condition, context));

    context["mode"] = "simple";
    EXPECT_FALSE(ConditionEvaluator::evaluate(condition, context));
}
```

### 6.3 分组排序测试

```cpp
// 测试字段分组
TEST_F(AdvancedUiTest, FieldGrouping) {
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

// 测试字段排序
TEST_F(AdvancedUiTest, FieldOrdering) {
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
```

## 7. 依赖关系

- **前置**: M15 (强类型事件流)
- **后续**: M17 (配置注入闭环)
