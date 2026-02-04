# stdiolink Driver 元数据自描述系统设计文档

> **版本**: v1.0  
> **日期**: 2026-02-04  
> **状态**: 设计阶段

---

## 1. 概述

### 1.1 背景与动机

当前 stdiolink 框架中，Driver 的命令、参数、配置等信息散落在具体实现代码中，存在以下问题：

1. **缺乏自描述能力**：Host 端无法在运行时获知 Driver 支持哪些命令及其参数规格
2. **无法自动生成文档**：每个 Driver 的接口文档需要手工维护，容易与实现脱节
3. **无法自动生成 UI**：配置界面和调用界面需要针对每个 Driver 手工开发
4. **开发体验差**：开发新 Driver 时，先写代码后补文档，容易遗漏

### 1.2 设计目标

1. **Driver 自描述**：每个 Driver 通过标准元数据模板声明其能力（命令、参数、配置）
2. **Driver 自文档**：元数据可自动生成 API 文档（Markdown / HTML / JSON Schema）
3. **自动 UI 生成**：Host 可根据 Driver 元数据自动生成配置界面和命令调用界面
4. **开发范式转变**：先定义接口元数据模板，再实现具体代码（Contract-First）
5. **运行时能力发现**：Host 可通过标准协议查询 Driver 的元数据

### 1.3 设计原则

- **向后兼容**：现有 Driver 无需修改即可继续工作，元数据为可选增强
- **最小侵入**：元数据通过声明式宏/模板定义，不影响业务逻辑代码
- **类型安全**：利用 C++ 类型系统在编译期进行类型检查
- **协议统一**：元数据通过现有 JSONL 协议传输，复用现有基础设施

---

## 2. 核心概念

### 2.1 元数据层次结构

```
DriverMeta (驱动元数据)
├── info: DriverInfo                    // 驱动基础信息
│   ├── name: string                    // 驱动名称（唯一标识）
│   ├── version: string                 // 版本号（语义化版本）
│   ├── description: string             // 描述
│   └── author: string                  // 作者（可选）
│
├── config: ConfigSchema                // 驱动配置模式
│   └── fields[]: ConfigField           // 配置字段列表
│       ├── name: string                // 字段名
│       ├── type: FieldType             // 字段类型
│       ├── required: bool              // 是否必填
│       ├── default: any                // 默认值
│       ├── description: string         // 描述
│       └── constraints: Constraints    // 约束条件
│
└── commands[]: CommandMeta             // 命令列表
    ├── name: string                    // 命令名
    ├── description: string             // 命令描述
    ├── params[]: ParamMeta             // 参数列表
    │   ├── name: string                // 参数名
    │   ├── type: FieldType             // 参数类型
    │   ├── required: bool              // 是否必填
    │   ├── default: any                // 默认值
    │   ├── description: string         // 描述
    │   └── constraints: Constraints    // 约束条件
    │
    ├── returns: ReturnMeta             // 返回值描述
    │   ├── type: FieldType             // 返回类型
    │   └── description: string         // 返回描述
    │
    └── events[]: EventMeta             // 事件列表（可选）
        ├── name: string                // 事件名
        ├── description: string         // 事件描述
        └── payload: PayloadMeta        // 事件载荷描述
```

### 2.2 字段类型系统（FieldType）

| 类型标识 | 说明 | JSON 对应 | C++ 对应 |
|---------|------|----------|---------|
| `string` | 字符串 | `"..."` | `QString` |
| `int` | 整数 | `123` | `int` / `qint64` |
| `float` | 浮点数 | `1.23` | `double` |
| `bool` | 布尔值 | `true/false` | `bool` |
| `object` | 对象 | `{...}` | `QJsonObject` |
| `array` | 数组 | `[...]` | `QJsonArray` |
| `enum` | 枚举 | `"value"` | `QString` (限定值) |
| `any` | 任意类型 | 任意 | `QJsonValue` |

### 2.3 约束条件（Constraints）

| 约束名 | 适用类型 | 说明 | 示例 |
|-------|---------|------|------|
| `min` | int/float | 最小值 | `"min": 0` |
| `max` | int/float | 最大值 | `"max": 100` |
| `minLength` | string | 最小长度 | `"minLength": 1` |
| `maxLength` | string | 最大长度 | `"maxLength": 255` |
| `pattern` | string | 正则表达式 | `"pattern": "^[a-z]+$"` |
| `enum` | string | 枚举值列表 | `"enum": ["a", "b", "c"]` |
| `minItems` | array | 最小元素数 | `"minItems": 1` |
| `maxItems` | array | 最大元素数 | `"maxItems": 10` |
| `itemType` | array | 元素类型 | `"itemType": "string"` |

---

## 3. 元数据 JSON 规范

### 3.1 完整元数据示例

```json
{
  "info": {
    "name": "scan_driver",
    "version": "1.2.0",
    "description": "3D 扫描驱动程序",
    "author": "stdiolink team"
  },
  "config": {
    "fields": [
      {
        "name": "device_id",
        "type": "string",
        "required": true,
        "description": "设备 ID"
      },
      {
        "name": "timeout_ms",
        "type": "int",
        "required": false,
        "default": 5000,
        "description": "超时时间（毫秒）",
        "constraints": {
          "min": 100,
          "max": 60000
        }
      }
    ]
  },
  "commands": [
    {
      "name": "scan",
      "description": "执行一次扫描",
      "params": [
        {
          "name": "mode",
          "type": "string",
          "required": true,
          "description": "扫描模式",
          "constraints": {
            "enum": ["frame", "continuous", "burst"]
          }
        },
        {
          "name": "fps",
          "type": "int",
          "required": false,
          "default": 10,
          "description": "帧率",
          "constraints": {
            "min": 1,
            "max": 60
          }
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
          {"name": "duration_ms", "type": "int", "description": "耗时（毫秒）"}
        ]
      },
      "events": [
        {
          "name": "progress",
          "description": "扫描进度",
          "payload": {
            "fields": [
              {"name": "percent", "type": "float", "description": "进度百分比"},
              {"name": "message", "type": "string", "description": "状态消息"}
            ]
          }
        }
      ]
    },
    {
      "name": "info",
      "description": "获取设备信息",
      "params": [],
      "returns": {
        "type": "object",
        "description": "设备信息"
      }
    }
  ]
}
```

### 3.2 保留命令：`__meta__`

为支持运行时元数据查询，定义保留命令 `__meta__`：

**请求**:
```json
{"cmd": "__meta__", "data": null}
```

**响应**:
```json
{"status": "done", "code": 0}
{"info": {...}, "config": {...}, "commands": [...]}
```

> [!NOTE]
> 所有支持元数据的 Driver 必须实现 `__meta__` 命令。框架层可自动处理此命令。

---

## 4. C++ API 设计

### 4.1 核心类型定义

#### 4.1.1 `protocol/meta_types.h` - 元数据类型

```cpp
#pragma once

#include <QJsonArray>
#include <QJsonObject>
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
    Float,
    Bool,
    Object,
    Array,
    Enum,
    Any
};

/**
 * 将 FieldType 转换为字符串
 */
QString fieldTypeToString(FieldType type);

/**
 * 从字符串解析 FieldType
 */
FieldType fieldTypeFromString(const QString& str);

/**
 * 字段约束
 */
struct Constraints {
    std::optional<double> min;
    std::optional<double> max;
    std::optional<int> minLength;
    std::optional<int> maxLength;
    std::optional<QString> pattern;
    QStringList enumValues;
    std::optional<int> minItems;
    std::optional<int> maxItems;
    std::optional<FieldType> itemType;

    QJsonObject toJson() const;
    static Constraints fromJson(const QJsonObject& obj);
    bool isEmpty() const;
};

/**
 * 字段元数据（通用：用于参数、配置、返回值字段）
 */
struct FieldMeta {
    QString name;
    FieldType type = FieldType::Any;
    bool required = false;
    QJsonValue defaultValue;
    QString description;
    Constraints constraints;
    QVector<FieldMeta> nestedFields;  // 用于 object 类型的嵌套字段

    QJsonObject toJson() const;
    static FieldMeta fromJson(const QJsonObject& obj);
};

/**
 * 事件载荷元数据
 */
struct PayloadMeta {
    QVector<FieldMeta> fields;

    QJsonObject toJson() const;
    static PayloadMeta fromJson(const QJsonObject& obj);
};

/**
 * 事件元数据
 */
struct EventMeta {
    QString name;
    QString description;
    PayloadMeta payload;

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

    QJsonObject toJson() const;
    static ReturnMeta fromJson(const QJsonObject& obj);
};

/**
 * 命令元数据
 */
struct CommandMeta {
    QString name;
    QString description;
    QVector<FieldMeta> params;
    ReturnMeta returns;
    QVector<EventMeta> events;

    QJsonObject toJson() const;
    static CommandMeta fromJson(const QJsonObject& obj);
};

/**
 * 配置模式元数据
 */
struct ConfigSchema {
    QVector<FieldMeta> fields;

    QJsonObject toJson() const;
    static ConfigSchema fromJson(const QJsonObject& obj);
};

/**
 * 驱动信息
 */
struct DriverInfo {
    QString name;
    QString version;
    QString description;
    QString author;

    QJsonObject toJson() const;
    static DriverInfo fromJson(const QJsonObject& obj);
};

/**
 * 驱动元数据（顶层结构）
 */
struct DriverMeta {
    DriverInfo info;
    ConfigSchema config;
    QVector<CommandMeta> commands;

    QJsonObject toJson() const;
    static DriverMeta fromJson(const QJsonObject& obj);

    /**
     * 根据命令名查找命令元数据
     */
    const CommandMeta* findCommand(const QString& name) const;
};

} // namespace stdiolink::meta
```

#### 4.1.2 `protocol/meta_validator.h` - 参数验证器

```cpp
#pragma once

#include "meta_types.h"
#include <QJsonValue>

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
 * 用于验证请求参数是否符合命令元数据定义
 */
class MetaValidator {
public:
    /**
     * 验证请求参数
     * @param data 请求数据
     * @param cmd 命令元数据
     * @return 验证结果
     */
    static ValidationResult validateParams(const QJsonValue& data,
                                           const CommandMeta& cmd);

    /**
     * 验证配置
     * @param config 配置数据
     * @param schema 配置模式
     * @return 验证结果
     */
    static ValidationResult validateConfig(const QJsonObject& config,
                                           const ConfigSchema& schema);

    /**
     * 验证单个字段值
     * @param value 字段值
     * @param field 字段元数据
     * @return 验证结果
     */
    static ValidationResult validateField(const QJsonValue& value,
                                          const FieldMeta& field);

private:
    static ValidationResult checkType(const QJsonValue& value, FieldType type);
    static ValidationResult checkConstraints(const QJsonValue& value,
                                             const FieldMeta& field);
};

} // namespace stdiolink::meta
```

### 4.2 元数据声明宏（`driver/meta_macros.h`）

为简化元数据定义，提供声明式宏：

```cpp
#pragma once

#include "stdiolink/protocol/meta_types.h"

namespace stdiolink::meta {

/**
 * 元数据构建器 - 流式 API
 */
class DriverMetaBuilder {
public:
    DriverMetaBuilder& info(const QString& name, const QString& version,
                            const QString& desc = {}, const QString& author = {});

    // 配置字段
    DriverMetaBuilder& configField(const QString& name, FieldType type,
                                   bool required = false,
                                   const QString& desc = {});
    DriverMetaBuilder& configFieldDefault(const QString& name, FieldType type,
                                          const QJsonValue& defaultVal,
                                          const QString& desc = {});
    DriverMetaBuilder& withConstraints(const Constraints& c);

    // 命令定义
    DriverMetaBuilder& command(const QString& name, const QString& desc = {});

    // 命令参数
    DriverMetaBuilder& param(const QString& name, FieldType type,
                             bool required = false, const QString& desc = {});
    DriverMetaBuilder& paramDefault(const QString& name, FieldType type,
                                    const QJsonValue& defaultVal,
                                    const QString& desc = {});
    DriverMetaBuilder& paramEnum(const QString& name, const QStringList& values,
                                 bool required = false, const QString& desc = {});

    // 命令返回值
    DriverMetaBuilder& returns(FieldType type, const QString& desc = {});
    DriverMetaBuilder& returnField(const QString& name, FieldType type,
                                   const QString& desc = {});

    // 命令事件
    DriverMetaBuilder& event(const QString& name, const QString& desc = {});
    DriverMetaBuilder& eventField(const QString& name, FieldType type,
                                  const QString& desc = {});

    DriverMeta build() const;

private:
    DriverMeta m_meta;
    CommandMeta* m_currentCmd = nullptr;
    EventMeta* m_currentEvent = nullptr;
    FieldMeta* m_lastField = nullptr;
};

} // namespace stdiolink::meta

// ============== 便捷宏定义 ==============

/**
 * 开始定义 Driver 元数据
 * 用法: STDIOLINK_DRIVER_META_BEGIN(ScanDriver)
 */
#define STDIOLINK_DRIVER_META_BEGIN(ClassName) \
    static const ::stdiolink::meta::DriverMeta& meta() { \
        static auto m = ::stdiolink::meta::DriverMetaBuilder()

/**
 * 结束定义
 */
#define STDIOLINK_DRIVER_META_END() \
            .build(); \
        return m; \
    }

// 简写别名
#define META_INFO(name, ver, desc) .info(name, ver, desc)
#define META_CONFIG(name, type, required, desc) .configField(name, type, required, desc)
#define META_CONFIG_DEFAULT(name, type, def, desc) .configFieldDefault(name, type, def, desc)
#define META_CMD(name, desc) .command(name, desc)
#define META_PARAM(name, type, required, desc) .param(name, type, required, desc)
#define META_PARAM_DEFAULT(name, type, def, desc) .paramDefault(name, type, def, desc)
#define META_PARAM_ENUM(name, values, required, desc) .paramEnum(name, values, required, desc)
#define META_RETURNS(type, desc) .returns(type, desc)
#define META_RETURN_FIELD(name, type, desc) .returnField(name, type, desc)
#define META_EVENT(name, desc) .event(name, desc)
#define META_EVENT_FIELD(name, type, desc) .eventField(name, type, desc)
#define META_CONSTRAINTS(c) .withConstraints(c)
```

### 4.3 使用示例

#### 4.3.1 声明式定义（推荐）

```cpp
#include "stdiolink/driver/meta_macros.h"
#include "stdiolink/driver/icommand_handler.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class ScanHandler : public ICommandHandler {
public:
    // 使用宏声明元数据
    STDIOLINK_DRIVER_META_BEGIN(ScanHandler)
        META_INFO("scan_driver", "1.2.0", "3D 扫描驱动程序")

        // 配置字段
        META_CONFIG("device_id", FieldType::String, true, "设备 ID")
        META_CONFIG_DEFAULT("timeout_ms", FieldType::Int, 5000, "超时时间（毫秒）")
            META_CONSTRAINTS({.min = 100, .max = 60000})

        // scan 命令
        META_CMD("scan", "执行一次扫描")
            META_PARAM_ENUM("mode", {"frame", "continuous", "burst"}, true, "扫描模式")
            META_PARAM_DEFAULT("fps", FieldType::Int, 10, "帧率")
                META_CONSTRAINTS({.min = 1, .max = 60})
            META_RETURNS(FieldType::Object, "扫描结果")
                META_RETURN_FIELD("count", FieldType::Int, "点云数量")
                META_RETURN_FIELD("duration_ms", FieldType::Int, "耗时")
            META_EVENT("progress", "扫描进度")
                META_EVENT_FIELD("percent", FieldType::Float, "进度百分比")
                META_EVENT_FIELD("message", FieldType::String, "状态消息")

        // info 命令
        META_CMD("info", "获取设备信息")
            META_RETURNS(FieldType::Object, "设备信息")
    STDIOLINK_DRIVER_META_END()

    void handle(const QString& cmd, const QJsonValue& data, IResponder& resp) override {
        if (cmd == "scan") {
            handleScan(data.toObject(), resp);
        } else if (cmd == "info") {
            handleInfo(resp);
        } else {
            resp.error(404, QJsonObject{{"message", "unknown command"}});
        }
    }

private:
    void handleScan(const QJsonObject& params, IResponder& resp);
    void handleInfo(IResponder& resp);
};
```

#### 4.3.2 程序化定义

```cpp
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink::meta;

DriverMeta createMeta() {
    DriverMeta meta;

    // 基础信息
    meta.info.name = "echo_driver";
    meta.info.version = "1.0.0";
    meta.info.description = "回显驱动程序";

    // 定义 echo 命令
    CommandMeta echoCmd;
    echoCmd.name = "echo";
    echoCmd.description = "回显消息";

    FieldMeta msgParam;
    msgParam.name = "msg";
    msgParam.type = FieldType::String;
    msgParam.required = true;
    msgParam.description = "要回显的消息";
    echoCmd.params.append(msgParam);

    echoCmd.returns.type = FieldType::Object;
    echoCmd.returns.description = "回显结果";

    FieldMeta echoField;
    echoField.name = "echo";
    echoField.type = FieldType::String;
    echoField.description = "回显的消息";
    echoCmd.returns.fields.append(echoField);

    meta.commands.append(echoCmd);

    return meta;
}
```

### 4.4 扩展 DriverCore 支持元数据

#### 4.4.1 `driver/meta_command_handler.h` - 元数据处理器接口

```cpp
#pragma once

#include "icommand_handler.h"
#include "stdiolink/protocol/meta_types.h"

namespace stdiolink {

/**
 * 支持元数据的命令处理器接口
 * 继承此接口以支持自动元数据查询和参数验证
 */
class IMetaCommandHandler : public ICommandHandler {
public:
    /**
     * 获取驱动元数据
     * 子类必须实现此方法
     */
    virtual const meta::DriverMeta& meta() const = 0;

    /**
     * 是否启用自动参数验证
     * 默认启用，子类可覆盖以禁用
     */
    virtual bool autoValidateParams() const { return true; }
};

} // namespace stdiolink
```

#### 4.4.2 扩展 `DriverCore`

```cpp
// driver_core.h 新增内容

class DriverCore {
public:
    // ... 现有接口 ...

    /**
     * 设置带元数据的处理器
     * 自动处理 __meta__ 命令和参数验证
     */
    void setMetaHandler(IMetaCommandHandler* h);

private:
    IMetaCommandHandler* m_metaHandler = nullptr;

    bool handleMetaCommand(const QString& cmd, const QJsonValue& data,
                           IResponder& responder);
    bool validateAndDispatch(const QString& cmd, const QJsonValue& data,
                             IResponder& responder);
};
```

实现逻辑：

```cpp
// driver_core.cpp 新增内容

void DriverCore::setMetaHandler(IMetaCommandHandler* h) {
    m_metaHandler = h;
    m_handler = h;  // 同时设置基础处理器
}

bool DriverCore::handleMetaCommand(const QString& cmd, const QJsonValue& /*data*/,
                                   IResponder& responder) {
    if (cmd != "__meta__") return false;

    if (m_metaHandler) {
        responder.done(0, m_metaHandler->meta().toJson());
    } else {
        responder.error(501, QJsonObject{{"message", "metadata not supported"}});
    }
    return true;
}

bool DriverCore::validateAndDispatch(const QString& cmd, const QJsonValue& data,
                                     IResponder& responder) {
    if (!m_metaHandler) {
        m_handler->handle(cmd, data, responder);
        return true;
    }

    // 查找命令元数据
    const auto* cmdMeta = m_metaHandler->meta().findCommand(cmd);
    if (!cmdMeta) {
        responder.error(404, QJsonObject{{"message", "unknown command"},
                                         {"command", cmd}});
        return true;
    }

    // 参数验证（如果启用）
    if (m_metaHandler->autoValidateParams()) {
        auto result = meta::MetaValidator::validateParams(data, *cmdMeta);
        if (!result.valid) {
            responder.error(400, QJsonObject{
                {"message", "parameter validation failed"},
                {"field", result.errorField},
                {"error", result.errorMessage}
            });
            return true;
        }
    }

    // 调用实际处理器
    m_metaHandler->handle(cmd, data, responder);
    return true;
}
```

---

## 5. Host 端 API 设计

### 5.1 元数据查询与缓存

#### 5.1.1 `host/driver_meta_cache.h`

```cpp
#pragma once

#include "stdiolink/protocol/meta_types.h"
#include <QHash>
#include <memory>

namespace stdiolink {

class Driver;

/**
 * Driver 元数据缓存
 * 管理已查询的 Driver 元数据
 */
class DriverMetaCache {
public:
    static DriverMetaCache& instance();

    /**
     * 获取 Driver 元数据（同步，可能阻塞）
     * @param driver Driver 实例
     * @param timeoutMs 超时时间
     * @return 元数据指针，失败返回 nullptr
     */
    const meta::DriverMeta* getMeta(Driver* driver, int timeoutMs = 5000);

    /**
     * 异步查询 Driver 元数据
     * @param driver Driver 实例
     * @param callback 回调函数
     */
    void queryMetaAsync(Driver* driver,
                        std::function<void(const meta::DriverMeta*)> callback);

    /**
     * 清除特定 Driver 的缓存
     */
    void invalidate(Driver* driver);

    /**
     * 清除所有缓存
     */
    void clear();

private:
    DriverMetaCache() = default;
    QHash<Driver*, std::shared_ptr<meta::DriverMeta>> m_cache;
};

} // namespace stdiolink
```

### 5.2 扩展 Driver 类

```cpp
// host/driver.h 新增内容

class Driver {
public:
    // ... 现有接口 ...

    /**
     * 查询 Driver 元数据
     * @param timeoutMs 超时时间
     * @return 元数据指针，失败返回 nullptr
     */
    const meta::DriverMeta* queryMeta(int timeoutMs = 5000);

    /**
     * 根据元数据构造请求（带参数验证）
     * @param cmd 命令名
     * @param data 参数数据
     * @return Task 对象；如果参数验证失败，返回失败的 Task
     */
    Task requestWithMeta(const QString& cmd, const QJsonObject& data = {});
};
```

### 5.3 UI 生成器接口

#### 5.3.1 `host/meta_ui_generator.h`

```cpp
#pragma once

#include "stdiolink/protocol/meta_types.h"
#include <QJsonObject>
#include <QString>

namespace stdiolink {

/**
 * UI 描述格式
 */
struct UiDescriptor {
    QString type;          // "form" | "panel" | "dialog"
    QJsonArray fields;     // 字段描述列表
    QJsonObject layout;    // 布局信息
};

/**
 * 元数据 UI 生成器
 * 根据元数据生成 UI 描述，可被前端框架消费
 */
class MetaUiGenerator {
public:
    /**
     * 生成配置界面描述
     * @param config 配置模式
     * @return UI 描述 JSON
     */
    static QJsonObject generateConfigForm(const meta::ConfigSchema& config);

    /**
     * 生成命令调用界面描述
     * @param cmd 命令元数据
     * @return UI 描述 JSON
     */
    static QJsonObject generateCommandForm(const meta::CommandMeta& cmd);

    /**
     * 生成完整 Driver 控制面板描述
     * @param meta Driver 元数据
     * @return UI 描述 JSON
     */
    static QJsonObject generateControlPanel(const meta::DriverMeta& meta);

    /**
     * 根据字段类型推荐 UI 控件类型
     */
    static QString suggestWidgetType(const meta::FieldMeta& field);
};

} // namespace stdiolink
```

UI 描述 JSON 格式示例：

```json
{
  "type": "form",
  "title": "scan 命令",
  "description": "执行一次扫描",
  "fields": [
    {
      "name": "mode",
      "label": "扫描模式",
      "widget": "select",
      "required": true,
      "options": [
        {"value": "frame", "label": "帧模式"},
        {"value": "continuous", "label": "连续模式"},
        {"value": "burst", "label": "突发模式"}
      ]
    },
    {
      "name": "fps",
      "label": "帧率",
      "widget": "slider",
      "required": false,
      "default": 10,
      "min": 1,
      "max": 60
    },
    {
      "name": "roi",
      "label": "感兴趣区域",
      "widget": "group",
      "required": false,
      "fields": [
        {"name": "x", "label": "X", "widget": "number", "required": true},
        {"name": "y", "label": "Y", "widget": "number", "required": true},
        {"name": "width", "label": "宽度", "widget": "number", "required": true},
        {"name": "height", "label": "高度", "widget": "number", "required": true}
      ]
    }
  ],
  "submit": {
    "label": "执行扫描",
    "action": "request"
  }
}
```

---

## 6. 文档生成

### 6.1 Markdown 文档生成器

#### 6.1.1 `tools/meta_doc_generator.h`

```cpp
#pragma once

#include "stdiolink/protocol/meta_types.h"
#include <QString>

namespace stdiolink {

/**
 * 文档生成器
 * 根据元数据自动生成 API 文档
 */
class MetaDocGenerator {
public:
    /**
     * 生成 Markdown 格式文档
     */
    static QString generateMarkdown(const meta::DriverMeta& meta);

    /**
     * 生成 JSON Schema
     */
    static QJsonObject generateJsonSchema(const meta::DriverMeta& meta);

    /**
     * 生成单个命令的 JSON Schema
     */
    static QJsonObject generateCommandSchema(const meta::CommandMeta& cmd);
};

} // namespace stdiolink
```

生成的 Markdown 示例：

```markdown
# scan_driver API 文档

> 版本: 1.2.0  
> 描述: 3D 扫描驱动程序

## 配置

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|-----|------|-----|-------|------|
| device_id | string | ✓ | - | 设备 ID |
| timeout_ms | int | - | 5000 | 超时时间（毫秒）<br>范围: 100 ~ 60000 |

## 命令

### scan

执行一次扫描

**参数**:

| 名称 | 类型 | 必填 | 默认值 | 说明 |
|-----|------|-----|-------|------|
| mode | string | ✓ | - | 扫描模式<br>可选值: `frame`, `continuous`, `burst` |
| fps | int | - | 10 | 帧率<br>范围: 1 ~ 60 |
| roi | object | - | - | 感兴趣区域 |

**返回值**:

| 字段 | 类型 | 说明 |
|-----|------|------|
| count | int | 点云数量 |
| duration_ms | int | 耗时（毫秒） |

**事件**:

- `progress`: 扫描进度
  - percent (float): 进度百分比
  - message (string): 状态消息

### info

获取设备信息

**参数**: 无

**返回值**: object - 设备信息
```

---

## 7. 协议扩展

### 7.1 新增保留命令

| 命令 | 说明 | 可选 |
|-----|------|-----|
| `__meta__` | 查询完整元数据 | 推荐实现 |
| `__version__` | 查询版本信息 | 可选 |
| `__commands__` | 只查询命令列表 | 可选 |

### 7.2 协议版本标识

在响应元数据时包含协议版本：

```json
{
  "protocol_version": "1.0",
  "info": {...},
  "config": {...},
  "commands": [...]
}
```

---

## 8. 文件结构

```
src/stdiolink/
├── protocol/
│   ├── meta_types.h         [NEW] 元数据类型定义
│   ├── meta_types.cpp       [NEW] 元数据类型实现
│   ├── meta_validator.h     [NEW] 参数验证器
│   └── meta_validator.cpp   [NEW] 参数验证器实现
│
├── driver/
│   ├── meta_macros.h        [NEW] 声明式宏定义
│   ├── meta_command_handler.h [NEW] 元数据处理器接口
│   ├── driver_core.h        [MODIFY] 添加元数据支持
│   └── driver_core.cpp      [MODIFY] 实现元数据处理
│
├── host/
│   ├── driver.h             [MODIFY] 添加元数据查询
│   ├── driver.cpp           [MODIFY] 实现元数据查询
│   ├── driver_meta_cache.h  [NEW] 元数据缓存
│   ├── driver_meta_cache.cpp [NEW] 缓存实现
│   ├── meta_ui_generator.h  [NEW] UI 生成器
│   └── meta_ui_generator.cpp [NEW] UI 生成器实现
│
└── tools/
    ├── meta_doc_generator.h  [NEW] 文档生成器
    └── meta_doc_generator.cpp [NEW] 文档生成器实现

doc/
└── generated/               [NEW] 自动生成的文档目录
```

---

## 9. 实现计划

### 9.1 阶段一：核心类型与协议（P0）

**目标**: 完成元数据类型系统和 JSON 序列化

**任务**:
- [ ] 实现 `protocol/meta_types.h` 和 `.cpp`
- [ ] 实现 `protocol/meta_validator.h` 和 `.cpp`
- [ ] 编写单元测试覆盖类型转换和验证逻辑
- [ ] 更新 JSONL 协议文档

**估时**: 2-3 天

### 9.2 阶段二：Driver 端支持（P0）

**目标**: 让 Driver 能够声明和暴露元数据

**任务**:
- [ ] 实现 `driver/meta_macros.h`
- [ ] 实现 `IMetaCommandHandler` 接口
- [ ] 扩展 `DriverCore` 支持 `__meta__` 命令
- [ ] 实现自动参数验证
- [ ] 改造 demo/echo_driver 作为示例
- [ ] 编写集成测试

**估时**: 3-4 天

### 9.3 阶段三：Host 端支持（P1）

**目标**: 让 Host 能够查询和使用元数据

**任务**:
- [ ] 扩展 `Driver` 类添加 `queryMeta()` 和 `requestWithMeta()`
- [ ] 实现 `DriverMetaCache`
- [ ] 编写集成测试

**估时**: 2-3 天

### 9.4 阶段四：UI 生成器（P1）

**目标**: 自动生成 UI 描述

**任务**:
- [ ] 实现 `MetaUiGenerator`
- [ ] 定义 UI 描述 JSON 规范
- [ ] 编写单元测试

**估时**: 2 天

### 9.5 阶段五：文档生成器（P2）

**目标**: 自动生成 API 文档

**任务**:
- [ ] 实现 `MetaDocGenerator`
- [ ] 支持 Markdown 和 JSON Schema 输出
- [ ] 集成到构建流程（可选）

**估时**: 1-2 天

---

## 10. 验证计划

### 10.1 单元测试

| 测试范围 | 测试文件 | 覆盖内容 |
|---------|---------|---------|
| 类型序列化 | `tests/meta_types_test.cpp` | `FieldType` 转换、`DriverMeta` 序列化/反序列化 |
| 参数验证 | `tests/meta_validator_test.cpp` | 类型检查、约束检查、必填字段检查 |
| 宏定义 | `tests/meta_macros_test.cpp` | 宏生成的元数据正确性 |
| UI 生成 | `tests/meta_ui_generator_test.cpp` | 生成的 UI 描述正确性 |

### 10.2 集成测试

| 测试场景 | 测试文件 | 覆盖内容 |
|---------|---------|---------|
| 元数据查询 | `tests/integration/meta_query_test.cpp` | Host 查询 Driver 元数据 |
| 参数验证 | `tests/integration/meta_validation_test.cpp` | 错误参数被正确拒绝 |
| 完整流程 | `tests/integration/meta_e2e_test.cpp` | 从定义到查询到调用的完整流程 |

### 10.3 测试运行命令

```bash
# 运行所有元数据相关测试
./build_ninja/src/tests/stdiolink_tests.exe --gtest_filter=Meta*

# 运行特定测试
./build_ninja/src/tests/stdiolink_tests.exe --gtest_filter=MetaTypesTest.*
./build_ninja/src/tests/stdiolink_tests.exe --gtest_filter=MetaValidatorTest.*
./build_ninja/src/tests/stdiolink_tests.exe --gtest_filter=MetaIntegrationTest.*
```

---

## 11. 开放问题

### 11.1 需要讨论的问题

1. **嵌套对象类型定义**：对于复杂嵌套对象（如 `roi: {x, y, width, height}`），是否需要支持类型引用/复用机制？

2. **二进制数据支持**：如果命令需要传输二进制数据（如图片），是否需要扩展类型系统？

3. **版本兼容策略**：当 Driver 元数据版本变化时，如何保证 Host 端兼容？

4. **动态命令注册**：是否需要支持运行时动态注册新命令？

5. **国际化支持**：描述文本是否需要支持多语言？如何实现？

### 11.2 后续增强方向

- **GraphQL 风格查询**：允许 Host 只查询需要的元数据子集
- **Schema 继承**：支持 Driver 之间的元数据继承复用
- **代码生成器**：根据元数据生成 C++ 强类型参数结构

---

## 12. 参考资料

- [JSON Schema 规范](https://json-schema.org/)
- [OpenAPI 规范](https://www.openapis.org/)
- [Qt Meta-Object System](https://doc.qt.io/qt-6/metaobjects.html)
- [Protocol Buffers](https://protobuf.dev/) - 类型系统设计参考

---

## 附录 A：完整 Demo 代码示例

### A.1 带元数据的 Echo Driver

```cpp
// demo/meta_echo_driver/main.cpp

#include <QCoreApplication>
#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_command_handler.h"
#include "stdiolink/driver/meta_macros.h"
#include "stdiolink/driver/stdio_responder.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class MetaEchoHandler : public IMetaCommandHandler {
public:
    // 声明元数据
    STDIOLINK_DRIVER_META_BEGIN(MetaEchoHandler)
        META_INFO("meta_echo_driver", "1.0.0", "带元数据的回显驱动")

        META_CMD("echo", "回显消息")
            META_PARAM("msg", FieldType::String, true, "要回显的消息")
            META_PARAM_DEFAULT("repeat", FieldType::Int, 1, "重复次数")
                META_CONSTRAINTS({.min = 1, .max = 10})
            META_RETURNS(FieldType::Object, "回显结果")
                META_RETURN_FIELD("echo", FieldType::String, "回显的消息")
                META_RETURN_FIELD("count", FieldType::Int, "实际重复次数")

        META_CMD("ping", "Ping 测试")
            META_RETURNS(FieldType::Object, "Pong 响应")
                META_RETURN_FIELD("pong", FieldType::Bool, "始终为 true")
    STDIOLINK_DRIVER_META_END()

    const DriverMeta& meta() const override {
        return MetaEchoHandler::meta();
    }

    void handle(const QString& cmd, const QJsonValue& data, IResponder& resp) override {
        if (cmd == "echo") {
            auto obj = data.toObject();
            QString msg = obj["msg"].toString();
            int repeat = obj.value("repeat").toInt(1);

            QString result;
            for (int i = 0; i < repeat; ++i) {
                result += msg;
                if (i < repeat - 1) result += " ";
            }

            resp.done(0, QJsonObject{
                {"echo", result},
                {"count", repeat}
            });
        } else if (cmd == "ping") {
            resp.done(0, QJsonObject{{"pong", true}});
        } else {
            // 不应该到达这里（框架已处理未知命令）
            resp.error(404, QJsonObject{{"message", "unknown command"}});
        }
    }
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    MetaEchoHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);  // 使用元数据处理器
    core.setProfile(DriverCore::Profile::KeepAlive);

    return core.run();
}
```

### A.2 Host 端查询元数据示例

```cpp
// demo/meta_host_demo/main.cpp

#include <QCoreApplication>
#include <QDebug>
#include "stdiolink/host/driver.h"
#include "stdiolink/host/meta_ui_generator.h"

using namespace stdiolink;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    Driver d;
    if (!d.start("meta_echo_driver.exe")) {
        qCritical() << "Failed to start driver";
        return 1;
    }

    // 查询元数据
    const auto* meta = d.queryMeta();
    if (!meta) {
        qWarning() << "Driver does not support metadata";
    } else {
        qInfo() << "Driver:" << meta->info.name;
        qInfo() << "Version:" << meta->info.version;
        qInfo() << "Commands:";
        for (const auto& cmd : meta->commands) {
            qInfo() << "  -" << cmd.name << ":" << cmd.description;
        }

        // 生成 UI 描述
        auto ui = MetaUiGenerator::generateControlPanel(*meta);
        qInfo() << "UI Descriptor:" << QJsonDocument(ui).toJson();
    }

    // 使用带验证的请求
    Task t = d.requestWithMeta("echo", {{"msg", "hello"}, {"repeat", 3}});

    Message msg;
    while (t.waitNext(msg, 5000)) {
        if (msg.status == "done") {
            qInfo() << "Result:" << msg.payload;
            break;
        } else if (msg.status == "error") {
            qWarning() << "Error:" << msg.code << msg.payload;
            break;
        }
    }

    d.terminate();
    return 0;
}
```
