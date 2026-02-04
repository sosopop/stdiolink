# 里程碑 9：Builder API

> **前置条件**: 里程碑 7 已完成
> **目标**: 提供流式 API 简化元数据定义

---

## 1. 目标

- 实现 `FieldBuilder` 用于构建字段元数据
- 实现 `CommandBuilder` 用于构建命令元数据
- 实现 `DriverMetaBuilder` 用于构建完整驱动元数据
- 提供链式调用风格，简化开发者使用

---

## 2. 技术要点

### 2.1 Builder 模式优势

- 避免手动构造复杂 JSON
- 编译期类型检查
- 链式调用，代码可读性高
- 自动处理默认值和可选字段

### 2.2 使用示例

```cpp
auto meta = DriverMetaBuilder()
    .info("com.example.scan", "Scan Driver", "1.0.0", "3D扫描驱动")
    .capability("keepalive")
    .capability("streaming")
    .configField(FieldBuilder("deviceId", FieldType::String)
        .required()
        .description("设备ID"))
    .configField(FieldBuilder("timeoutMs", FieldType::Int)
        .defaultValue(5000)
        .range(100, 60000)
        .widget("slider"))
    .command(CommandBuilder("scan")
        .description("执行扫描")
        .param(FieldBuilder("mode", FieldType::Enum)
            .required()
            .enumValues({"frame", "continuous"}))
        .param(FieldBuilder("fps", FieldType::Int)
            .defaultValue(10)
            .range(1, 60))
        .returns(FieldType::Object, "扫描结果")
        .returnField("count", FieldType::Int, "点云数量"))
    .build();
```

---

## 3. 实现步骤

### 3.1 创建 driver/meta_builder.h

```cpp
#pragma once

#include "stdiolink/protocol/meta_types.h"

namespace stdiolink::meta {

/**
 * 字段构建器
 */
class FieldBuilder {
public:
    explicit FieldBuilder(const QString& name, FieldType type);

    FieldBuilder& required(bool req = true);
    FieldBuilder& defaultValue(const QJsonValue& val);
    FieldBuilder& description(const QString& desc);

    // 数值约束
    FieldBuilder& range(double minVal, double maxVal);
    FieldBuilder& min(double val);
    FieldBuilder& max(double val);

    // 字符串约束
    FieldBuilder& minLength(int len);
    FieldBuilder& maxLength(int len);
    FieldBuilder& pattern(const QString& regex);

    // 枚举值
    FieldBuilder& enumValues(const QJsonArray& values);
    FieldBuilder& enumValues(const QStringList& values);

    // 格式提示
    FieldBuilder& format(const QString& fmt);

    // UI 提示
    FieldBuilder& widget(const QString& w);
    FieldBuilder& group(const QString& g);
    FieldBuilder& order(int o);
    FieldBuilder& placeholder(const QString& p);
    FieldBuilder& unit(const QString& u);
    FieldBuilder& advanced(bool adv = true);
    FieldBuilder& readonly(bool ro = true);

    // Object 类型嵌套字段
    FieldBuilder& addField(const FieldBuilder& field);
    FieldBuilder& requiredKeys(const QStringList& keys);
    FieldBuilder& additionalProperties(bool allowed);

    // Array 类型元素
    FieldBuilder& items(const FieldBuilder& item);
    FieldBuilder& minItems(int n);
    FieldBuilder& maxItems(int n);

    FieldMeta build() const;

private:
    FieldMeta m_field;
};

} // namespace stdiolink::meta
```

### 3.2 CommandBuilder 定义

```cpp
namespace stdiolink::meta {

/**
 * 命令构建器
 */
class CommandBuilder {
public:
    explicit CommandBuilder(const QString& name);

    CommandBuilder& description(const QString& desc);
    CommandBuilder& title(const QString& t);
    CommandBuilder& summary(const QString& s);

    // 参数
    CommandBuilder& param(const FieldBuilder& field);

    // 返回值
    CommandBuilder& returns(FieldType type, const QString& desc = {});
    CommandBuilder& returnField(const QString& name, FieldType type,
                                const QString& desc = {});

    // 事件
    CommandBuilder& event(const QString& name, const QString& desc = {});
    CommandBuilder& eventField(const QString& eventName,
                               const QString& fieldName,
                               FieldType type,
                               const QString& desc = {});

    // 错误码
    CommandBuilder& error(int code, const QString& name,
                          const QString& message);

    // 示例
    CommandBuilder& example(const QString& title,
                            const QJsonObject& data);

    // UI
    CommandBuilder& group(const QString& g);
    CommandBuilder& order(int o);

    CommandMeta build() const;

private:
    CommandMeta m_cmd;
};

} // namespace stdiolink::meta
```

### 3.3 DriverMetaBuilder 定义

```cpp
namespace stdiolink::meta {

/**
 * 驱动元数据构建器
 */
class DriverMetaBuilder {
public:
    DriverMetaBuilder& schemaVersion(const QString& ver);

    DriverMetaBuilder& info(const QString& id,
                            const QString& name,
                            const QString& version,
                            const QString& desc = {});
    DriverMetaBuilder& vendor(const QString& v);
    DriverMetaBuilder& entry(const QString& program,
                             const QStringList& defaultArgs = {});

    DriverMetaBuilder& capability(const QString& cap);
    DriverMetaBuilder& profile(const QString& prof);

    DriverMetaBuilder& configField(const FieldBuilder& field);
    DriverMetaBuilder& configApply(const QString& method,
                                   const QString& param = {});

    DriverMetaBuilder& command(const CommandBuilder& cmd);

    DriverMetaBuilder& defineType(const QString& name,
                                  const FieldBuilder& type);

    DriverMeta build() const;

private:
    DriverMeta m_meta;
};

} // namespace stdiolink::meta
```

---

## 4. 验收标准

1. 使用 Builder 构建的元数据与手写 JSON 等价
2. 链式调用语法正确，编译无警告
3. 所有约束条件正确设置
4. 嵌套结构（Object/Array）正确构建
5. 缺省值正确处理

---

## 5. 单元测试用例

### 5.1 测试文件：tests/meta_builder_test.cpp

```cpp
#include <gtest/gtest.h>
#include "stdiolink/driver/meta_builder.h"

using namespace stdiolink::meta;

class MetaBuilderTest : public ::testing::Test {};

// 测试 FieldBuilder 基本功能
TEST_F(MetaBuilderTest, FieldBuilderBasic) {
    auto field = FieldBuilder("name", FieldType::String)
        .required()
        .description("用户名")
        .minLength(1)
        .maxLength(32)
        .build();

    EXPECT_EQ(field.name, "name");
    EXPECT_EQ(field.type, FieldType::String);
    EXPECT_TRUE(field.required);
    EXPECT_EQ(field.description, "用户名");
    EXPECT_EQ(field.constraints.minLength, 1);
    EXPECT_EQ(field.constraints.maxLength, 32);
}

// 测试数值范围约束
TEST_F(MetaBuilderTest, FieldBuilderRange) {
    auto field = FieldBuilder("timeout", FieldType::Int)
        .defaultValue(5000)
        .range(100, 60000)
        .build();

    EXPECT_EQ(field.defaultValue.toInt(), 5000);
    EXPECT_EQ(field.constraints.min, 100);
    EXPECT_EQ(field.constraints.max, 60000);
}

// 测试枚举值
TEST_F(MetaBuilderTest, FieldBuilderEnum) {
    auto field = FieldBuilder("mode", FieldType::Enum)
        .enumValues({"fast", "normal", "slow"})
        .defaultValue("normal")
        .build();

    EXPECT_EQ(field.type, FieldType::Enum);
    EXPECT_EQ(field.constraints.enumValues.size(), 3);
    EXPECT_EQ(field.defaultValue.toString(), "normal");
}

// 测试 UI 提示
TEST_F(MetaBuilderTest, FieldBuilderUIHint) {
    auto field = FieldBuilder("volume", FieldType::Int)
        .widget("slider")
        .group("音频")
        .unit("dB")
        .range(0, 100)
        .build();

    EXPECT_EQ(field.ui.widget, "slider");
    EXPECT_EQ(field.ui.group, "音频");
    EXPECT_EQ(field.ui.unit, "dB");
}

// 测试嵌套 Object
TEST_F(MetaBuilderTest, FieldBuilderNestedObject) {
    auto field = FieldBuilder("point", FieldType::Object)
        .addField(FieldBuilder("x", FieldType::Int).required())
        .addField(FieldBuilder("y", FieldType::Int).required())
        .addField(FieldBuilder("z", FieldType::Int).defaultValue(0))
        .requiredKeys({"x", "y"})
        .build();

    EXPECT_EQ(field.fields.size(), 3);
    EXPECT_EQ(field.requiredKeys.size(), 2);
}

// 测试 Array items
TEST_F(MetaBuilderTest, FieldBuilderArray) {
    auto field = FieldBuilder("tags", FieldType::Array)
        .items(FieldBuilder("", FieldType::String).maxLength(20))
        .minItems(1)
        .maxItems(10)
        .build();

    EXPECT_NE(field.items, nullptr);
    EXPECT_EQ(field.items->type, FieldType::String);
    EXPECT_EQ(field.constraints.minItems, 1);
    EXPECT_EQ(field.constraints.maxItems, 10);
}

// 测试 CommandBuilder
TEST_F(MetaBuilderTest, CommandBuilderBasic) {
    auto cmd = CommandBuilder("scan")
        .description("执行扫描")
        .param(FieldBuilder("mode", FieldType::Enum)
            .required()
            .enumValues({"frame", "continuous"}))
        .param(FieldBuilder("fps", FieldType::Int)
            .defaultValue(10))
        .returns(FieldType::Object, "扫描结果")
        .returnField("count", FieldType::Int, "点云数量")
        .build();

    EXPECT_EQ(cmd.name, "scan");
    EXPECT_EQ(cmd.params.size(), 2);
    EXPECT_EQ(cmd.returns.type, FieldType::Object);
    EXPECT_EQ(cmd.returns.fields.size(), 1);
}

// 测试 DriverMetaBuilder
TEST_F(MetaBuilderTest, DriverMetaBuilderComplete) {
    auto meta = DriverMetaBuilder()
        .info("com.test.driver", "Test Driver", "1.0.0")
        .vendor("TestCorp")
        .capability("keepalive")
        .configField(FieldBuilder("debug", FieldType::Bool)
            .defaultValue(false))
        .command(CommandBuilder("echo")
            .description("回显")
            .param(FieldBuilder("msg", FieldType::String).required()))
        .build();

    EXPECT_EQ(meta.info.id, "com.test.driver");
    EXPECT_EQ(meta.info.vendor, "TestCorp");
    EXPECT_EQ(meta.info.capabilities.size(), 1);
    EXPECT_EQ(meta.config.fields.size(), 1);
    EXPECT_EQ(meta.commands.size(), 1);
}

// 测试 JSON 输出等价性
TEST_F(MetaBuilderTest, JsonEquivalence) {
    auto meta = DriverMetaBuilder()
        .info("test.id", "Test", "1.0.0")
        .command(CommandBuilder("cmd")
            .param(FieldBuilder("p", FieldType::Int).required()))
        .build();

    QJsonObject json = meta.toJson();
    DriverMeta restored = DriverMeta::fromJson(json);

    EXPECT_EQ(restored.info.id, meta.info.id);
    EXPECT_EQ(restored.commands.size(), meta.commands.size());
}
```

---

## 6. 依赖关系

- **前置依赖**: 里程碑 7（元数据类型定义）
- **后续依赖**: 无直接依赖，但为开发者提供便利

---

## 7. 文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/stdiolink/driver/meta_builder.h` | 新增 | Builder API 定义 |
| `src/stdiolink/driver/meta_builder.cpp` | 新增 | Builder 实现 |
| `src/tests/meta_builder_test.cpp` | 新增 | 单元测试 |
