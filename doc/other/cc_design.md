# stdiolink 元数据自描述系统设计文档

## 1. 概述

### 1.1 背景

当前 stdiolink 框架中，Driver 的命令接口是隐式定义的，Host 端需要预先知道 Driver 支持哪些命令、每个命令的参数格式、返回值类型等信息。这种方式存在以下问题：

1. **缺乏自描述能力**：Host 无法在运行时发现 Driver 的能力
2. **文档与代码分离**：接口文档需要手动维护，容易与实际实现不一致
3. **无法自动生成 UI**：配置界面需要针对每个 Driver 单独开发
4. **类型安全性差**：参数类型检查只能在运行时进行

### 1.2 目标

设计并实现一套元数据自描述系统，使 Driver 具备以下能力：

1. **自描述**：Driver 可以声明自己支持的命令、参数类型、配置项
2. **自文档**：自动生成 API 文档，保证文档与代码一致
3. **UI 生成**：Host 可根据元数据自动生成配置界面和调用界面
4. **类型安全**：编译期和运行时双重类型检查
5. **开发体验**：先定义接口模板，再实现代码，接口即文档

### 1.3 设计原则

- **声明式优先**：使用声明式语法定义接口，减少样板代码
- **类型安全**：利用 C++ 模板和 Qt 元对象系统实现类型安全
- **向后兼容**：新系统与现有 API 兼容，支持渐进式迁移
- **最小侵入**：对现有代码改动最小化

---

## 2. 需求分析

### 2.1 功能需求

#### 2.1.1 Driver 端需求

| 编号 | 需求 | 优先级 | 描述 |
|------|------|--------|------|
| DR-001 | 命令声明 | P0 | 支持声明式定义命令，包括名称、描述、参数、返回值 |
| DR-002 | 参数类型定义 | P0 | 支持基础类型、复合类型、枚举类型、数组类型 |
| DR-003 | 参数约束 | P1 | 支持参数验证规则：必填、范围、正则、枚举值 |
| DR-004 | 配置项声明 | P1 | 支持声明 Driver 级别的配置项 |
| DR-005 | 元数据导出 | P0 | 支持将元数据序列化为 JSON 格式导出 |
| DR-006 | 版本信息 | P1 | 支持声明 Driver 版本、协议版本 |
| DR-007 | 依赖声明 | P2 | 支持声明 Driver 的外部依赖 |

#### 2.1.2 Host 端需求

| 编号 | 需求 | 优先级 | 描述 |
|------|------|--------|------|
| HR-001 | 元数据查询 | P0 | 支持查询 Driver 的元数据信息 |
| HR-002 | 命令发现 | P0 | 支持发现 Driver 支持的所有命令 |
| HR-003 | 参数验证 | P1 | 根据元数据在发送前验证参数 |
| HR-004 | UI 生成 | P1 | 根据元数据自动生成配置/调用界面 |
| HR-005 | 文档生成 | P2 | 根据元数据自动生成 API 文档 |
| HR-006 | 代码生成 | P2 | 根据元数据生成类型安全的调用代码 |

#### 2.1.3 协议需求

| 编号 | 需求 | 优先级 | 描述 |
|------|------|--------|------|
| PR-001 | 内置命令 | P0 | 定义 `__meta__` 内置命令用于获取元数据 |
| PR-002 | 元数据格式 | P0 | 定义标准的元数据 JSON Schema |
| PR-003 | 版本协商 | P1 | 支持 Host 和 Driver 之间的版本协商 |

### 2.2 非功能需求

| 编号 | 需求 | 描述 |
|------|------|------|
| NR-001 | 性能 | 元数据查询响应时间 < 100ms |
| NR-002 | 内存 | 元数据内存占用 < 1MB |
| NR-003 | 兼容性 | 支持 Qt 5.12+ |
| NR-004 | 可扩展性 | 支持自定义类型和验证器 |

---

## 3. 系统架构

### 3.1 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                          Host 端                                 │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │ MetaClient  │  │ UIGenerator │  │ DocGenerator            │  │
│  │             │  │             │  │                         │  │
│  │ - 查询元数据 │  │ - 生成配置UI│  │ - 生成 Markdown        │  │
│  │ - 缓存管理  │  │ - 生成调用UI│  │ - 生成 HTML            │  │
│  │ - 版本协商  │  │ - 参数绑定  │  │ - 生成 OpenAPI         │  │
│  └──────┬──────┘  └──────┬──────┘  └───────────┬─────────────┘  │
│         │                │                      │                │
│         └────────────────┼──────────────────────┘                │
│                          │                                       │
│                   ┌──────▼──────┐                                │
│                   │ MetaSchema  │                                │
│                   │             │                                │
│                   │ - 类型定义  │                                │
│                   │ - 验证规则  │                                │
│                   │ - 序列化   │                                │
│                   └──────┬──────┘                                │
└──────────────────────────┼──────────────────────────────────────┘
                           │ JSONL Protocol
                           │ __meta__ command
┌──────────────────────────┼──────────────────────────────────────┐
│                          │                                       │
│                   ┌──────▼──────┐                                │
│                   │ MetaSchema  │                                │
│                   │ (共享定义)  │                                │
│                   └──────┬──────┘                                │
│                          │                                       │
│  ┌───────────────────────┼───────────────────────────────────┐  │
│  │                       │                                    │  │
│  │  ┌─────────────┐  ┌───▼───────┐  ┌─────────────────────┐  │  │
│  │  │ MetaBuilder │  │ MetaStore │  │ CommandRegistry     │  │  │
│  │  │             │  │           │  │                     │  │  │
│  │  │ - 声明命令  │  │ - 存储    │  │ - 命令注册          │  │  │
│  │  │ - 声明参数  │  │ - 查询    │  │ - 命令分发          │  │  │
│  │  │ - 声明配置  │  │ - 导出    │  │ - 参数验证          │  │  │
│  │  └─────────────┘  └───────────┘  └─────────────────────┘  │  │
│  │                                                            │  │
│  │                    Driver Core                             │  │
│  └────────────────────────────────────────────────────────────┘  │
│                          Driver 端                               │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 模块划分

| 模块 | 位置 | 职责 |
|------|------|------|
| `meta/schema` | 共享 | 元数据类型定义、JSON Schema |
| `meta/builder` | Driver | 声明式构建元数据 |
| `meta/store` | Driver | 存储和导出元数据 |
| `meta/validator` | 共享 | 参数验证器 |
| `meta/client` | Host | 查询和缓存元数据 |
| `meta/ui` | Host | UI 生成器 |
| `meta/doc` | Host | 文档生成器 |

---

## 4. 详细设计

### 4.1 元数据 Schema 定义

#### 4.1.1 类型系统

支持以下基础类型和复合类型：

```
基础类型 (Primitive Types)
├── string      - 字符串
├── int         - 32位整数
├── int64       - 64位整数
├── double      - 双精度浮点数
├── bool        - 布尔值
├── null        - 空值
└── any         - 任意类型（不推荐）

复合类型 (Composite Types)
├── object      - 对象（键值对）
├── array<T>    - 数组（元素类型为 T）
├── enum        - 枚举（预定义值集合）
├── union<T...> - 联合类型（多选一）
└── optional<T> - 可选类型（可为 null）
```

#### 4.1.2 元数据 JSON Schema

**DriverMeta - Driver 元数据根结构**

```json
{
  "$schema": "https://stdiolink.dev/schema/driver-meta/v1.json",
  "name": "string",
  "version": "string",
  "description": "string",
  "author": "string?",
  "license": "string?",
  "protocol_version": "string",
  "commands": ["CommandMeta"],
  "config": "ConfigMeta?",
  "types": ["TypeDef"]
}
```

**CommandMeta - 命令元数据**

```json
{
  "name": "string",
  "description": "string",
  "category": "string?",
  "deprecated": "bool?",
  "deprecated_message": "string?",
  "params": ["ParamMeta"],
  "returns": "ReturnMeta",
  "errors": ["ErrorMeta"],
  "examples": ["ExampleMeta"]
}
```

**ParamMeta - 参数元数据**

```json
{
  "name": "string",
  "type": "TypeRef",
  "description": "string",
  "required": "bool",
  "default": "any?",
  "constraints": "Constraints?"
}
```

**TypeRef - 类型引用**

```json
{
  "kind": "primitive | array | object | enum | ref",
  "primitive": "string?",
  "element_type": "TypeRef?",
  "properties": ["ParamMeta"]?,
  "enum_values": ["EnumValue"]?,
  "ref_name": "string?"
}
```

**Constraints - 约束条件**

```json
{
  "min": "number?",
  "max": "number?",
  "min_length": "int?",
  "max_length": "int?",
  "pattern": "string?",
  "enum": ["any"]?,
  "custom": "string?"
}
```

**ConfigMeta - 配置元数据**

```json
{
  "groups": ["ConfigGroup"]
}
```

**ConfigGroup - 配置分组**

```json
{
  "name": "string",
  "title": "string",
  "description": "string?",
  "items": ["ConfigItem"]
}
```

**ConfigItem - 配置项**

```json
{
  "key": "string",
  "type": "TypeRef",
  "title": "string",
  "description": "string?",
  "default": "any?",
  "required": "bool",
  "constraints": "Constraints?",
  "ui_hints": "UIHints?"
}
```

**UIHints - UI 提示**

```json
{
  "widget": "string?",
  "placeholder": "string?",
  "help_text": "string?",
  "order": "int?",
  "hidden": "bool?",
  "readonly": "bool?",
  "depends_on": "string?"
}
```

#### 4.1.3 完整元数据示例

以下是一个图像处理 Driver 的完整元数据示例：

```json
{
  "$schema": "https://stdiolink.dev/schema/driver-meta/v1.json",
  "name": "image-processor",
  "version": "1.2.0",
  "description": "图像处理 Driver，支持缩放、裁剪、滤镜等操作",
  "author": "stdiolink team",
  "license": "MIT",
  "protocol_version": "1.0",

  "types": [
    {
      "name": "ImageFormat",
      "kind": "enum",
      "description": "支持的图像格式",
      "values": [
        {"value": "png", "description": "PNG 格式"},
        {"value": "jpg", "description": "JPEG 格式"},
        {"value": "webp", "description": "WebP 格式"}
      ]
    },
    {
      "name": "Rectangle",
      "kind": "object",
      "description": "矩形区域",
      "properties": [
        {"name": "x", "type": {"kind": "primitive", "primitive": "int"}, "description": "X 坐标"},
        {"name": "y", "type": {"kind": "primitive", "primitive": "int"}, "description": "Y 坐标"},
        {"name": "width", "type": {"kind": "primitive", "primitive": "int"}, "description": "宽度"},
        {"name": "height", "type": {"kind": "primitive", "primitive": "int"}, "description": "高度"}
      ]
    }
  ],

  "commands": [
    {
      "name": "resize",
      "description": "调整图像尺寸",
      "category": "transform",
      "params": [
        {
          "name": "input",
          "type": {"kind": "primitive", "primitive": "string"},
          "description": "输入文件路径",
          "required": true
        },
        {
          "name": "output",
          "type": {"kind": "primitive", "primitive": "string"},
          "description": "输出文件路径",
          "required": true
        },
        {
          "name": "width",
          "type": {"kind": "primitive", "primitive": "int"},
          "description": "目标宽度（像素）",
          "required": true,
          "constraints": {"min": 1, "max": 65535}
        },
        {
          "name": "height",
          "type": {"kind": "primitive", "primitive": "int"},
          "description": "目标高度（像素）",
          "required": true,
          "constraints": {"min": 1, "max": 65535}
        },
        {
          "name": "keep_aspect",
          "type": {"kind": "primitive", "primitive": "bool"},
          "description": "是否保持宽高比",
          "required": false,
          "default": true
        }
      ],
      "returns": {
        "type": {"kind": "object", "properties": [
          {"name": "width", "type": {"kind": "primitive", "primitive": "int"}},
          {"name": "height", "type": {"kind": "primitive", "primitive": "int"}},
          {"name": "file_size", "type": {"kind": "primitive", "primitive": "int64"}}
        ]},
        "description": "处理结果"
      },
      "errors": [
        {"code": 1001, "message": "输入文件不存在"},
        {"code": 1002, "message": "不支持的图像格式"},
        {"code": 1003, "message": "输出路径无写入权限"}
      ],
      "examples": [
        {
          "title": "基本缩放",
          "request": {"input": "/path/to/image.png", "output": "/path/to/output.png", "width": 800, "height": 600},
          "response": {"width": 800, "height": 600, "file_size": 102400}
        }
      ]
    }
  ],

  "config": {
    "groups": [
      {
        "name": "general",
        "title": "常规设置",
        "items": [
          {
            "key": "default_format",
            "type": {"kind": "ref", "ref_name": "ImageFormat"},
            "title": "默认输出格式",
            "default": "png",
            "required": false,
            "ui_hints": {"widget": "select"}
          },
          {
            "key": "quality",
            "type": {"kind": "primitive", "primitive": "int"},
            "title": "输出质量",
            "description": "JPEG/WebP 压缩质量 (1-100)",
            "default": 85,
            "required": false,
            "constraints": {"min": 1, "max": 100},
            "ui_hints": {"widget": "slider"}
          }
        ]
      },
      {
        "name": "performance",
        "title": "性能设置",
        "items": [
          {
            "key": "thread_count",
            "type": {"kind": "primitive", "primitive": "int"},
            "title": "处理线程数",
            "default": 4,
            "required": false,
            "constraints": {"min": 1, "max": 32},
            "ui_hints": {"widget": "spinbox"}
          }
        ]
      }
    ]
  }
}
```

### 4.2 C++ API 设计

#### 4.2.1 类型定义 (meta/types.h)

```cpp
#pragma once

#include <QString>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonArray>
#include <vector>
#include <optional>
#include <memory>

namespace stdiolink {
namespace meta {

/**
 * 基础类型枚举
 */
enum class PrimitiveType {
    String,
    Int,
    Int64,
    Double,
    Bool,
    Null,
    Any
};

/**
 * 类型种类
 */
enum class TypeKind {
    Primitive,  // 基础类型
    Array,      // 数组
    Object,     // 对象
    Enum,       // 枚举
    Ref         // 类型引用
};

/**
 * 约束条件
 */
struct Constraints {
    std::optional<double> min;
    std::optional<double> max;
    std::optional<int> minLength;
    std::optional<int> maxLength;
    std::optional<QString> pattern;
    std::optional<QJsonArray> enumValues;

    QJsonObject toJson() const;
    static Constraints fromJson(const QJsonObject& json);
};

/**
 * UI 提示
 */
struct UIHints {
    QString widget;           // 控件类型: input, select, slider, checkbox
    QString placeholder;
    QString helpText;
    int order = 0;
    bool hidden = false;
    bool readonly = false;
    QString dependsOn;        // 依赖的其他配置项

    QJsonObject toJson() const;
    static UIHints fromJson(const QJsonObject& json);
};

} // namespace meta
} // namespace stdiolink
```

#### 4.2.2 类型引用 (meta/type_ref.h)

```cpp
#pragma once

#include "types.h"
#include <memory>
#include <vector>

namespace stdiolink {
namespace meta {

// 前向声明
class ParamMeta;

/**
 * 枚举值定义
 */
struct EnumValue {
    QJsonValue value;       // 枚举值（通常是字符串或整数）
    QString description;    // 值描述

    QJsonObject toJson() const;
    static EnumValue fromJson(const QJsonObject& json);
};

/**
 * 类型引用
 * 描述参数或返回值的类型信息
 */
class TypeRef {
public:
    TypeRef() = default;

    // 工厂方法 - 创建基础类型
    static TypeRef primitive(PrimitiveType type);
    static TypeRef string();
    static TypeRef integer();
    static TypeRef int64();
    static TypeRef number();
    static TypeRef boolean();
    static TypeRef any();

    // 工厂方法 - 创建复合类型
    static TypeRef array(const TypeRef& elementType);
    static TypeRef object(std::vector<ParamMeta> properties);
    static TypeRef enumType(std::vector<EnumValue> values);
    static TypeRef ref(const QString& typeName);

    // 访问器
    TypeKind kind() const { return m_kind; }
    PrimitiveType primitiveType() const { return m_primitive; }
    const TypeRef* elementType() const { return m_elementType.get(); }
    const std::vector<ParamMeta>& properties() const { return m_properties; }
    const std::vector<EnumValue>& enumValues() const { return m_enumValues; }
    const QString& refName() const { return m_refName; }

    // 序列化
    QJsonObject toJson() const;
    static TypeRef fromJson(const QJsonObject& json);

private:
    TypeKind m_kind = TypeKind::Primitive;
    PrimitiveType m_primitive = PrimitiveType::Any;
    std::shared_ptr<TypeRef> m_elementType;
    std::vector<ParamMeta> m_properties;
    std::vector<EnumValue> m_enumValues;
    QString m_refName;
};

} // namespace meta
} // namespace stdiolink
```

#### 4.2.3 参数元数据 (meta/param_meta.h)

```cpp
#pragma once

#include "type_ref.h"

namespace stdiolink {
namespace meta {

/**
 * 参数元数据
 * 描述命令参数或对象属性
 */
class ParamMeta {
public:
    ParamMeta() = default;
    ParamMeta(const QString& name, const TypeRef& type);

    // 链式设置器
    ParamMeta& setDescription(const QString& desc);
    ParamMeta& setRequired(bool required);
    ParamMeta& setDefault(const QJsonValue& value);
    ParamMeta& setConstraints(const Constraints& constraints);
    ParamMeta& setUIHints(const UIHints& hints);

    // 访问器
    const QString& name() const { return m_name; }
    const TypeRef& type() const { return m_type; }
    const QString& description() const { return m_description; }
    bool isRequired() const { return m_required; }
    const QJsonValue& defaultValue() const { return m_default; }
    const Constraints& constraints() const { return m_constraints; }
    const UIHints& uiHints() const { return m_uiHints; }

    // 序列化
    QJsonObject toJson() const;
    static ParamMeta fromJson(const QJsonObject& json);

private:
    QString m_name;
    TypeRef m_type;
    QString m_description;
    bool m_required = true;
    QJsonValue m_default;
    Constraints m_constraints;
    UIHints m_uiHints;
};

} // namespace meta
} // namespace stdiolink
```

#### 4.2.4 命令元数据 (meta/command_meta.h)

```cpp
#pragma once

#include "param_meta.h"

namespace stdiolink {
namespace meta {

/**
 * 错误定义
 */
struct ErrorMeta {
    int code;
    QString message;
    QString description;

    QJsonObject toJson() const;
    static ErrorMeta fromJson(const QJsonObject& json);
};

/**
 * 示例定义
 */
struct ExampleMeta {
    QString title;
    QString description;
    QJsonObject request;
    QJsonValue response;

    QJsonObject toJson() const;
    static ExampleMeta fromJson(const QJsonObject& json);
};

/**
 * 返回值元数据
 */
struct ReturnMeta {
    TypeRef type;
    QString description;

    QJsonObject toJson() const;
    static ReturnMeta fromJson(const QJsonObject& json);
};

/**
 * 命令元数据
 */
class CommandMeta {
public:
    CommandMeta() = default;
    explicit CommandMeta(const QString& name);

    // 链式设置器
    CommandMeta& setDescription(const QString& desc);
    CommandMeta& setCategory(const QString& category);
    CommandMeta& setDeprecated(bool deprecated, const QString& message = {});
    CommandMeta& addParam(const ParamMeta& param);
    CommandMeta& setReturns(const ReturnMeta& returns);
    CommandMeta& addError(const ErrorMeta& error);
    CommandMeta& addExample(const ExampleMeta& example);

    // 访问器
    const QString& name() const { return m_name; }
    const QString& description() const { return m_description; }
    const QString& category() const { return m_category; }
    bool isDeprecated() const { return m_deprecated; }
    const QString& deprecatedMessage() const { return m_deprecatedMessage; }
    const std::vector<ParamMeta>& params() const { return m_params; }
    const ReturnMeta& returns() const { return m_returns; }
    const std::vector<ErrorMeta>& errors() const { return m_errors; }
    const std::vector<ExampleMeta>& examples() const { return m_examples; }

    // 序列化
    QJsonObject toJson() const;
    static CommandMeta fromJson(const QJsonObject& json);

private:
    QString m_name;
    QString m_description;
    QString m_category;
    bool m_deprecated = false;
    QString m_deprecatedMessage;
    std::vector<ParamMeta> m_params;
    ReturnMeta m_returns;
    std::vector<ErrorMeta> m_errors;
    std::vector<ExampleMeta> m_examples;
};

} // namespace meta
} // namespace stdiolink
```

#### 4.2.5 Driver 元数据 (meta/driver_meta.h)

```cpp
#pragma once

#include "command_meta.h"

namespace stdiolink {
namespace meta {

/**
 * 配置项
 */
struct ConfigItem {
    QString key;
    TypeRef type;
    QString title;
    QString description;
    QJsonValue defaultValue;
    bool required = false;
    Constraints constraints;
    UIHints uiHints;

    QJsonObject toJson() const;
    static ConfigItem fromJson(const QJsonObject& json);
};

/**
 * 配置分组
 */
struct ConfigGroup {
    QString name;
    QString title;
    QString description;
    std::vector<ConfigItem> items;

    QJsonObject toJson() const;
    static ConfigGroup fromJson(const QJsonObject& json);
};

/**
 * 自定义类型定义
 */
struct TypeDef {
    QString name;
    TypeKind kind;
    QString description;
    std::vector<ParamMeta> properties;  // for object
    std::vector<EnumValue> enumValues;  // for enum

    QJsonObject toJson() const;
    static TypeDef fromJson(const QJsonObject& json);
};

/**
 * Driver 元数据根类
 */
class DriverMeta {
public:
    DriverMeta() = default;
    explicit DriverMeta(const QString& name);

    // 链式设置器
    DriverMeta& setVersion(const QString& version);
    DriverMeta& setDescription(const QString& desc);
    DriverMeta& setAuthor(const QString& author);
    DriverMeta& setLicense(const QString& license);
    DriverMeta& setProtocolVersion(const QString& version);
    DriverMeta& addCommand(const CommandMeta& cmd);
    DriverMeta& addType(const TypeDef& type);
    DriverMeta& addConfigGroup(const ConfigGroup& group);

    // 访问器
    const QString& name() const { return m_name; }
    const QString& version() const { return m_version; }
    const QString& description() const { return m_description; }
    const QString& author() const { return m_author; }
    const QString& license() const { return m_license; }
    const QString& protocolVersion() const { return m_protocolVersion; }
    const std::vector<CommandMeta>& commands() const { return m_commands; }
    const std::vector<TypeDef>& types() const { return m_types; }
    const std::vector<ConfigGroup>& configGroups() const { return m_configGroups; }

    // 查找
    const CommandMeta* findCommand(const QString& name) const;
    const TypeDef* findType(const QString& name) const;

    // 序列化
    QJsonObject toJson() const;
    static DriverMeta fromJson(const QJsonObject& json);

private:
    QString m_name;
    QString m_version;
    QString m_description;
    QString m_author;
    QString m_license;
    QString m_protocolVersion = "1.0";
    std::vector<CommandMeta> m_commands;
    std::vector<TypeDef> m_types;
    std::vector<ConfigGroup> m_configGroups;
};

} // namespace meta
} // namespace stdiolink
```

#### 4.2.6 MetaBuilder 流式构建器 (meta/meta_builder.h)

```cpp
#pragma once

#include "driver_meta.h"

namespace stdiolink {
namespace meta {

/**
 * 参数构建器
 */
class ParamBuilder {
public:
    explicit ParamBuilder(const QString& name);

    ParamBuilder& type(const TypeRef& t);
    ParamBuilder& description(const QString& desc);
    ParamBuilder& required(bool r = true);
    ParamBuilder& optional();
    ParamBuilder& defaultValue(const QJsonValue& v);
    ParamBuilder& min(double v);
    ParamBuilder& max(double v);
    ParamBuilder& range(double minVal, double maxVal);
    ParamBuilder& minLength(int v);
    ParamBuilder& maxLength(int v);
    ParamBuilder& pattern(const QString& regex);

    ParamMeta build() const;
    operator ParamMeta() const { return build(); }

private:
    ParamMeta m_param;
};

/**
 * 命令构建器
 */
class CommandBuilder {
public:
    explicit CommandBuilder(const QString& name);

    CommandBuilder& description(const QString& desc);
    CommandBuilder& category(const QString& cat);
    CommandBuilder& deprecated(const QString& message = {});
    CommandBuilder& param(const ParamMeta& p);
    CommandBuilder& param(const ParamBuilder& p);
    CommandBuilder& returns(const TypeRef& type, const QString& desc = {});
    CommandBuilder& error(int code, const QString& message);
    CommandBuilder& example(const QString& title,
                            const QJsonObject& request,
                            const QJsonValue& response);

    CommandMeta build() const;
    operator CommandMeta() const { return build(); }

private:
    CommandMeta m_cmd;
};

/**
 * Driver 元数据构建器
 */
class DriverMetaBuilder {
public:
    explicit DriverMetaBuilder(const QString& name);

    DriverMetaBuilder& version(const QString& v);
    DriverMetaBuilder& description(const QString& desc);
    DriverMetaBuilder& author(const QString& a);
    DriverMetaBuilder& license(const QString& l);
    DriverMetaBuilder& protocolVersion(const QString& v);
    DriverMetaBuilder& command(const CommandMeta& cmd);
    DriverMetaBuilder& command(const CommandBuilder& cmd);
    DriverMetaBuilder& type(const TypeDef& t);
    DriverMetaBuilder& configGroup(const ConfigGroup& g);

    DriverMeta build() const;
    operator DriverMeta() const { return build(); }

private:
    DriverMeta m_meta;
};

// 便捷工厂函数
inline ParamBuilder param(const QString& name) { return ParamBuilder(name); }
inline CommandBuilder command(const QString& name) { return CommandBuilder(name); }
inline DriverMetaBuilder driver(const QString& name) { return DriverMetaBuilder(name); }

} // namespace meta
} // namespace stdiolink
```

#### 4.2.7 使用示例

**Driver 端声明元数据**

```cpp
#include "stdiolink/meta/meta_builder.h"

using namespace stdiolink::meta;

// 声明 Driver 元数据
DriverMeta createImageProcessorMeta() {
    return driver("image-processor")
        .version("1.2.0")
        .description("图像处理 Driver")
        .author("stdiolink team")
        .license("MIT")
        .command(
            command("resize")
                .description("调整图像尺寸")
                .category("transform")
                .param(param("input")
                    .type(TypeRef::string())
                    .description("输入文件路径")
                    .required())
                .param(param("output")
                    .type(TypeRef::string())
                    .description("输出文件路径")
                    .required())
                .param(param("width")
                    .type(TypeRef::integer())
                    .description("目标宽度")
                    .required()
                    .range(1, 65535))
                .param(param("height")
                    .type(TypeRef::integer())
                    .description("目标高度")
                    .required()
                    .range(1, 65535))
                .param(param("keep_aspect")
                    .type(TypeRef::boolean())
                    .description("保持宽高比")
                    .optional()
                    .defaultValue(true))
                .returns(TypeRef::object({
                    ParamMeta("width", TypeRef::integer()),
                    ParamMeta("height", TypeRef::integer()),
                    ParamMeta("file_size", TypeRef::int64())
                }), "处理结果")
                .error(1001, "输入文件不存在")
                .error(1002, "不支持的图像格式")
        )
        .build();
}
```

### 4.3 协议设计

#### 4.3.1 内置命令 `__meta__`

Driver 必须实现 `__meta__` 内置命令，用于返回元数据信息。

**请求格式**

```json
{"cmd": "__meta__"}
```

**响应格式**

```json
{"status": "done", "code": 0}
{...DriverMeta JSON...}
```

#### 4.3.2 MetaHandler 实现

```cpp
#pragma once

#include "stdiolink/driver/icommand_handler.h"
#include "stdiolink/meta/driver_meta.h"

namespace stdiolink {
namespace meta {

/**
 * 元数据命令处理器
 * 自动处理 __meta__ 命令
 */
class MetaHandler : public ICommandHandler {
public:
    explicit MetaHandler(const DriverMeta& meta, ICommandHandler* next = nullptr);

    void handle(const QString& cmd, const QJsonValue& data,
                IResponder& responder) override;

private:
    DriverMeta m_meta;
    ICommandHandler* m_next;
};

} // namespace meta
} // namespace stdiolink
```

**MetaHandler 实现**

```cpp
// meta_handler.cpp
void MetaHandler::handle(const QString& cmd, const QJsonValue& data,
                         IResponder& responder) {
    if (cmd == "__meta__") {
        responder.done(0, m_meta.toJson());
        return;
    }

    // 转发给下一个处理器
    if (m_next) {
        m_next->handle(cmd, data, responder);
    } else {
        responder.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
    }
}
```

### 4.4 Host 端 API 设计

#### 4.4.1 MetaClient 类

```cpp
#pragma once

#include "stdiolink/host/driver.h"
#include "stdiolink/meta/driver_meta.h"
#include <QCache>

namespace stdiolink {
namespace meta {

/**
 * 元数据客户端
 * 负责查询和缓存 Driver 元数据
 */
class MetaClient {
public:
    explicit MetaClient(Driver* driver);

    /**
     * 获取 Driver 元数据
     * @param forceRefresh 强制刷新缓存
     * @return 元数据，失败返回空
     */
    std::optional<DriverMeta> getMeta(bool forceRefresh = false);

    /**
     * 获取命令列表
     */
    QStringList listCommands();

    /**
     * 获取指定命令的元数据
     */
    std::optional<CommandMeta> getCommand(const QString& name);

    /**
     * 验证参数
     */
    bool validateParams(const QString& cmd, const QJsonObject& params,
                        QString* errorMsg = nullptr);

private:
    Driver* m_driver;
    std::optional<DriverMeta> m_cache;
};

} // namespace meta
} // namespace stdiolink
```

#### 4.4.2 参数验证器

```cpp
namespace stdiolink {
namespace meta {

/**
 * 参数验证器
 */
class Validator {
public:
    /**
     * 验证参数值是否符合元数据定义
     */
    static bool validate(const QJsonValue& value, const ParamMeta& param,
                         QString* errorMsg = nullptr);

    /**
     * 验证整个请求参数
     */
    static bool validateRequest(const QJsonObject& params,
                                const CommandMeta& cmd,
                                QString* errorMsg = nullptr);

private:
    static bool checkType(const QJsonValue& value, const TypeRef& type);
    static bool checkConstraints(const QJsonValue& value, const Constraints& c);
};

} // namespace meta
} // namespace stdiolink
```

#### 4.4.3 UI 生成器接口

```cpp
namespace stdiolink {
namespace meta {

/**
 * UI 控件类型
 */
enum class WidgetType {
    LineEdit,    // 单行文本
    TextEdit,    // 多行文本
    SpinBox,     // 整数输入
    DoubleSpinBox, // 浮点数输入
    CheckBox,    // 布尔值
    ComboBox,    // 下拉选择
    Slider,      // 滑块
    FileSelect,  // 文件选择
    ColorPicker, // 颜色选择
    Custom       // 自定义
};

/**
 * UI 生成器接口
 */
class IUIGenerator {
public:
    virtual ~IUIGenerator() = default;

    /**
     * 根据命令元数据生成参数输入界面
     */
    virtual QWidget* createCommandForm(const CommandMeta& cmd) = 0;

    /**
     * 根据配置元数据生成配置界面
     */
    virtual QWidget* createConfigForm(const std::vector<ConfigGroup>& groups) = 0;

    /**
     * 从界面收集参数值
     */
    virtual QJsonObject collectParams(QWidget* form) = 0;
};

} // namespace meta
} // namespace stdiolink
```

---

## 5. 实现计划

### 5.1 里程碑划分

| 里程碑 | 名称 | 优先级 | 依赖 |
|--------|------|--------|------|
| M1 | 元数据类型定义 | P0 | 无 |
| M2 | MetaBuilder 构建器 | P0 | M1 |
| M3 | __meta__ 协议实现 | P0 | M2 |
| M4 | Host 端 MetaClient | P0 | M3 |
| M5 | 参数验证器 | P1 | M1 |
| M6 | UI 生成器 | P1 | M4, M5 |
| M7 | 文档生成器 | P2 | M4 |

### 5.2 M1: 元数据类型定义

**目标**：实现所有元数据类型的 C++ 定义和 JSON 序列化

**交付物**：
- `meta/types.h` - 基础类型枚举、约束、UI提示
- `meta/type_ref.h` - 类型引用
- `meta/param_meta.h` - 参数元数据
- `meta/command_meta.h` - 命令元数据
- `meta/driver_meta.h` - Driver 元数据

**验收标准**：
- 所有类型支持 `toJson()` 和 `fromJson()` 序列化
- 单元测试覆盖率 > 90%

### 5.3 M2: MetaBuilder 构建器

**目标**：实现流式 API 构建元数据

**交付物**：
- `meta/meta_builder.h` - ParamBuilder, CommandBuilder, DriverMetaBuilder

**验收标准**：
- 支持链式调用
- 示例代码可编译运行

### 5.4 M3: __meta__ 协议实现

**目标**：实现 Driver 端 __meta__ 命令处理

**交付物**：
- `meta/meta_handler.h` - MetaHandler 类

**验收标准**：
- Driver 能响应 __meta__ 命令
- 返回完整的元数据 JSON

### 5.5 M4: Host 端 MetaClient

**目标**：实现 Host 端元数据查询

**交付物**：
- `meta/meta_client.h` - MetaClient 类

**验收标准**：
- 能查询 Driver 元数据
- 支持缓存

### 5.6 M5: 参数验证器

**目标**：实现参数验证

**交付物**：
- `meta/validator.h` - Validator 类

**验收标准**：
- 支持类型检查
- 支持约束验证

### 5.7 M6: UI 生成器

**目标**：实现基于元数据的 UI 自动生成

**交付物**：
- `meta/ui_generator.h` - IUIGenerator 接口
- `meta/qt_ui_generator.h` - Qt 实现

**验收标准**：
- 能根据命令元数据生成参数表单
- 能根据配置元数据生成配置界面

### 5.8 M7: 文档生成器

**目标**：实现 API 文档自动生成

**交付物**：
- `meta/doc_generator.h` - 文档生成器

**验收标准**：
- 能生成 Markdown 格式文档
- 能生成 HTML 格式文档

---

## 6. 文件结构

```
src/stdiolink/meta/
├── types.h              # 基础类型定义
├── type_ref.h           # 类型引用
├── type_ref.cpp
├── param_meta.h         # 参数元数据
├── param_meta.cpp
├── command_meta.h       # 命令元数据
├── command_meta.cpp
├── driver_meta.h        # Driver 元数据
├── driver_meta.cpp
├── meta_builder.h       # 流式构建器
├── meta_builder.cpp
├── meta_handler.h       # __meta__ 命令处理
├── meta_handler.cpp
├── meta_client.h        # Host 端客户端
├── meta_client.cpp
├── validator.h          # 参数验证器
├── validator.cpp
├── ui_generator.h       # UI 生成器接口
└── qt_ui_generator.h    # Qt UI 实现
```

---

## 7. 总结

本设计文档定义了 stdiolink 元数据自描述系统的完整方案：

1. **类型系统**：支持基础类型、复合类型、枚举、类型引用
2. **元数据结构**：DriverMeta → CommandMeta → ParamMeta 层次结构
3. **构建器 API**：流式 API 简化元数据声明
4. **协议扩展**：`__meta__` 内置命令
5. **Host 端支持**：MetaClient、Validator、UIGenerator

通过实现此系统，Driver 将具备完整的自描述能力，Host 可以：
- 运行时发现 Driver 能力
- 自动生成配置和调用界面
- 在发送前验证参数
- 自动生成 API 文档

