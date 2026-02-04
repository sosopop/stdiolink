# 里程碑 7：元数据类型与序列化

> **前置条件**: 里程碑 1-6 已完成
> **目标**: 完成元数据类型系统和 JSON 序列化

---

## 1. 目标

实现 stdiolink 元数据自描述系统的基础类型定义，包括：

- 定义字段类型枚举 `FieldType`
- 定义元数据核心结构体（`FieldMeta`、`CommandMeta`、`DriverMeta` 等）
- 实现所有结构体的 JSON 序列化与反序列化
- 为后续里程碑提供类型基础

---

## 2. 技术要点

### 2.1 字段类型枚举

```cpp
namespace stdiolink::meta {

enum class FieldType {
    String,     // 字符串
    Int,        // 32位整数
    Int64,      // 64位整数
    Double,     // 浮点数
    Bool,       // 布尔值
    Object,     // 嵌套对象
    Array,      // 数组
    Enum,       // 枚举（字符串限定值）
    Any         // 任意类型
};

} // namespace stdiolink::meta
```

### 2.2 类型与 JSON 映射

| FieldType | JSON 类型 | C++ 类型 | 说明 |
|-----------|-----------|----------|------|
| String | string | QString | 普通字符串 |
| Int | number | int | 32位有符号整数 |
| Int64 | number | qint64 | 64位有符号整数 |
| Double | number | double | 双精度浮点 |
| Bool | boolean | bool | 布尔值 |
| Object | object | QJsonObject | 嵌套对象 |
| Array | array | QJsonArray | 数组 |
| Enum | string | QString | 限定值字符串 |
| Any | any | QJsonValue | 任意类型 |

### 2.3 核心结构体层次

```
DriverMeta (驱动元数据)
├── schemaVersion: string
├── info: DriverInfo
│   ├── id, name, version, description, vendor
│   ├── entry: { program, defaultArgs }
│   ├── capabilities: string[]
│   └── profiles: string[]
├── types: QHash<QString, FieldMeta>  // 可复用类型定义
├── config: ConfigSchema
│   ├── fields[]: FieldMeta
│   └── apply: ConfigApply
├── commands[]: CommandMeta
│   ├── name, description, title, summary
│   ├── params[]: FieldMeta
│   ├── returns: ReturnMeta
│   └── events[]: EventMeta
└── errors[]/examples[]
```

### 2.4 约束条件设计

| 约束名 | 适用类型 | JSON 键 |
|--------|----------|---------|
| required | 所有 | `"required": true` |
| default | 所有 | `"default": ...` |
| min/max | Int/Int64/Double | `"min": 0, "max": 100` |
| minLength/maxLength | String | `"minLength": 1` |
| pattern | String | `"pattern": "^[a-z]+$"` |
| enum | Enum | `"enum": ["a", "b"]` |
| minItems/maxItems | Array | `"minItems": 1` |
| items | Array | `"items": { ... }` |
| requiredKeys | Object | `"requiredKeys": ["x"]` |
| additionalProperties | Object | `"additionalProperties": false` |

---

## 3. 实现步骤

### 3.1 创建 protocol/meta_types.h

```cpp
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>
#include <QHash>
#include <optional>
#include <memory>

namespace stdiolink::meta {

// 字段类型枚举
enum class FieldType {
    String, Int, Int64, Double, Bool, Object, Array, Enum, Any
};

QString fieldTypeToString(FieldType type);
FieldType fieldTypeFromString(const QString& str);

// UI 渲染提示
struct UIHint {
    QString widget;
    QString group;
    int order = 0;
    QString placeholder;
    bool advanced = false;
    bool readonly = false;
    QString visibleIf;
    QString unit;
    double step = 0;

    QJsonObject toJson() const;
    static UIHint fromJson(const QJsonObject& obj);
    bool isEmpty() const;
};

// 字段约束条件
struct Constraints {
    std::optional<double> min;
    std::optional<double> max;
    std::optional<int> minLength;
    std::optional<int> maxLength;
    QString pattern;
    QJsonArray enumValues;
    QString format;
    std::optional<int> minItems;
    std::optional<int> maxItems;

    QJsonObject toJson() const;
    static Constraints fromJson(const QJsonObject& obj);
    bool isEmpty() const;
};

// 字段元数据（前向声明用于递归）
struct FieldMeta {
    QString name;
    FieldType type = FieldType::Any;
    bool required = false;
    QJsonValue defaultValue;
    QString description;
    Constraints constraints;
    UIHint ui;
    QVector<FieldMeta> fields;           // Object 嵌套字段
    std::shared_ptr<FieldMeta> items;    // Array 元素 schema
    QStringList requiredKeys;            // Object 必填键
    bool additionalProperties = true;

    QJsonObject toJson() const;
    static FieldMeta fromJson(const QJsonObject& obj);
};

// 事件元数据
struct EventMeta {
    QString name;
    QString description;
    QVector<FieldMeta> fields;

    QJsonObject toJson() const;
    static EventMeta fromJson(const QJsonObject& obj);
};

// 返回值元数据
struct ReturnMeta {
    FieldType type = FieldType::Object;
    QString description;
    QVector<FieldMeta> fields;

    QJsonObject toJson() const;
    static ReturnMeta fromJson(const QJsonObject& obj);
};

// 命令元数据
struct CommandMeta {
    QString name;
    QString description;
    QString title;
    QString summary;
    QVector<FieldMeta> params;
    ReturnMeta returns;
    QVector<EventMeta> events;
    QVector<QJsonObject> errors;
    QVector<QJsonObject> examples;
    UIHint ui;

    QJsonObject toJson() const;
    static CommandMeta fromJson(const QJsonObject& obj);
};

// 配置注入方式
struct ConfigApply {
    QString method;      // startupArgs|env|command|file
    QString envPrefix;
    QString command;
    QString fileName;

    QJsonObject toJson() const;
    static ConfigApply fromJson(const QJsonObject& obj);
};

// 配置模式
struct ConfigSchema {
    QVector<FieldMeta> fields;
    ConfigApply apply;

    QJsonObject toJson() const;
    static ConfigSchema fromJson(const QJsonObject& obj);
};

// 驱动基本信息
struct DriverInfo {
    QString id;
    QString name;
    QString version;
    QString description;
    QString vendor;
    QJsonObject entry;
    QStringList capabilities;
    QStringList profiles;

    QJsonObject toJson() const;
    static DriverInfo fromJson(const QJsonObject& obj);
};

// 驱动元数据（顶层结构）
struct DriverMeta {
    QString schemaVersion = "1.0";
    DriverInfo info;
    ConfigSchema config;
    QVector<CommandMeta> commands;
    QHash<QString, FieldMeta> types;
    QVector<QJsonObject> errors;
    QVector<QJsonObject> examples;

    QJsonObject toJson() const;
    static DriverMeta fromJson(const QJsonObject& obj);

    const CommandMeta* findCommand(const QString& name) const;
};

} // namespace stdiolink::meta
```

### 3.2 创建 protocol/meta_types.cpp

实现所有结构体的 `toJson()` 和 `fromJson()` 方法。

```cpp
#include "meta_types.h"

namespace stdiolink::meta {

QString fieldTypeToString(FieldType type) {
    switch (type) {
    case FieldType::String: return "string";
    case FieldType::Int:    return "int";
    case FieldType::Int64:  return "int64";
    case FieldType::Double: return "double";
    case FieldType::Bool:   return "bool";
    case FieldType::Object: return "object";
    case FieldType::Array:  return "array";
    case FieldType::Enum:   return "enum";
    case FieldType::Any:    return "any";
    }
    return "any";
}

FieldType fieldTypeFromString(const QString& str) {
    if (str == "string") return FieldType::String;
    if (str == "int")    return FieldType::Int;
    if (str == "int64")  return FieldType::Int64;
    if (str == "double" || str == "number") return FieldType::Double;
    if (str == "bool" || str == "boolean")  return FieldType::Bool;
    if (str == "object") return FieldType::Object;
    if (str == "array")  return FieldType::Array;
    if (str == "enum")   return FieldType::Enum;
    return FieldType::Any;
}

// UIHint 实现
QJsonObject UIHint::toJson() const {
    QJsonObject obj;
    if (!widget.isEmpty()) obj["widget"] = widget;
    if (!group.isEmpty())  obj["group"] = group;
    if (order != 0)        obj["order"] = order;
    if (!placeholder.isEmpty()) obj["placeholder"] = placeholder;
    if (advanced)          obj["advanced"] = true;
    if (readonly)          obj["readonly"] = true;
    if (!visibleIf.isEmpty()) obj["visibleIf"] = visibleIf;
    if (!unit.isEmpty())   obj["unit"] = unit;
    if (step != 0)         obj["step"] = step;
    return obj;
}

UIHint UIHint::fromJson(const QJsonObject& obj) {
    UIHint hint;
    hint.widget = obj["widget"].toString();
    hint.group = obj["group"].toString();
    hint.order = obj["order"].toInt();
    hint.placeholder = obj["placeholder"].toString();
    hint.advanced = obj["advanced"].toBool();
    hint.readonly = obj["readonly"].toBool();
    hint.visibleIf = obj["visibleIf"].toString();
    hint.unit = obj["unit"].toString();
    hint.step = obj["step"].toDouble();
    return hint;
}

bool UIHint::isEmpty() const {
    return widget.isEmpty() && group.isEmpty() && order == 0;
}

// ... 其他结构体实现类似

} // namespace stdiolink::meta
```

### 3.3 更新 CMakeLists.txt

在 `src/stdiolink/CMakeLists.txt` 中添加新文件：

```cmake
set(STDIOLINK_SOURCES
    # ... 现有文件
    protocol/meta_types.cpp
)

set(STDIOLINK_HEADERS
    # ... 现有文件
    protocol/meta_types.h
)
```

---

## 4. 验收标准

1. `FieldType` 枚举与字符串可双向转换
2. 所有结构体可正确序列化为 JSON
3. JSON 可正确反序列化为结构体
4. 嵌套结构（Object 的 fields、Array 的 items）正确处理
5. 可选字段缺失时使用默认值
6. 编译无警告，clang-tidy 检查通过

---

## 5. 单元测试用例

### 5.1 测试文件：tests/meta_types_test.cpp

```cpp
#include <gtest/gtest.h>
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink::meta;

class MetaTypesTest : public ::testing::Test {};

// 测试 FieldType 转换
TEST_F(MetaTypesTest, FieldTypeConversion) {
    EXPECT_EQ(fieldTypeToString(FieldType::String), "string");
    EXPECT_EQ(fieldTypeToString(FieldType::Int), "int");
    EXPECT_EQ(fieldTypeToString(FieldType::Int64), "int64");
    EXPECT_EQ(fieldTypeToString(FieldType::Double), "double");
    EXPECT_EQ(fieldTypeToString(FieldType::Bool), "bool");
    EXPECT_EQ(fieldTypeToString(FieldType::Object), "object");
    EXPECT_EQ(fieldTypeToString(FieldType::Array), "array");
    EXPECT_EQ(fieldTypeToString(FieldType::Enum), "enum");
    EXPECT_EQ(fieldTypeToString(FieldType::Any), "any");

    EXPECT_EQ(fieldTypeFromString("string"), FieldType::String);
    EXPECT_EQ(fieldTypeFromString("number"), FieldType::Double);
    EXPECT_EQ(fieldTypeFromString("boolean"), FieldType::Bool);
    EXPECT_EQ(fieldTypeFromString("unknown"), FieldType::Any);
}

// 测试 UIHint 序列化
TEST_F(MetaTypesTest, UIHintSerialization) {
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
}

// 测试 FieldMeta 序列化
TEST_F(MetaTypesTest, FieldMetaSerialization) {
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

    FieldMeta restored = FieldMeta::fromJson(json);
    EXPECT_EQ(restored.name, field.name);
    EXPECT_EQ(restored.type, field.type);
    EXPECT_EQ(restored.defaultValue.toInt(), 5000);
}

// 测试嵌套 Object 字段
TEST_F(MetaTypesTest, NestedObjectField) {
    FieldMeta roi;
    roi.name = "roi";
    roi.type = FieldType::Object;
    roi.fields = {
        {"x", FieldType::Int, true, {}, "X坐标", {}, {}, {}, nullptr, {}, true},
        {"y", FieldType::Int, true, {}, "Y坐标", {}, {}, {}, nullptr, {}, true}
    };

    QJsonObject json = roi.toJson();
    EXPECT_TRUE(json.contains("fields"));
    EXPECT_EQ(json["fields"].toArray().size(), 2);

    FieldMeta restored = FieldMeta::fromJson(json);
    EXPECT_EQ(restored.fields.size(), 2);
    EXPECT_EQ(restored.fields[0].name, "x");
}

// 测试 Array 类型的 items
TEST_F(MetaTypesTest, ArrayItemsField) {
    FieldMeta tags;
    tags.name = "tags";
    tags.type = FieldType::Array;
    tags.items = std::make_shared<FieldMeta>();
    tags.items->type = FieldType::String;

    QJsonObject json = tags.toJson();
    EXPECT_TRUE(json.contains("items"));

    FieldMeta restored = FieldMeta::fromJson(json);
    EXPECT_NE(restored.items, nullptr);
    EXPECT_EQ(restored.items->type, FieldType::String);
}

// 测试 CommandMeta 序列化
TEST_F(MetaTypesTest, CommandMetaSerialization) {
    CommandMeta cmd;
    cmd.name = "scan";
    cmd.description = "执行扫描";
    cmd.params = {
        {"mode", FieldType::Enum, true, {}, "扫描模式", {}, {}, {}, nullptr, {}, true}
    };
    cmd.params[0].constraints.enumValues = QJsonArray{"frame", "continuous"};

    QJsonObject json = cmd.toJson();
    EXPECT_EQ(json["name"].toString(), "scan");
    EXPECT_EQ(json["params"].toArray().size(), 1);

    CommandMeta restored = CommandMeta::fromJson(json);
    EXPECT_EQ(restored.name, "scan");
    EXPECT_EQ(restored.params.size(), 1);
}

// 测试 DriverMeta 完整序列化
TEST_F(MetaTypesTest, DriverMetaSerialization) {
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
    EXPECT_TRUE(json.contains("info") || json.contains("driver"));
    EXPECT_EQ(json["commands"].toArray().size(), 1);

    DriverMeta restored = DriverMeta::fromJson(json);
    EXPECT_EQ(restored.info.id, "com.example.test");
    EXPECT_EQ(restored.commands.size(), 1);
}

// 测试 findCommand
TEST_F(MetaTypesTest, FindCommand) {
    DriverMeta meta;
    CommandMeta cmd1, cmd2;
    cmd1.name = "scan";
    cmd2.name = "stop";
    meta.commands = {cmd1, cmd2};

    EXPECT_NE(meta.findCommand("scan"), nullptr);
    EXPECT_EQ(meta.findCommand("scan")->name, "scan");
    EXPECT_EQ(meta.findCommand("unknown"), nullptr);
}
```

---

## 6. 依赖关系

- **前置依赖**: 里程碑 1-6（基础框架已完成）
- **后续依赖**:
  - 里程碑 8（meta.describe）依赖本里程碑的类型定义
  - 里程碑 9（Builder API）依赖本里程碑的结构体
  - 里程碑 10（参数验证）依赖本里程碑的类型系统
  - 里程碑 11（Host 侧）依赖本里程碑的序列化能力

---

## 7. 文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/stdiolink/protocol/meta_types.h` | 新增 | 元数据类型定义 |
| `src/stdiolink/protocol/meta_types.cpp` | 新增 | 元数据类型实现 |
| `src/stdiolink/CMakeLists.txt` | 修改 | 添加新文件 |
| `src/tests/meta_types_test.cpp` | 新增 | 单元测试 |
| `src/tests/CMakeLists.txt` | 修改 | 添加测试文件 |
