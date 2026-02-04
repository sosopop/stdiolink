# stdiolink Driver 元数据自描述系统 - 开发需求与详细设计

> **版本**: v2.0
> **日期**: 2026-02-04
> **状态**: 设计修订版
> **前置条件**: 里程碑 1-6 已完成

---

## 1. 概述

### 1.1 背景与动机

当前 stdiolink 框架已具备稳定的 JSONL 协议与 Driver/Host 调用模型，但 Driver 对外接口缺少标准化的"结构化描述"，导致：

1. **缺乏自描述能力**：Host 端无法在运行时获知 Driver 支持哪些命令及其参数规格
2. **无法自动生成文档**：每个 Driver 的接口文档需要手工维护，容易与实现脱节
3. **无法自动生成 UI**：配置界面和调用界面需要针对每个 Driver 手工开发
4. **开发体验差**：调用请求只能人工拼装，难以自动化或低代码

### 1.2 设计目标

| 目标 | 描述 | 受益方 |
|------|------|--------|
| **自描述** | Driver 通过标准元数据模板声明其能力 | Host/IDE |
| **自文档** | 元数据可自动生成 API 文档 | 开发者 |
| **自动 UI** | Host 可根据元数据自动生成配置界面和命令调用界面 | 终端用户 |
| **自动验证** | 框架层自动处理参数类型检查和约束验证 | 运行时系统 |

### 1.3 设计原则

1. **向后兼容**：现有 Driver 无需修改即可继续工作，元数据为可选增强
2. **最小侵入**：元数据通过声明式宏/模板定义，不影响业务逻辑代码
3. **类型安全**：以运行时校验为主，编译期强类型绑定为可选增强
4. **协议统一**：元数据通过现有 JSONL 协议传输，复用现有基础设施
5. **先定义后实现**：推行"先写模板描述 → 再实现命令代码"的开发范式

### 1.4 范围界定

**本阶段交付范围**：
- 元数据类型定义与 JSON 序列化
- Driver 侧元数据声明与导出
- Host 侧元数据查询与缓存
- 参数校验与默认值填充
- UI 描述模型生成（不含具体 UI 实现）

**明确非目标**：
- 不做完整 GUI 实现（仅输出 UI 生成所需的数据模型）
- 不做 TCP/LocalSocket（仍使用 stdio）
- 不做复杂权限体系（先留字段，后续扩展）
- 不做跨语言反射机制

### 1.5 修订说明

本修订版在保持 v1 全部内容的基础上，补齐以下关键缺口并保持向后兼容：

1. 明确事件类型在协议中的承载方式（event payload 中必须带事件名）
2. 补充 `done/error/event` 响应 schema 描述与错误码范围
3. 统一配置命令命名，明确 `meta.config.get/set` 与兼容别名
4. 强化数组与对象类型描述能力（items、additionalProperties、requiredKeys）
5. 明确校验与类型转换的执行边界
6. 增加 `entry` 启动信息与 capabilities/profiles 规范
7. 处理 int64 精度约束与传输策略

---

## 2. 核心概念与术语

### 2.1 术语定义

| 术语 | 定义 |
|------|------|
| **DriverMeta** | Driver 的完整元数据描述，包含基本信息、配置模式、命令列表 |
| **CommandMeta** | 单个命令的结构化描述，包含参数、返回值、事件定义 |
| **FieldMeta** | 字段/参数的元数据，包含类型、约束、默认值、UI 提示 |
| **ConfigSchema** | Driver 全局配置的模式描述 |
| **Constraints** | 字段约束条件，如 min/max/pattern/enum 等 |
| **UIHint** | UI 渲染提示，如控件类型、分组、排序等 |

### 2.2 元数据层次结构

```
DriverMeta (驱动元数据)
├── schemaVersion: string              // 元数据版本
├── info: DriverInfo                    // 驱动基础信息
│   ├── id: string                      // 驱动唯一标识
│   ├── name: string                    // 驱动名称
│   ├── version: string                 // 版本号（语义化版本）
│   ├── description: string             // 描述
│   └── vendor: string                  // 厂商（可选）
│   ├── entry: object                   // 启动信息（program + defaultArgs）
│   ├── capabilities: string[]          // 能力声明
│   └── profiles: string[]              // oneshot/keepalive 支持情况
│
├── types: TypeDef map                  // 可复用类型定义（可选）
├── config: ConfigSchema                // 驱动配置模式
│   ├── fields[]: FieldMeta             // 配置字段列表
│   └── apply: ConfigApply              // 配置注入方式
│
├── commands[]: CommandMeta             // 命令列表
│   ├── name: string                    // 命令名
│   ├── description: string             // 命令描述
│   ├── params[]: FieldMeta             // 参数列表
│   ├── returns: ReturnMeta             // 返回值描述
│   └── events[]: EventMeta             // 事件列表（可选）
│
└── errors[]/examples[]                 // 错误码与示例（可选）
```

---

## 3. 协议扩展设计

### 3.1 保留命令命名空间

所有框架保留命令使用 `meta.` 前缀，业务命令应避免此前缀：

| 命令 | 方向 | 描述 | 必须实现 |
|------|------|------|----------|
| `meta.describe` | Host → Driver | 获取完整元数据 | 推荐实现 |
| `meta.config.get` | Host → Driver | 获取当前配置 | 可选 |
| `meta.config.set` | Host → Driver | 设置配置 | 可选 |
| `meta.validate` | Host → Driver | 验证参数（不执行） | 可选 |

兼容别名（可选支持）：`meta.getConfig`、`meta.setConfig`、`meta.get`、`meta.introspect`、`sys.get_meta`。

### 3.2 meta.describe 请求与响应

**请求**：
```json
{"cmd": "meta.describe", "data": null}
```

**响应**（done payload）：
```json
{
  "schemaVersion": "1.0",
  "info": {
    "id": "com.example.scan_driver",
    "name": "Scan Driver",
    "version": "1.2.0",
    "description": "3D 扫描驱动程序",
    "vendor": "stdiolink"
  },
  "capabilities": ["keepAlive", "streaming", "config"],
  "config": {
    "fields": [...],
    "apply": {"method": "env", "envPrefix": "SCAN_"}
  },
  "commands": [...]
}
```

**兼容与扩展说明**：
`info` 字段在 v2 中升级为 `driver`，Host 端应同时兼容 `info` 与 `driver` 两种字段名。推荐新的完整结构示例：

```json
{
  "schemaVersion": "1.1",
  "driver": {
    "id": "com.example.scan_driver",
    "name": "Scan Driver",
    "version": "1.2.0",
    "description": "3D 扫描驱动程序",
    "vendor": "stdiolink",
    "entry": { "program": "scan_driver.exe", "defaultArgs": ["--mode=stdio", "--profile=keepalive"] },
    "profiles": ["oneshot", "keepalive"],
    "capabilities": ["streaming", "config"]
  },
  "types": {},
  "config": { "fields": [], "apply": { "method": "env", "envPrefix": "SCAN_" } },
  "commands": []
}
```

### 3.3 错误码扩展

| 错误码 | 名称 | 描述 |
|--------|------|------|
| 400 | ValidationFailed | 参数验证失败 |
| 404 | CommandNotFound | 命令不存在 |
| 409 | SchemaMismatch | Schema 版本不兼容 |
| 501 | MetaNotSupported | 不支持元数据查询 |

### 3.4 事件命名约定（关键修订）

协议响应头仅包含 `status` 与 `code`，无法区分事件类型，因此事件名必须在 payload 中承载。

约定：
1. 当 `status == "event"` 时，payload 必须包含 `event` 字段
2. 当命令只有单一事件类型时，允许省略 `event` 字段，但 Host 需将其视为默认事件

示例：
```json
{"status":"event","code":0}
{"event":"progress","percent":0.35,"message":"processing"}
```

### 3.5 meta.config.get / meta.config.set 数据格式

`meta.config.get`：
- 请求：`{"cmd":"meta.config.get","data":null}`
- 响应：`done` payload 为当前配置对象，需符合 `config` schema

`meta.config.set`：
- 请求：`{"cmd":"meta.config.set","data":{...}}`，其中 data 为部分或完整配置对象
- 响应：`done` payload 建议返回 `{"status":"applied"}` 或完整配置对象

### 3.6 版本协商

- Driver 通过 `schemaVersion` 声明元数据版本
- Driver 可选声明 `minHostSchemaVersion` 与 `maxHostSchemaVersion`
- Host 若发现版本不兼容，应返回 409 并提示降级或升级

### 3.7 capabilities 与 profiles 枚举约定

推荐的 `profiles` 值：
- `oneshot`
- `keepalive`

推荐的 `capabilities` 值（可扩展）：
- `stdio`
- `console`
- `streaming`
- `progress`
- `config`
- `cancel`

---

## 4. 类型系统设计

### 4.1 字段类型枚举

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

### 4.2 类型与 JSON 映射

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

### 4.3 约束条件

| 约束名 | 适用类型 | 说明 | JSON 键 |
|--------|----------|------|---------|
| required | 所有 | 是否必填 | `"required": true` |
| default | 所有 | 默认值 | `"default": ...` |
| min | Int/Int64/Double | 最小值 | `"min": 0` |
| max | Int/Int64/Double | 最大值 | `"max": 100` |
| minLength | String | 最小长度 | `"minLength": 1` |
| maxLength | String | 最大长度 | `"maxLength": 255` |
| pattern | String | 正则表达式 | `"pattern": "^[a-z]+$"` |
| enum | Enum | 枚举值列表 | `"enum": ["a", "b"]` |
| minItems | Array | 最小元素数 | `"minItems": 1` |
| maxItems | Array | 最大元素数 | `"maxItems": 10` |
| itemType | Array | 元素类型 | `"itemType": "string"` |
| items | Array | 元素 schema | `"items": { ... }` |
| requiredKeys | Object | 必填键列表 | `"requiredKeys": ["x","y"]` |
| additionalProperties | Object | 是否允许未知字段 | `"additionalProperties": false` |
| format | String/Int64 | 格式提示 | `"format": "file"` |

### 4.4 format 与扩展类型约定

`format` 用于 UI 与校验提示，推荐值包括：

- `file`、`path`、`folder`
- `uri`、`ipv4`
- `date`、`time`、`datetime`
- `int64`（当使用字符串编码传输 int64 时）

### 4.5 int64 精度约束

JSON number 基于 double 表示，超过 2^53 会失真。约定：

1. `int64` 建议使用字符串编码传输
2. 若以 number 传输，Host 与 Driver 必须拒绝超出安全整数范围的值

---

## 5. 元数据 JSON 规范

### 5.1 完整元数据示例

```json
{
  "schemaVersion": "1.0",
  "info": {
    "id": "com.example.scan_driver",
    "name": "Scan Driver",
    "version": "1.2.0",
    "description": "3D 扫描驱动程序",
    "vendor": "stdiolink"
  },
  "capabilities": ["keepAlive", "streaming", "config"],
  "config": {
    "fields": [
      {
        "name": "deviceId",
        "type": "string",
        "required": true,
        "description": "设备 ID"
      },
      {
        "name": "timeoutMs",
        "type": "int",
        "required": false,
        "default": 5000,
        "description": "超时时间（毫秒）",
        "min": 100,
        "max": 60000,
        "ui": {"widget": "slider", "group": "性能"}
      }
    ],
    "apply": {"method": "env", "envPrefix": "SCAN_"}
  },
  "commands": [
    {
      "name": "scan",
      "description": "执行一次扫描",
      "params": [
        {
          "name": "mode",
          "type": "enum",
          "required": true,
          "description": "扫描模式",
          "enum": ["frame", "continuous", "burst"]
        },
        {
          "name": "fps",
          "type": "int",
          "required": false,
          "default": 10,
          "description": "帧率",
          "min": 1,
          "max": 60,
          "ui": {"widget": "slider"}
        },
        {
          "name": "roi",
          "type": "object",
          "required": false,
          "description": "感兴趣区域",
          "fields": [
            {"name": "x", "type": "int", "required": true},
            {"name": "y", "type": "int", "required": true},
            {"name": "width", "type": "int", "required": true},
            {"name": "height", "type": "int", "required": true}
          ]
        }
      ],
      "returns": {
        "type": "object",
        "description": "扫描结果",
        "fields": [
          {"name": "count", "type": "int", "description": "点云数量"},
          {"name": "durationMs", "type": "int", "description": "耗时（毫秒）"}
        ]
      },
      "events": [
        {
          "name": "progress",
          "description": "扫描进度",
          "fields": [
            {"name": "percent", "type": "double", "description": "进度百分比"},
            {"name": "message", "type": "string", "description": "状态消息"}
          ]
        }
      ]
    }
  ]
}
```

### 5.1.1 事件与响应 schema 约定（补充）

1. `returns` 默认对应 `done` payload 的结构
2. `events` 描述 event payload 的结构与名称
3. `error` payload 结构需在命令级补充 `errorSchema` 或通过 `errors` 统一描述
4. `event` 响应必须在 payload 中带 `event` 字段（见 3.4）

兼容说明：`capabilities` 中的 `keepAlive` 视为 `keepalive` 的旧写法，Host 端应兼容并规范化为小写。

### 5.2 UIHint 规范

```json
{
  "widget": "text|textarea|number|slider|checkbox|select|file|folder|password",
  "group": "分组名",
  "order": 10,
  "placeholder": "提示文本",
  "advanced": false,
  "readonly": false,
  "visibleIf": "mode == 'advanced'",
  "unit": "ms",
  "step": 1
}
```

**控件类型映射**：

| FieldType | 默认控件 | 可选控件 |
|-----------|----------|----------|
| String | text | textarea, password, file, folder |
| Int/Int64 | number | slider |
| Double | number | slider |
| Bool | checkbox | - |
| Enum | select | radio |
| Array | list | - |
| Object | group | - |

### 5.3 ConfigApply 规范

配置注入方式定义：

```json
{
  "method": "startupArgs|env|command|file",
  "envPrefix": "DRIVER_",
  "command": "meta.config.set",
  "fileName": "config.json"
}
```

| method | 说明 |
|--------|------|
| startupArgs | Host 以 `--key=value` 命令行参数注入 |
| env | Host 以环境变量注入（使用 envPrefix） |
| command | Host 通过 stdiolink 调用指定命令注入（推荐 `meta.config.set`） |
| file | Host 生成配置文件并传入路径 |

兼容别名：若 Driver 仍实现 `meta.setConfig` 或 `config.set`，Host 可在失败时尝试回退调用。

### 5.4 Schema 校验规则

Host 端解析 Meta 时必须执行以下校验，失败即注册失败：

1. `schemaVersion` 必填，格式 `主版本.次版本`
2. `info.id` 必填，正则 `^[a-zA-Z0-9_.-]+$`，全局唯一
3. `info.name`、`info.version` 必填
4. `commands` 为数组且命令名唯一，命令名正则 `^[a-zA-Z0-9_.-]+$`
5. `params` 中 `type` 必填且必须在允许集合内
6. `enum` 类型必须包含非空 `enum` 数组
7. `array` 类型必须包含 `items` 或 `itemType`
8. `object` 类型可省略 `fields`，表示允许自由字段
9. 若存在 `request`/`response` schema，则 `request.schema` 必须为 object 或 any
10. 若 `additionalProperties` 为 false，则 Host/Driver 必须拒绝未知字段
11. 若声明 `profiles` 或 `capabilities`，其值必须来自固定枚举集合

说明：`items` 与 `itemType` 均视为 array 元素定义，优先使用 `items`，`itemType` 作为兼容字段保留。

### 5.5 可复用类型定义（types）

为避免重复定义复杂结构，允许在顶层 `types` 中声明可复用类型，并在字段中使用引用。

示例：
```json
{
  "types": {
    "Resolution": {
      "type": "object",
      "properties": {
        "width": { "type": "int", "minimum": 1 },
        "height": { "type": "int", "minimum": 1 }
      },
      "requiredKeys": ["width", "height"]
    }
  }
}
```

字段引用方式建议：
1. `{"$ref":"#/types/Resolution"}`（推荐）
2. 或 `{"ref":"Resolution"}`（兼容简写）

---

## 6. C++ API 设计

### 6.1 核心类型定义

#### 6.1.1 protocol/meta_types.h

```cpp
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>
#include <optional>

namespace stdiolink::meta {

/**
 * 字段类型枚举
 */
enum class FieldType {
    String,
    Int,
    Int64,
    Double,
    Bool,
    Object,
    Array,
    Enum,
    Any
};

QString fieldTypeToString(FieldType type);
FieldType fieldTypeFromString(const QString& str);

/**
 * UI 渲染提示
 */
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

/**
 * 字段约束条件
 */
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
    std::optional<FieldType> itemType;

    QJsonObject toJson() const;
    static Constraints fromJson(const QJsonObject& obj);
    bool isEmpty() const;
};

/**
 * 字段元数据
 */
struct FieldMeta {
    QString name;
    FieldType type = FieldType::Any;
    bool required = false;
    QJsonValue defaultValue;
    QString description;
    Constraints constraints;
    UIHint ui;
    QVector<FieldMeta> fields;  // 用于 Object 类型的嵌套字段
    std::shared_ptr<FieldMeta> items; // 用于 Array 类型的元素 schema
    QStringList requiredKeys;         // Object 必填键
    bool additionalProperties = true; // 是否允许未知字段

    QJsonObject toJson() const;
    static FieldMeta fromJson(const QJsonObject& obj);
};

/**
 * 事件元数据
 */
struct EventMeta {
    QString name;
    QString description;
    QVector<FieldMeta> fields;
    FieldMeta schema; // 事件 payload schema（推荐）

    QJsonObject toJson() const;
    static EventMeta fromJson(const QJsonObject& obj);
};

/**
 * 返回值元数据
 */
struct ReturnMeta {
    FieldType type = FieldType::Object;
    QString description;
    QVector<FieldMeta> fields;
    FieldMeta schema; // done payload schema（推荐）

    QJsonObject toJson() const;
    static ReturnMeta fromJson(const QJsonObject& obj);
};

/**
 * 响应元数据（done/error/event）
 */
struct ResponseMeta {
    FieldMeta done;
    FieldMeta error;
    FieldMeta event;

    QJsonObject toJson() const;
    static ResponseMeta fromJson(const QJsonObject& obj);
};

/**
 * 命令元数据
 */
struct CommandMeta {
    QString name;
    QString description;
    QString title;
    QString summary;
    QString profile; // oneshot|keepalive|both
    QVector<FieldMeta> params;
    ReturnMeta returns;
    QVector<EventMeta> events;
    FieldMeta request;       // request.schema（推荐）
    ResponseMeta response;   // response.event/done/error（推荐）
    QVector<QJsonObject> errors;
    QVector<QJsonObject> examples;
    UIHint ui;

    QJsonObject toJson() const;
    static CommandMeta fromJson(const QJsonObject& obj);
};

/**
 * 配置注入方式
 */
struct ConfigApply {
    QString method;      // startupArgs|env|command|file
    QString envPrefix;
    QString command;
    QString fileName;

    QJsonObject toJson() const;
    static ConfigApply fromJson(const QJsonObject& obj);
};

/**
 * 配置模式
 */
struct ConfigSchema {
    QVector<FieldMeta> fields;
    ConfigApply apply;

    QJsonObject toJson() const;
    static ConfigSchema fromJson(const QJsonObject& obj);
};

/**
 * 驱动基本信息
 */
struct DriverInfo {
    QString id;
    QString name;
    QString version;
    QString description;
    QString vendor;
    QString protocol;
    QJsonObject entry; // program + defaultArgs
    QStringList capabilities;
    QStringList profiles;
    QString minHostSchemaVersion;
    QString maxHostSchemaVersion;

    QJsonObject toJson() const;
    static DriverInfo fromJson(const QJsonObject& obj);
};

/**
 * 驱动元数据（顶层结构）
 */
struct DriverMeta {
    QString schemaVersion = "1.0";
    DriverInfo info;
    QStringList capabilities;
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

说明：`DriverInfo.capabilities` 与顶层 `DriverMeta.capabilities` 视为等价字段，优先读取 `DriverInfo`，顶层字段保留以兼容 v1。
当使用 `items`、`additionalProperties`、`entry` 等新字段时，建议将 `schemaVersion` 提升为 `1.1`。

#### 6.1.2 protocol/meta_validator.h

```cpp
#pragma once

#include "meta_types.h"

namespace stdiolink::meta {

/**
 * 验证结果
 */
struct ValidationResult {
    bool valid = true;
    QString errorField;
    QString errorMessage;

    static ValidationResult ok() { return {true, {}, {}}; }
    static ValidationResult fail(const QString& field, const QString& msg) {
        return {false, field, msg};
    }
};

/**
 * 元数据验证器
 */
class MetaValidator {
public:
    static ValidationResult validateParams(const QJsonValue& data,
                                           const CommandMeta& cmd,
                                           bool allowUnknown = true);
    static ValidationResult validateField(const QJsonValue& value,
                                          const FieldMeta& field);
private:
    static ValidationResult checkType(const QJsonValue& value, FieldType type);
    static ValidationResult checkConstraints(const QJsonValue& value,
                                             const FieldMeta& field);
};

/**
 * 默认值填充器
 */
class DefaultFiller {
public:
    static QJsonObject fillDefaults(const QJsonObject& data,
                                    const QVector<FieldMeta>& fields);
    static QJsonObject fillDefaults(const QJsonObject& data,
                                    const FieldMeta& schema);
};

} // namespace stdiolink::meta
```

#### 6.1.3 类型检查规则

**严格类型匹配**：

| FieldType | 接受的 JSON 类型 | 拒绝的 JSON 类型 |
|-----------|------------------|------------------|
| String | string | number, bool, null, object, array |
| Int | number (整数) | number (小数), string, bool |
| Int64 | number (整数) | number (小数), string, bool |
| Double | number | string, bool, null |
| Bool | bool | string ("true"/"false"), number (0/1) |
| Object | object | array, string, number |
| Array | array | object, string, number |
| Enum | string (在枚举列表中) | 不在列表中的 string |
| Any | 任意类型 | 无 |

补充规则：
1. `Int64` 使用 number 表示时必须在安全整数范围内
2. 当 `format` 为 `int64` 时允许字符串编码

**类型检查实现**：

```cpp
ValidationResult MetaValidator::checkType(const QJsonValue& value, FieldType type) {
    switch (type) {
    case FieldType::String:
        if (!value.isString())
            return ValidationResult::fail("", "expected string");
        break;
    case FieldType::Int:
        if (!value.isDouble())
            return ValidationResult::fail("", "expected integer");
        if (value.toDouble() != static_cast<int>(value.toDouble()))
            return ValidationResult::fail("", "expected integer, got decimal");
        break;
    case FieldType::Int64:
        if (!value.isDouble())
            return ValidationResult::fail("", "expected integer");
        // 建议增加安全整数范围校验，或允许 format=int64 的字符串编码
        break;
    case FieldType::Double:
        if (!value.isDouble())
            return ValidationResult::fail("", "expected number");
        break;
    case FieldType::Bool:
        if (!value.isBool())
            return ValidationResult::fail("", "expected boolean");
        break;
    case FieldType::Object:
        if (!value.isObject())
            return ValidationResult::fail("", "expected object");
        break;
    case FieldType::Array:
        if (!value.isArray())
            return ValidationResult::fail("", "expected array");
        break;
    case FieldType::Any:
        break; // 接受任意类型
    default:
        break;
    }
    return ValidationResult::ok();
}
```

#### 6.1.4 类型自动转换规则

为提升易用性，框架支持**可选的宽松模式**，允许常见的类型自动转换：

**转换规则表**：

| 源类型 | 目标类型 | 转换规则 | 示例 |
|--------|----------|----------|------|
| string | Int | 解析为整数，失败则报错 | "123" → 123 |
| string | Int64 | 解析为64位整数 | "9999999999" → 9999999999 |
| string | Double | 解析为浮点数 | "3.14" → 3.14 |
| string | Bool | "true"/"false"/"1"/"0" | "true" → true |
| number | String | 转为字符串 | 123 → "123" |
| number (int) | Bool | 0→false, 非0→true | 1 → true |
| bool | Int | true→1, false→0 | true → 1 |
| bool | String | "true"/"false" | true → "true" |

**类型转换器实现**：

```cpp
// protocol/meta_converter.h
namespace stdiolink::meta {

class TypeConverter {
public:
    enum class Mode {
        Strict,  // 严格模式：不进行自动转换
        Lenient  // 宽松模式：允许自动转换
    };

    static QJsonValue convert(const QJsonValue& value,
                              FieldType targetType,
                              Mode mode,
                              bool& ok);

    static QJsonObject convertParams(const QJsonObject& data,
                                     const QVector<FieldMeta>& fields,
                                     Mode mode);
};

} // namespace stdiolink::meta
```

**转换器实现示例**：

```cpp
QJsonValue TypeConverter::convert(const QJsonValue& value,
                                  FieldType targetType,
                                  Mode mode,
                                  bool& ok) {
    ok = true;

    // 严格模式：不转换
    if (mode == Mode::Strict) {
        return value;
    }

    // 宽松模式：尝试转换
    switch (targetType) {
    case FieldType::Int:
        if (value.isString()) {
            int result = value.toString().toInt(&ok);
            if (ok) return QJsonValue(result);
        }
        break;

    case FieldType::Double:
        if (value.isString()) {
            double result = value.toString().toDouble(&ok);
            if (ok) return QJsonValue(result);
        }
        break;

    case FieldType::Bool:
        if (value.isString()) {
            QString s = value.toString().toLower();
            if (s == "true" || s == "1") return QJsonValue(true);
            if (s == "false" || s == "0") return QJsonValue(false);
            ok = false;
        } else if (value.isDouble()) {
            return QJsonValue(value.toInt() != 0);
        }
        break;

    case FieldType::String:
        if (value.isDouble()) {
            return QJsonValue(QString::number(value.toDouble()));
        } else if (value.isBool()) {
            return QJsonValue(value.toBool() ? "true" : "false");
        }
        break;

    default:
        break;
    }

    return value;
}
```

转换边界约定：
1. 若启用自动转换，必须在校验前进行
2. Host 可做预转换与预校验，Driver 必须做最终校验

#### 6.2.1 driver/meta_builder.h

```cpp
#pragma once

#include "stdiolink/protocol/meta_types.h"

namespace stdiolink::meta {

class FieldBuilder {
public:
    explicit FieldBuilder(const QString& name, FieldType type);
    FieldBuilder& required(bool req = true);
    FieldBuilder& defaultValue(const QJsonValue& val);
    FieldBuilder& description(const QString& desc);
    FieldBuilder& range(double minVal, double maxVal);
    FieldBuilder& enumValues(const QJsonArray& values);
    FieldBuilder& enumValues(const QStringList& values);
    FieldBuilder& format(const QString& fmt);
    FieldBuilder& widget(const QString& w);
    FieldBuilder& group(const QString& g);
    FieldBuilder& addField(const FieldBuilder& field); // object
    FieldBuilder& items(const FieldBuilder& item);      // array
    FieldMeta build() const;
private:
    FieldMeta m_field;
};

class CommandBuilder {
public:
    explicit CommandBuilder(const QString& name);
    CommandBuilder& description(const QString& desc);
    CommandBuilder& param(const FieldBuilder& field);
    CommandBuilder& returns(FieldType type, const QString& desc = {});
    CommandBuilder& returnField(const QString& name, FieldType type,
                                const QString& desc = {});
    CommandBuilder& event(const QString& name, const QString& desc = {});
    CommandBuilder& eventSchema(const FieldBuilder& field);
    CommandBuilder& errorSchema(const FieldBuilder& field);
    CommandMeta build() const;
private:
    CommandMeta m_cmd;
};

class DriverMetaBuilder {
public:
    DriverMetaBuilder& info(const QString& id, const QString& name,
                            const QString& version, const QString& desc = {});
    DriverMetaBuilder& capability(const QString& cap);
    DriverMetaBuilder& configField(const FieldBuilder& field);
    DriverMetaBuilder& configApply(const QString& method, const QString& param = {});
    DriverMetaBuilder& command(const CommandBuilder& cmd);
    DriverMeta build() const;
private:
    DriverMeta m_meta;
};

} // namespace stdiolink::meta
```

### 6.3 参数映射与请求构造规则（补充）

1. `params[]` 与 `request.schema.properties` 视为等价表达，二者同时存在时以 `request.schema` 为准
2. `params[].name` 映射到请求 `data` 的同名字段
3. 对象型字段使用 `fields` 或 `properties` 描述子结构
4. 数组型字段使用 `items` 描述元素 schema
5. `additionalProperties=false` 时必须拒绝未知字段

---

## 7. Driver 侧实现设计

### 7.1 IMetaCommandHandler 接口

```cpp
// driver/meta_command_handler.h
#pragma once

#include "icommand_handler.h"
#include "stdiolink/protocol/meta_types.h"

namespace stdiolink {

class IMetaCommandHandler : public ICommandHandler {
public:
    virtual const meta::DriverMeta& driverMeta() const = 0;
    virtual bool autoValidateParams() const { return true; }
};

} // namespace stdiolink
```

### 7.2 DriverCore 扩展

```cpp
// driver_core.h 新增
class DriverCore {
public:
    void setMetaHandler(IMetaCommandHandler* h);
private:
    IMetaCommandHandler* m_metaHandler = nullptr;
    bool handleMetaCommand(const QString& cmd, const QJsonValue& data,
                           IResponder& responder);
};
```

### 7.3 使用示例

```cpp
#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_command_handler.h"
#include "stdiolink/driver/meta_builder.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class ScanHandler : public IMetaCommandHandler {
public:
    const DriverMeta& driverMeta() const override {
        static auto meta = DriverMetaBuilder()
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
        return meta;
    }

    void handle(const QString& cmd, const QJsonValue& data,
                IResponder& resp) override {
        if (cmd == "scan") {
            // 参数已经过验证，可直接使用
            auto obj = data.toObject();
            QString mode = obj["mode"].toString();
            int fps = obj["fps"].toInt();
            // 业务逻辑...
            resp.done(0, QJsonObject{{"count", 1000}});
        }
    }
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    ScanHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);
    core.setProfile(DriverCore::Profile::KeepAlive);
    return core.run();
}
```

---

## 8. Host 侧实现设计

### 8.1 Driver 类扩展

```cpp
// host/driver.h 新增
class Driver {
public:
    const meta::DriverMeta* queryMeta(int timeoutMs = 5000);
    bool hasMeta() const;
    Task requestWithValidation(const QString& cmd, const QJsonObject& data = {});
private:
    std::shared_ptr<meta::DriverMeta> m_meta;
};
```

建议实现策略：`queryMeta()` 先尝试 `meta.describe`，失败后可回退到 `meta.get`/`meta.introspect`/`sys.get_meta`，并对 `info` 与 `driver` 字段做兼容解析。

### 8.2 元数据缓存

```cpp
// host/meta_cache.h
#pragma once

#include "stdiolink/protocol/meta_types.h"
#include <QHash>
#include <memory>

namespace stdiolink {

class MetaCache {
public:
    static MetaCache& instance();
    void store(const QString& driverId, std::shared_ptr<meta::DriverMeta> meta);
    std::shared_ptr<meta::DriverMeta> get(const QString& driverId) const;
    void invalidate(const QString& driverId);
    void clear();
private:
    QHash<QString, std::shared_ptr<meta::DriverMeta>> m_cache;
};

} // namespace stdiolink
```

建议增加 `metaHash` 缓存字段，用于检测 Driver 元数据变更后自动刷新。

### 8.3 UI 描述生成器

```cpp
// host/ui_generator.h
#pragma once

#include "stdiolink/protocol/meta_types.h"

namespace stdiolink {

struct FormDesc {
    QString title;
    QString description;
    QJsonArray widgets;
};

class UiGenerator {
public:
    static FormDesc generateCommandForm(const meta::CommandMeta& cmd);
    static FormDesc generateConfigForm(const meta::ConfigSchema& config);
    static QJsonObject toJson(const FormDesc& form);
};

} // namespace stdiolink
```

---

## 9. 实现里程碑

### M1：元数据类型与序列化

**目标**：完成元数据类型系统和 JSON 序列化

**任务**：
- [ ] 实现 `protocol/meta_types.h` 和 `.cpp`
- [ ] 实现 FieldType 枚举与字符串转换
- [ ] 实现所有结构体的 `toJson()` 和 `fromJson()`
- [ ] 编写单元测试

**验收标准**：
- 构建 DriverMeta → JSON 输出稳定、字段齐全
- JSON → DriverMeta 反序列化正确

### M2：Driver 侧 meta.describe

**目标**：让 Driver 能够响应 meta.describe 命令

**任务**：
- [ ] 实现 `IMetaCommandHandler` 接口
- [ ] 扩展 `DriverCore` 拦截 meta.* 命令
- [ ] 实现 meta.describe 响应
- [ ] 改造 demo/echo_driver 作为示例

**验收标准**：
- Host 发送 `meta.describe` 能收到完整元数据
- 不实现 meta 的旧 Driver 仍正常工作

### M3：Builder API

**目标**：提供流式 API 简化元数据定义

**任务**：
- [ ] 实现 `FieldBuilder`
- [ ] 实现 `CommandBuilder`
- [ ] 实现 `DriverMetaBuilder`
- [ ] 编写使用示例

**验收标准**：
- 使用 Builder 构建的元数据与手写 JSON 等价

### M4：参数验证与默认值填充

**目标**：实现自动参数校验

**任务**：
- [ ] 实现 `MetaValidator`
- [ ] 实现 `DefaultFiller`
- [ ] 在 DriverCore 中集成验证逻辑
- [ ] 编写验证测试用例

**验收标准**：
- 缺失必填参数返回 400 错误
- 类型不匹配返回明确错误信息
- 默认值正确填充

### M5：Host 侧元数据查询

**目标**：Host 能查询和使用 Driver 元数据

**任务**：
- [ ] 扩展 `Driver` 类添加 `queryMeta()`
- [ ] 实现 `MetaCache`
- [ ] 实现 `UiGenerator`
- [ ] 编写集成测试

**验收标准**：
- Host 能获取并缓存 Driver 元数据
- 能生成 UI 描述 JSON

---

## 10. 测试计划

### 10.1 单元测试

| 测试文件 | 覆盖内容 |
|----------|----------|
| `meta_types_test.cpp` | FieldType 转换、结构体序列化/反序列化 |
| `meta_validator_test.cpp` | 类型检查、约束检查、必填字段检查 |
| `meta_builder_test.cpp` | Builder 构建的元数据正确性 |
| `ui_generator_test.cpp` | UI 描述生成正确性 |

### 10.2 集成测试

| 测试场景 | 覆盖内容 |
|----------|----------|
| meta.describe 查询 | Host 查询 Driver 元数据完整流程 |
| 参数验证拒绝 | 错误参数被正确拒绝并返回明确错误 |
| 默认值填充 | 缺省参数被正确填充 |
| 端到端流程 | 从定义到查询到调用的完整流程 |

### 10.3 测试命令

```bash
# 运行所有元数据相关测试
./build_ninja/src/tests/stdiolink_tests.exe --gtest_filter=Meta*

# 运行特定测试
./build_ninja/src/tests/stdiolink_tests.exe --gtest_filter=MetaTypesTest.*
./build_ninja/src/tests/stdiolink_tests.exe --gtest_filter=MetaValidatorTest.*
```

---

## 11. 文件结构

```
src/stdiolink/
├── protocol/
│   ├── meta_types.h          [NEW] 元数据类型定义
│   ├── meta_types.cpp        [NEW] 元数据类型实现
│   ├── meta_validator.h      [NEW] 参数验证器
│   └── meta_validator.cpp    [NEW] 参数验证器实现
│
├── driver/
│   ├── meta_builder.h        [NEW] Builder API
│   ├── meta_builder.cpp      [NEW] Builder 实现
│   ├── meta_command_handler.h [NEW] 元数据处理器接口
│   ├── driver_core.h         [MODIFY] 添加元数据支持
│   └── driver_core.cpp       [MODIFY] 实现 meta 命令拦截
│
├── host/
│   ├── driver.h              [MODIFY] 添加 queryMeta()
│   ├── driver.cpp            [MODIFY] 实现元数据查询
│   ├── meta_cache.h          [NEW] 元数据缓存
│   ├── meta_cache.cpp        [NEW] 缓存实现
│   ├── ui_generator.h        [NEW] UI 生成器
│   └── ui_generator.cpp      [NEW] UI 生成器实现
│
└── tests/
    ├── meta_types_test.cpp   [NEW]
    ├── meta_validator_test.cpp [NEW]
    ├── meta_builder_test.cpp [NEW]
    └── meta_integration_test.cpp [NEW]
```

---

## 12. 风险与缓解措施

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 命令名冲突 | 中 | 强制 `meta.` 前缀为保留命令，文档明确警告 |
| Schema 复杂性 | 中 | 限制支持的约束类型，避免实现验证器时过于复杂 |
| 性能影响 | 低 | 元数据静态构建并缓存，避免每次请求重新生成 |
| 向后兼容 | 高 | 不实现 meta 的旧 Driver 返回 501，Host 优雅降级 |
| 类型系统扩展 | 低 | 预留 Any 类型，后续可扩展新类型 |
| 事件类型不可区分 | 高 | 规定 event payload 必须携带事件名 |
| int64 精度丢失 | 中 | 约定字符串编码或安全整数范围 |

---

## 13. 后续扩展方向

1. **配置持久化**：meta.config.get/set 支持配置落盘（兼容 meta.getConfig/setConfig）
2. **文档生成器**：根据元数据自动生成 Markdown/HTML 文档
3. **代码生成器**：根据元数据生成 C++ 强类型参数结构
4. **国际化支持**：描述文本支持多语言
5. **Schema 继承**：支持 Driver 之间的元数据继承复用

---

## 14. 总结

本设计文档定义了 stdiolink 框架的 Driver 元数据自描述系统，核心特性包括：

1. **标准化元数据格式**：基于 JSON 的 Meta Schema v1.0
2. **类型安全的参数系统**：支持 9 种基础类型和丰富的约束条件
3. **流式 Builder API**：简化元数据定义
4. **自动参数验证**：框架层自动校验，业务代码无需处理非法输入
5. **UI 描述生成**：为自动 UI 生成提供数据模型

通过 5 个里程碑的渐进式实现，可在保持向后兼容的前提下，为 stdiolink 框架添加完整的自描述能力。

---

> 本文档综合了 10 篇设计方案的最佳实践，采用渐进式实现策略，确保设计可落地、可验证。
