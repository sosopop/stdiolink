# Driver 元数据自描述架构 - 需求与设计文档

**版本**: 1.0  
**日期**: 2026-02-04  
**状态**: 设计阶段

---

## 目录

1. [概述](#1-概述)
2. [需求分析](#2-需求分析)
3. [架构设计](#3-架构设计)
4. [详细设计](#4-详细设计)
5. [实现规范](#5-实现规范)
6. [开发流程](#6-开发流程)
7. [测试策略](#7-测试策略)
8. [示例场景](#8-示例场景)
9. [兼容性与迁移](#9-兼容性与迁移)
10. [附录](#10-附录)

---

## 1. 概述

### 1.1 背景

当前 stdiolink 框架提供了基本的 Host-Driver 通信能力,但存在以下问题:

1. **缺乏标准化的接口描述**: Driver 支持的命令、参数、配置项完全隐式,需要查看源码或文档才能了解
2. **Host 无法发现能力**: Host 不知道 Driver 支持哪些命令、参数类型、配置项
3. **手动编写调用代码**: Host 需要手动构造请求,容易出错
4. **配置界面无法自动生成**: UI 需要针对每个 Driver 单独开发配置界面
5. **缺乏版本管理**: Driver 接口变更时无法平滑升级
6. **文档与代码分离**: 接口文档与实现容易不同步

### 1.2 目标

通过引入**元数据驱动的自描述架构**,实现:

1. **Driver 自描述**: Driver 通过元数据声明式定义所有命令、参数、配置项
2. **自动文档生成**: 从元数据自动生成接口文档,保证代码与文档一致
3. **运行时能力发现**: Host 可以在运行时查询 Driver 的完整能力清单
4. **UI 自动生成**: 根据元数据自动生成配置界面和调用界面
5. **类型安全**: 编译期和运行期的参数类型检查
6. **版本兼容**: 支持向后兼容的接口演进

### 1.3 设计原则

1. **声明式优先**: 先定义接口元数据,再实现业务逻辑
2. **零依赖侵入**: 元数据定义不依赖特定框架,可以独立使用
3. **类型安全**: 强类型约束,编译期检查
4. **自解释性**: 元数据包含完整的描述、示例、约束信息
5. **可扩展性**: 支持自定义类型、验证器、转换器
6. **向后兼容**: 接口版本化,平滑升级

---

## 2. 需求分析

### 2.1 功能需求

#### 2.1.1 元数据定义

**FR-1.1**: Driver 必须能够通过元数据声明所有支持的命令

- 命令名称、描述、分类
- 参数列表(名称、类型、必填性、默认值、约束、描述)
- 返回值结构
- 示例用法
- 废弃标记、版本信息

**FR-1.2**: Driver 必须能够通过元数据声明所有配置项

- 配置项名称、类型、默认值
- 验证规则、取值范围
- 配置分组、UI 提示
- 配置依赖关系

**FR-1.3**: 元数据必须支持复杂类型定义

- 基础类型: string, int, double, bool
- 容器类型: array, object, map
- 枚举类型: 有限值集合
- 自定义类型: 组合类型、别名
- 可选类型: nullable

**FR-1.4**: 元数据必须包含丰富的约束信息

- 数值范围: min, max, step
- 字符串: minLength, maxLength, pattern
- 数组: minItems, maxItems, uniqueItems
- 对象: required, additionalProperties
- 自定义验证器

#### 2.1.2 运行时能力发现

**FR-2.1**: Host 必须能够查询 Driver 的完整元数据

- `introspect` 命令返回完整的元数据描述
- 支持筛选(按命令、按分类)
- 支持格式化输出(JSON, Markdown, HTML)

**FR-2.2**: Host 必须能够验证请求的合法性

- 命令存在性检查
- 参数类型检查
- 参数约束验证
- 返回建议修正方案

**FR-2.3**: 支持版本协商

- Driver 声明支持的协议版本
- Host 声明期望的协议版本
- 自动选择兼容版本

#### 2.1.3 自动 UI 生成

**FR-3.1**: 根据配置元数据自动生成配置界面

- 表单字段自动布局
- 根据类型选择合适的控件(文本框、下拉框、滑块等)
- 实时验证
- 分组、折叠、标签页

**FR-3.2**: 根据命令元数据自动生成调用界面

- 参数输入表单
- 命令执行按钮
- 结果展示区
- 历史记录

**FR-3.3**: 支持自定义 UI 增强

- 自定义渲染器
- 自定义验证器
- UI hints (placeholder, tooltip, icon)

#### 2.1.4 文档生成

**FR-4.1**: 从元数据自动生成接口文档

- Markdown 格式文档
- HTML 格式文档
- OpenAPI 风格规范
- 示例代码

**FR-4.2**: 支持文档模板定制

- 自定义文档结构
- 多语言支持
- 品牌定制

### 2.2 非功能需求

**NFR-1**: **性能**

- 元数据解析耗时 < 10ms
- introspect 命令响应时间 < 100ms
- 元数据序列化大小 < 100KB

**NFR-2**: **可维护性**

- 元数据与实现代码在同一文件中
- 修改元数据后无需手动同步文档
- 单元测试覆盖率 > 80%

**NFR-3**: **可扩展性**

- 支持插件式自定义类型
- 支持自定义验证器
- 支持自定义序列化格式

**NFR-4**: **易用性**

- 简洁的 C++ 宏或模板定义语法
- 清晰的错误提示
- 完善的示例代码

**NFR-5**: **兼容性**

- 与现有 stdiolink 框架向后兼容
- C++11 或更高版本
- Qt 5.12 或更高版本

---

## 3. 架构设计

### 3.1 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                         Driver 侧                            │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │           元数据定义层 (Metadata Layer)              │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────────────┐   │  │
│  │  │ 命令定义 │  │ 配置定义 │  │  类型系统定义    │   │  │
│  │  └──────────┘  └──────────┘  └──────────────────┘   │  │
│  └──────────────────────────────────────────────────────┘  │
│                          │                                   │
│                          ▼                                   │
│  ┌──────────────────────────────────────────────────────┐  │
│  │         元数据注册器 (Metadata Registry)             │  │
│  │  - 收集所有元数据定义                                │  │
│  │  - 提供查询接口                                       │  │
│  │  - 版本管理                                          │  │
│  └──────────────────────────────────────────────────────┘  │
│                          │                                   │
│                          ▼                                   │
│  ┌──────────────────────────────────────────────────────┐  │
│  │          命令路由器 (Command Router)                  │  │
│  │  - 命令分发                                           │  │
│  │  - 参数验证                                           │  │
│  │  - 类型转换                                           │  │
│  └──────────────────────────────────────────────────────┘  │
│                          │                                   │
│                          ▼                                   │
│  ┌──────────────────────────────────────────────────────┐  │
│  │           业务处理层 (Handler Layer)                  │  │
│  │  - 实际命令实现                                       │  │
│  │  - 业务逻辑                                           │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                              │
└─────────────────────────────────────────────────────────────┘
                             │
                             │ JSONL Protocol
                             │
┌─────────────────────────────────────────────────────────────┐
│                         Host 侧                              │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │      元数据客户端 (Metadata Client)                   │  │
│  │  - 查询 Driver 元数据                                 │  │
│  │  - 缓存元数据                                         │  │
│  │  - 验证请求                                           │  │
│  └──────────────────────────────────────────────────────┘  │
│                          │                                   │
│                          ▼                                   │
│  ┌──────────────────────────────────────────────────────┐  │
│  │         UI 生成器 (UI Generator)                      │  │
│  │  - 配置界面生成                                       │  │
│  │  - 命令调用界面生成                                   │  │
│  │  - 实时验证                                           │  │
│  └──────────────────────────────────────────────────────┘  │
│                          │                                   │
│                          ▼                                   │
│  ┌──────────────────────────────────────────────────────┐  │
│  │        文档生成器 (Doc Generator)                     │  │
│  │  - Markdown 文档                                      │  │
│  │  - HTML 文档                                          │  │
│  │  - OpenAPI 规范                                       │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 核心组件

#### 3.2.1 元数据定义 (Metadata Definition)

**职责**:

- 提供声明式 API 定义命令和配置
- 类型系统定义
- 验证规则定义

**关键类**:

- `CommandMetadata`: 命令元数据
- `ParameterMetadata`: 参数元数据
- `ConfigMetadata`: 配置元数据
- `TypeDescriptor`: 类型描述符
- `Constraint`: 约束条件

#### 3.2.2 元数据注册器 (Metadata Registry)

**职责**:

- 收集和管理所有元数据
- 提供查询接口
- 版本管理
- 序列化/反序列化

**关键类**:

- `MetadataRegistry`: 元数据注册中心
- `DriverInfo`: Driver 基本信息
- `VersionInfo`: 版本信息

#### 3.2.3 命令路由器 (Command Router)

**职责**:

- 解析请求命令
- 参数验证
- 类型转换
- 分发到具体 Handler

**关键类**:

- `CommandRouter`: 命令路由器
- `ParameterValidator`: 参数验证器
- `TypeConverter`: 类型转换器

#### 3.2.4 元数据客户端 (Metadata Client)

**职责**:

- 从 Driver 查询元数据
- 缓存元数据
- 验证请求合法性
- 提供便捷的调用 API

**关键类**:

- `MetadataClient`: 元数据客户端
- `MetadataCache`: 元数据缓存
- `RequestBuilder`: 请求构建器

#### 3.2.5 UI 生成器 (UI Generator)

**职责**:

- 根据元数据生成 Qt Widgets 界面
- 根据元数据生成 QML 界面
- 实时验证
- 数据绑定

**关键类**:

- `ConfigUIGenerator`: 配置界面生成器
- `CommandUIGenerator`: 命令界面生成器
- `WidgetFactory`: 控件工厂
- `Validator`: 验证器

#### 3.2.6 文档生成器 (Doc Generator)

**职责**:

- 生成 Markdown 文档
- 生成 HTML 文档
- 生成 OpenAPI 规范
- 支持模板定制

**关键类**:

- `DocGenerator`: 文档生成器
- `MarkdownGenerator`: Markdown 生成器
- `HtmlGenerator`: HTML 生成器
- `OpenApiGenerator`: OpenAPI 生成器

---

## 4. 详细设计

### 4.1 元数据定义规范

#### 4.1.1 元数据结构

元数据采用 JSON Schema 兼容的结构定义:

```cpp
// 驱动信息元数据
struct DriverInfo {
    QString name;           // 驱动名称
    QString version;        // 驱动版本 (semver)
    QString description;    // 驱动描述
    QString author;         // 作者
    QString license;        // 许可证
    QStringList tags;       // 标签
    QString homepage;       // 主页 URL
    
    struct ProtocolVersion {
        int major;
        int minor;
    };
    ProtocolVersion protocolVersion; // 协议版本
};

// 命令元数据
struct CommandMetadata {
    QString name;           // 命令名称 (唯一标识)
    QString displayName;    // 显示名称
    QString description;    // 命令描述
    QString category;       // 分类 (用于分组)
    QStringList tags;       // 标签
    
    struct Parameter {
        QString name;       // 参数名称
        QString displayName;// 显示名称
        QString description;// 参数描述
        TypeDescriptor type;// 类型描述
        bool required;      // 是否必填
        QJsonValue defaultValue; // 默认值
        QList<Constraint> constraints; // 约束条件
        QJsonObject uiHints; // UI 提示
        QString example;    // 示例值
        bool deprecated;    // 是否废弃
        QString deprecationMessage; // 废弃说明
    };
    QList<Parameter> parameters; // 参数列表
    
    struct ReturnValue {
        QString description;    // 返回值描述
        TypeDescriptor type;    // 类型描述
        QJsonObject schema;     // JSON Schema
        QString example;        // 示例
    };
    ReturnValue returnValue; // 返回值定义
    
    QList<QString> examples; // 完整示例
    bool deprecated;        // 是否废弃
    QString deprecationMessage;
    QString since;          // 引入版本
};

// 配置元数据
struct ConfigMetadata {
    QString name;           // 配置项名称
    QString displayName;    // 显示名称
    QString description;    // 配置描述
    QString category;       // 分类
    TypeDescriptor type;    // 类型描述
    QJsonValue defaultValue;// 默认值
    bool required;          // 是否必填
    QList<Constraint> constraints; // 约束条件
    QJsonObject uiHints;    // UI 提示
    QString example;        // 示例值
    
    struct Dependency {
        QString configName; // 依赖的配置项
        QString condition;  // 条件表达式
    };
    QList<Dependency> dependencies; // 配置依赖
    
    bool restartRequired;   // 修改后是否需要重启
    bool deprecated;
    QString deprecationMessage;
    QString since;
};

// 类型描述符
struct TypeDescriptor {
    enum BasicType {
        String,
        Integer,
        Double,
        Boolean,
        Null,
        Array,
        Object,
        Enum,
        Custom
    };
    
    BasicType basicType;
    
    // Array 类型扩展
    struct ArrayType {
        std::shared_ptr<TypeDescriptor> itemType;
        int minItems = -1;
        int maxItems = -1;
        bool uniqueItems = false;
    };
    std::shared_ptr<ArrayType> arrayType;
    
    // Object 类型扩展
    struct ObjectType {
        QMap<QString, TypeDescriptor> properties;
        QStringList required;
        bool additionalProperties = true;
    };
    std::shared_ptr<ObjectType> objectType;
    
    // Enum 类型扩展
    struct EnumType {
        QList<QJsonValue> values;
        QMap<QJsonValue, QString> descriptions;
    };
    std::shared_ptr<EnumType> enumType;
    
    // Custom 类型扩展
    struct CustomType {
        QString typeName;
        QJsonObject schema;
    };
    std::shared_ptr<CustomType> customType;
};

// 约束条件
struct Constraint {
    enum Type {
        // 数值约束
        Minimum,
        Maximum,
        ExclusiveMinimum,
        ExclusiveMaximum,
        MultipleOf,
        
        // 字符串约束
        MinLength,
        MaxLength,
        Pattern,
        Format,
        
        // 数组约束
        MinItems,
        MaxItems,
        UniqueItems,
        
        // 对象约束
        MinProperties,
        MaxProperties,
        
        // 自定义约束
        Custom
    };
    
    Type type;
    QJsonValue value;
    QString errorMessage; // 自定义错误消息
    
    // 自定义约束
    struct CustomConstraint {
        QString name;
        QString expression; // 表达式 (支持简单的条件语法)
        QString errorMessage;
    };
    std::shared_ptr<CustomConstraint> customConstraint;
};
```

#### 4.1.2 元数据定义 API

提供两种定义方式:

**方式 1: C++ 宏定义 (简洁,适合简单场景)**

```cpp
// 定义命令
STDIOLINK_COMMAND(ping, "测试连接")
    .category("system")
    .parameter("message", Type::String, "消息内容")
        .required(false)
        .defaultValue("Hello")
        .constraint(Constraint::MaxLength, 100)
        .uiHint("placeholder", "输入消息")
    .returns(Type::Object, "响应对象")
        .example(R"({"echo": "Hello", "timestamp": 1234567890})")
    .example(R"(--cmd=ping --message="Hello World")")
.end();

// 定义配置
STDIOLINK_CONFIG(timeout, "超时时间", Type::Integer)
    .defaultValue(5000)
    .constraint(Constraint::Minimum, 100)
    .constraint(Constraint::Maximum, 60000)
    .unit("毫秒")
    .uiHint("widget", "slider")
.end();
```

**方式 2: Builder 模式 (灵活,适合复杂场景)**

```cpp
CommandMetadata buildPingCommand() {
    return CommandBuilder("ping")
        .displayName("测试连接")
        .description("向 Driver 发送 ping 请求以测试连接")
        .category("system")
        .tag("diagnostic")
        .parameter(
            ParameterBuilder("message")
                .type(Type::String)
                .description("要发送的消息内容")
                .defaultValue("Hello")
                .constraint(Constraint::MaxLength, 100)
                .uiHint("placeholder", "输入消息")
                .example("Hello World")
                .build()
        )
        .returnValue(
            ReturnValueBuilder()
                .type(Type::Object)
                .description("包含回显消息和时间戳的对象")
                .schema(R"({
                    "type": "object",
                    "properties": {
                        "echo": {"type": "string"},
                        "timestamp": {"type": "integer"}
                    }
                })")
                .example(R"({"echo": "Hello", "timestamp": 1234567890})")
                .build()
        )
        .example(R"(--cmd=ping --message="Hello World")")
        .since("1.0.0")
        .build();
}
```

#### 4.1.3 元数据注册

```cpp
class MetadataRegistry {
public:
    static MetadataRegistry& instance();
    
    // 注册元数据
    void registerDriver(const DriverInfo& info);
    void registerCommand(const CommandMetadata& cmd);
    void registerConfig(const ConfigMetadata& cfg);
    
    // 查询元数据
    DriverInfo driverInfo() const;
    QList<CommandMetadata> commands() const;
    QList<CommandMetadata> commandsByCategory(const QString& category) const;
    CommandMetadata command(const QString& name) const;
    
    QList<ConfigMetadata> configs() const;
    QList<ConfigMetadata> configsByCategory(const QString& category) const;
    ConfigMetadata config(const QString& name) const;
    
    // 序列化
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);
    
    // 验证
    bool validateCommand(const QString& cmd, const QJsonObject& params, 
                        QStringList& errors) const;
    bool validateConfig(const QString& name, const QJsonValue& value,
                       QStringList& errors) const;
    
private:
    MetadataRegistry() = default;
    
    DriverInfo m_driverInfo;
    QMap<QString, CommandMetadata> m_commands;
    QMap<QString, ConfigMetadata> m_configs;
};

// 自动注册宏
#define STDIOLINK_REGISTER_COMMAND(name, metadata) \
    namespace { \
        struct CommandRegistrar_##name { \
            CommandRegistrar_##name() { \
                MetadataRegistry::instance().registerCommand(metadata); \
            } \
        }; \
        static CommandRegistrar_##name g_cmdReg_##name; \
    }

#define STDIOLINK_REGISTER_CONFIG(name, metadata) \
    namespace { \
        struct ConfigRegistrar_##name { \
            ConfigRegistrar_##name() { \
                MetadataRegistry::instance().registerConfig(metadata); \
            } \
        }; \
        static ConfigRegistrar_##name g_cfgReg_##name; \
    }
```

### 4.2 命令路由与验证

#### 4.2.1 命令路由器

```cpp
class CommandRouter {
public:
    CommandRouter(ICommandHandler* handler);
    
    // 处理请求 (带验证)
    void route(const QString& cmd, const QJsonValue& data, IResponder& resp);
    
    // 设置是否严格模式 (未知参数是否报错)
    void setStrictMode(bool strict);
    
private:
    ICommandHandler* m_handler;
    bool m_strictMode = true;
    
    // 验证参数
    bool validateParameters(const CommandMetadata& meta,
                           const QJsonObject& params,
                           QStringList& errors);
    
    // 类型转换
    QJsonObject convertParameters(const CommandMetadata& meta,
                                  const QJsonObject& params);
};
```

#### 4.2.2 参数验证器

```cpp
class ParameterValidator {
public:
    // 验证单个参数
    static bool validate(const TypeDescriptor& type,
                        const QList<Constraint>& constraints,
                        const QJsonValue& value,
                        QStringList& errors);
    
    // 验证对象的所有参数
    static bool validateObject(const QMap<QString, TypeDescriptor>& schema,
                              const QJsonObject& obj,
                              QStringList& errors);
    
private:
    static bool checkType(const TypeDescriptor& type, const QJsonValue& value);
    static bool checkConstraint(const Constraint& constraint, const QJsonValue& value);
    static QString formatError(const QString& field, const QString& message);
};
```

### 4.3 内建命令

框架提供以下内建命令(所有 Driver 自动支持):

#### 4.3.1 introspect - 获取元数据

**请求**:

```json
{
    "cmd": "introspect",
    "data": {
        "filter": {
            "commands": true,      // 是否包含命令
            "configs": true,       // 是否包含配置
            "category": "system"   // 按分类筛选 (可选)
        },
        "format": "json"           // 输出格式: json | markdown | html
    }
}
```

**响应**:

```json
{
    "status": "done",
    "code": 0
}
{
    "driver": {
        "name": "example-driver",
        "version": "1.2.3",
        "description": "示例驱动",
        "protocolVersion": {"major": 1, "minor": 0}
    },
    "commands": [
        {
            "name": "ping",
            "displayName": "测试连接",
            "description": "...",
            "parameters": [...],
            "returnValue": {...}
        }
    ],
    "configs": [...]
}
```

#### 4.3.2 validate - 验证请求

**请求**:

```json
{
    "cmd": "validate",
    "data": {
        "command": "ping",
        "parameters": {
            "message": "Hello"
        }
    }
}
```

**响应**:

```json
{
    "status": "done",
    "code": 0
}
{
    "valid": true,
    "errors": []
}
```

或:

```json
{
    "status": "done",
    "code": 0
}
{
    "valid": false,
    "errors": [
        "Parameter 'timeout' must be >= 100",
        "Parameter 'mode' is required"
    ],
    "suggestions": [
        "Did you mean 'message' instead of 'msg'?"
    ]
}
```

#### 4.3.3 get-config - 获取配置

**请求**:

```json
{
    "cmd": "get-config",
    "data": {
        "names": ["timeout", "retries"]  // 可选,不指定则返回全部
    }
}
```

**响应**:

```json
{
    "status": "done",
    "code": 0
}
{
    "configs": {
        "timeout": 5000,
        "retries": 3
    }
}
```

#### 4.3.4 set-config - 设置配置

**请求**:

```json
{
    "cmd": "set-config",
    "data": {
        "configs": {
            "timeout": 10000,
            "retries": 5
        },
        "validate": true  // 是否验证
    }
}
```

**响应**:

```json
{
    "status": "done",
    "code": 0
}
{
    "updated": ["timeout", "retries"],
    "restartRequired": false
}
```

### 4.4 Host 侧 API

#### 4.4.1 元数据客户端

```cpp
class MetadataClient {
public:
    MetadataClient(Driver* driver);
    
    // 获取元数据 (自动缓存)
    Task fetchMetadata();
    
    // 同步获取 (阻塞)
    DriverMetadata getMetadata(int timeoutMs = 5000);
    
    // 查询
    DriverInfo driverInfo() const;
    QList<CommandMetadata> commands() const;
    CommandMetadata command(const QString& name) const;
    QList<ConfigMetadata> configs() const;
    
    // 验证请求
    bool validateRequest(const QString& cmd, const QJsonObject& params,
                        QStringList& errors);
    
    // 获取配置
    Task getConfig(const QStringList& names = {});
    QJsonObject getConfigSync(const QStringList& names = {}, int timeoutMs = 5000);
    
    // 设置配置
    Task setConfig(const QJsonObject& configs, bool validate = true);
    bool setConfigSync(const QJsonObject& configs, bool validate = true, 
                      int timeoutMs = 5000);
    
    // 清除缓存
    void clearCache();
    
private:
    Driver* m_driver;
    std::shared_ptr<DriverMetadata> m_cache;
    QDateTime m_cacheTime;
};
```

#### 4.4.2 请求构建器

```cpp
class RequestBuilder {
public:
    RequestBuilder(MetadataClient* client, const QString& cmd);
    
    // 设置参数 (自动类型转换)
    RequestBuilder& param(const QString& name, const QJsonValue& value);
    RequestBuilder& params(const QJsonObject& params);
    
    // 验证
    bool validate(QStringList& errors);
    
    // 构建并发送
    Task send();
    
    // 同步发送
    Message sendSync(int timeoutMs = 5000);
    
private:
    MetadataClient* m_client;
    QString m_cmd;
    QJsonObject m_params;
};

// 使用示例
MetadataClient client(&driver);
auto task = RequestBuilder(&client, "ping")
    .param("message", "Hello")
    .param("timeout", 1000)
    .send();
```

### 4.5 UI 自动生成

#### 4.5.1 配置界面生成器

```cpp
class ConfigUIGenerator {
public:
    // 生成 Qt Widgets 配置界面
    QWidget* generateWidget(const QList<ConfigMetadata>& configs,
                           QObject* parent = nullptr);
    
    // 生成 QML 配置界面
    QString generateQML(const QList<ConfigMetadata>& configs);
    
    // 自定义控件工厂
    void setWidgetFactory(const QString& typeName, WidgetFactory* factory);
    
    // 获取配置值
    QJsonObject getValues(QWidget* widget);
    
    // 设置配置值
    void setValues(QWidget* widget, const QJsonObject& values);
    
    // 验证
    bool validate(QWidget* widget, QStringList& errors);
    
private:
    QMap<QString, WidgetFactory*> m_factories;
    
    QWidget* createWidget(const ConfigMetadata& config);
    QWidget* createStringWidget(const ConfigMetadata& config);
    QWidget* createIntegerWidget(const ConfigMetadata& config);
    QWidget* createBooleanWidget(const ConfigMetadata& config);
    QWidget* createEnumWidget(const ConfigMetadata& config);
    QWidget* createArrayWidget(const ConfigMetadata& config);
    QWidget* createObjectWidget(const ConfigMetadata& config);
};

// 控件工厂接口
class WidgetFactory {
public:
    virtual ~WidgetFactory() = default;
    virtual QWidget* create(const ConfigMetadata& config, QWidget* parent) = 0;
    virtual QJsonValue getValue(QWidget* widget) = 0;
    virtual void setValue(QWidget* widget, const QJsonValue& value) = 0;
    virtual bool validate(QWidget* widget, QStringList& errors) = 0;
};
```

**使用示例**:

```cpp
// 获取元数据
MetadataClient client(&driver);
auto configs = client.configs();

// 生成配置界面
ConfigUIGenerator generator;
QWidget* configWidget = generator.generateWidget(configs);

// 显示界面
QDialog dialog;
dialog.setLayout(new QVBoxLayout);
dialog.layout()->addWidget(configWidget);

QPushButton* saveBtn = new QPushButton("保存");
connect(saveBtn, &QPushButton::clicked, [&]() {
    QStringList errors;
    if (generator.validate(configWidget, errors)) {
        auto values = generator.getValues(configWidget);
        client.setConfigSync(values);
        dialog.accept();
    } else {
        QMessageBox::warning(&dialog, "验证失败", errors.join("\n"));
    }
});
dialog.layout()->addWidget(saveBtn);

dialog.exec();
```

#### 4.5.2 命令界面生成器

```cpp
class CommandUIGenerator {
public:
    // 生成命令调用界面
    QWidget* generateWidget(const CommandMetadata& command,
                           Driver* driver,
                           QObject* parent = nullptr);
    
    // 生成 QML 界面
    QString generateQML(const CommandMetadata& command);
    
    // 自定义控件工厂
    void setWidgetFactory(const QString& typeName, WidgetFactory* factory);
    
private:
    QMap<QString, WidgetFactory*> m_factories;
};
```

### 4.6 文档生成

#### 4.6.1 文档生成器

```cpp
class DocGenerator {
public:
    // 生成 Markdown 文档
    QString generateMarkdown(const DriverMetadata& metadata,
                            const DocOptions& options = {});
    
    // 生成 HTML 文档
    QString generateHTML(const DriverMetadata& metadata,
                        const DocOptions& options = {});
    
    // 生成 OpenAPI 规范
    QJsonObject generateOpenAPI(const DriverMetadata& metadata,
                               const DocOptions& options = {});
    
    // 设置模板
    void setTemplate(const QString& format, const QString& templatePath);
    
private:
    QMap<QString, QString> m_templates;
};

struct DocOptions {
    QString title;
    QString description;
    QString version;
    QString baseUrl;
    bool includeExamples = true;
    bool includeDeprecated = false;
    QString language = "zh-CN";
};
```

**生成的 Markdown 文档示例**:

```markdown
# Example Driver

**版本**: 1.2.3  
**作者**: John Doe  
**协议版本**: 1.0

## 描述

这是一个示例驱动程序,用于演示元数据自描述架构。

## 命令

### ping - 测试连接

**分类**: system  
**引入版本**: 1.0.0

测试与 Driver 的连接状态。

#### 参数

| 参数名 | 类型 | 必填 | 默认值 | 描述 |
|--------|------|------|--------|------|
| message | string | 否 | "Hello" | 要发送的消息内容 |
| timeout | integer | 否 | 5000 | 超时时间(毫秒) |

**约束**:
- message: 最大长度 100
- timeout: 范围 [100, 60000]

#### 返回值

返回包含回显消息和时间戳的对象。

**类型**: object

**Schema**:
```json
{
    "type": "object",
    "properties": {
        "echo": {"type": "string"},
        "timestamp": {"type": "integer"}
    }
}
```

#### 示例

**命令行**:
```bash
./driver --cmd=ping --message="Hello World" --timeout=3000
```

**响应**:
```json
{"status": "done", "code": 0}
{"echo": "Hello World", "timestamp": 1234567890}
```

---

### list-files - 列出文件

...

## 配置

### timeout - 超时时间

**分类**: general  
**类型**: integer  
**默认值**: 5000  
**单位**: 毫秒

命令执行的默认超时时间。

**约束**:
- 最小值: 100
- 最大值: 60000

**UI 提示**:
- widget: slider

---

## 示例

### 基本用法

```bash
# 测试连接
./driver --cmd=ping

# 列出文件
./driver --cmd=list-files --path=/home --recursive=true

# 设置配置
./driver --cmd=set-config --timeout=10000
```
```

---

## 5. 实现规范

### 5.1 目录结构

```
stdiolink/
├── metadata/
│   ├── types.hpp              # 类型系统
│   ├── command.hpp            # 命令元数据
│   ├── config.hpp             # 配置元数据
│   ├── constraint.hpp         # 约束条件
│   ├── registry.hpp           # 元数据注册器
│   ├── builder.hpp            # Builder 模式 API
│   └── macros.hpp             # 宏定义 API
├── driver/
│   ├── router.hpp             # 命令路由器
│   ├── validator.hpp          # 参数验证器
│   └── builtin_commands.hpp   # 内建命令
├── host/
│   ├── metadata_client.hpp    # 元数据客户端
│   ├── request_builder.hpp    # 请求构建器
│   ├── ui_generator.hpp       # UI 生成器
│   └── doc_generator.hpp      # 文档生成器
├── utils/
│   ├── json_schema.hpp        # JSON Schema 工具
│   └── type_converter.hpp     # 类型转换器
└── examples/
    ├── simple_driver/         # 简单示例
    ├── complex_driver/        # 复杂示例
    └── ui_demo/               # UI 生成示例
```

### 5.2 编码规范

1. **命名约定**:
   - 类名: PascalCase
   - 函数名: camelCase
   - 变量名: camelCase
   - 常量名: UPPER_SNAKE_CASE
   - 宏名: STDIOLINK_PREFIX_

2. **文件组织**:
   - 每个类一个 .hpp 文件
   - 复杂实现放在 .cpp 文件
   - 模板实现放在 .hpp 文件

3. **注释规范**:
   - 使用 Doxygen 风格注释
   - 公开 API 必须有详细注释
   - 复杂逻辑必须有实现注释

4. **错误处理**:
   - 使用 QStringList 收集错误信息
   - 提供清晰的错误消息
   - 包含错误位置和建议修正

### 5.3 性能优化

1. **元数据缓存**:
   - Host 侧缓存元数据,避免重复查询
   - 支持缓存失效机制

2. **延迟加载**:
   - 元数据按需加载
   - 支持部分加载(只加载命令或配置)

3. **序列化优化**:
   - 使用紧凑的 JSON 格式
   - 支持压缩传输(可选)

4. **验证优化**:
   - 编译期类型检查
   - 运行期快速路径(常见情况)

### 5.4 测试要求

1. **单元测试**:
   - 所有公开 API 必须有单元测试
   - 覆盖率 > 80%
   - 使用 Qt Test 框架

2. **集成测试**:
   - 完整的 Host-Driver 交互测试
   - 验证元数据序列化/反序列化
   - 验证 UI 生成

3. **性能测试**:
   - 元数据解析性能
   - introspect 命令性能
   - UI 生成性能

---

## 6. 开发流程

### 6.1 Driver 开发流程

```
1. 定义 Driver 信息
   ├─ 名称、版本、描述
   └─ 协议版本
   
2. 定义配置元数据
   ├─ 配置项名称、类型
   ├─ 默认值、约束
   └─ UI 提示
   
3. 定义命令元数据
   ├─ 命令名称、描述
   ├─ 参数列表(类型、约束)
   ├─ 返回值定义
   └─ 示例
   
4. 注册元数据
   └─ 使用宏或 Builder 注册
   
5. 实现命令处理器
   ├─ 继承 ICommandHandler
   └─ 实现 handle() 方法
   
6. 配置命令路由
   ├─ 创建 CommandRouter
   └─ 设置验证模式
   
7. 测试
   ├─ 单元测试
   ├─ 手动测试
   └─ 生成文档验证
   
8. 发布
   ├─ 更新版本号
   ├─ 生成文档
   └─ 打包发布
```

### 6.2 开发示例

**步骤 1: 定义 Driver 信息**

```cpp
// my_driver.cpp
#include <stdiolink/metadata/registry.hpp>
#include <stdiolink/metadata/builder.hpp>

using namespace stdiolink;

// 注册 Driver 信息
STDIOLINK_REGISTER_DRIVER(
    DriverInfoBuilder()
        .name("my-awesome-driver")
        .version("1.0.0")
        .description("我的超棒驱动")
        .author("张三")
        .license("MIT")
        .tag("file-system")
        .tag("utility")
        .protocolVersion(1, 0)
        .build()
);
```

**步骤 2: 定义配置元数据**

```cpp
// 配置: 超时时间
STDIOLINK_REGISTER_CONFIG(
    ConfigBuilder("timeout")
        .displayName("超时时间")
        .description("命令执行的默认超时时间")
        .type(Type::Integer)
        .defaultValue(5000)
        .category("general")
        .constraint(Constraint::Minimum, 100)
        .constraint(Constraint::Maximum, 60000)
        .uiHint("widget", "slider")
        .uiHint("unit", "毫秒")
        .build()
);

// 配置: 日志级别
STDIOLINK_REGISTER_CONFIG(
    ConfigBuilder("log_level")
        .displayName("日志级别")
        .type(Type::Enum)
        .enumValues({"DEBUG", "INFO", "WARNING", "ERROR"})
        .enumDescriptions({
            {"DEBUG", "调试信息"},
            {"INFO", "一般信息"},
            {"WARNING", "警告"},
            {"ERROR", "错误"}
        })
        .defaultValue("INFO")
        .category("general")
        .build()
);
```

**步骤 3: 定义命令元数据**

```cpp
// 命令: list-files
STDIOLINK_REGISTER_COMMAND(
    CommandBuilder("list-files")
        .displayName("列出文件")
        .description("列出指定目录下的文件和子目录")
        .category("file-system")
        .tag("read-only")
        
        // 参数: path
        .parameter(
            ParameterBuilder("path")
                .type(Type::String)
                .description("要列出的目录路径")
                .required(true)
                .constraint(Constraint::MinLength, 1)
                .uiHint("placeholder", "/path/to/directory")
                .example("/home/user/documents")
                .build()
        )
        
        // 参数: recursive
        .parameter(
            ParameterBuilder("recursive")
                .type(Type::Boolean)
                .description("是否递归列出子目录")
                .defaultValue(false)
                .build()
        )
        
        // 参数: filter
        .parameter(
            ParameterBuilder("filter")
                .type(Type::String)
                .description("文件名过滤模式(支持通配符)")
                .required(false)
                .constraint(Constraint::Pattern, R"(^[\w\*\?\.]+$)")
                .example("*.txt")
                .build()
        )
        
        // 返回值
        .returnValue(
            ReturnValueBuilder()
                .type(Type::Array)
                .description("文件和目录列表")
                .schema(R"({
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "name": {"type": "string"},
                            "type": {"type": "string", "enum": ["file", "directory"]},
                            "size": {"type": "integer"},
                            "modified": {"type": "integer"}
                        }
                    }
                })")
                .build()
        )
        
        // 示例
        .example(R"(
            // 列出 /home 目录
            --cmd=list-files --path=/home
            
            // 递归列出所有 .txt 文件
            --cmd=list-files --path=/home --recursive=true --filter=*.txt
        )")
        
        .since("1.0.0")
        .build()
);
```

**步骤 4: 实现命令处理器**

```cpp
class MyCommandHandler : public ICommandHandler {
public:
    void handle(const QString& cmd, const QJsonValue& data, IResponder& resp) override {
        if (cmd == "list-files") {
            handleListFiles(data.toObject(), resp);
        } else {
            resp.error(404, QJsonObject{
                {"message", QString("Unknown command: %1").arg(cmd)}
            });
        }
    }
    
private:
    void handleListFiles(const QJsonObject& params, IResponder& resp) {
        QString path = params["path"].toString();
        bool recursive = params["recursive"].toBool(false);
        QString filter = params["filter"].toString("*");
        
        // 执行文件列表操作
        QJsonArray files;
        // ... 实现逻辑 ...
        
        // 发送进度事件
        resp.event(0, QJsonObject{
            {"progress", 50},
            {"message", "正在扫描..."}
        });
        
        // 发送最终结果
        resp.done(0, files);
    }
};
```

**步骤 5: 配置并运行**

```cpp
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    // 创建命令处理器
    MyCommandHandler handler;
    
    // 创建命令路由器
    CommandRouter router(&handler);
    router.setStrictMode(true); // 严格模式:拒绝未知参数
    
    // 创建 Driver 核心
    DriverCore core;
    core.setProfile(DriverCore::Profile::KeepAlive);
    core.setHandler(&router);
    
    // 运行
    return core.run();
}
```

### 6.3 Host 使用流程

```cpp
// 1. 启动 Driver
Driver driver;
driver.start("./my-awesome-driver");

// 2. 获取元数据
MetadataClient client(&driver);
auto metadata = client.getMetadata();

qDebug() << "Driver:" << metadata.driverInfo.name;
qDebug() << "Commands:" << metadata.commands.size();

// 3. 列出所有命令
for (const auto& cmd : metadata.commands) {
    qDebug() << "-" << cmd.name << ":" << cmd.description;
}

// 4. 生成配置界面
ConfigUIGenerator uiGen;
QWidget* configWidget = uiGen.generateWidget(metadata.configs);

QDialog dialog;
// ... 显示配置界面 ...

// 5. 构建并发送请求
auto task = RequestBuilder(&client, "list-files")
    .param("path", "/home")
    .param("recursive", true)
    .send();

// 6. 处理结果
Message msg;
while (task.waitNext(msg)) {
    if (msg.status == "event") {
        qDebug() << "Progress:" << msg.payload;
    } else if (msg.status == "done") {
        qDebug() << "Result:" << msg.payload;
    }
}

// 7. 生成文档
DocGenerator docGen;
QString markdown = docGen.generateMarkdown(metadata);
QFile file("driver_docs.md");
file.open(QIODevice::WriteOnly);
file.write(markdown.toUtf8());
```

---

## 7. 测试策略

### 7.1 单元测试

**测试范围**:

1. 元数据定义与注册
2. 类型系统
3. 约束验证
4. 命令路由
5. 参数转换
6. UI 生成
7. 文档生成

**测试框架**: Qt Test

**测试示例**:

```cpp
class MetadataTest : public QObject {
    Q_OBJECT
    
private slots:
    void testCommandBuilder() {
        auto cmd = CommandBuilder("test")
            .description("Test command")
            .parameter(
                ParameterBuilder("count")
                    .type(Type::Integer)
                    .defaultValue(10)
                    .build()
            )
            .build();
        
        QCOMPARE(cmd.name, QString("test"));
        QCOMPARE(cmd.parameters.size(), 1);
        QCOMPARE(cmd.parameters[0].name, QString("count"));
        QCOMPARE(cmd.parameters[0].defaultValue.toInt(), 10);
    }
    
    void testParameterValidation() {
        ParameterMetadata param;
        param.type = Type::Integer;
        param.constraints << Constraint{Constraint::Minimum, 10};
        param.constraints << Constraint{Constraint::Maximum, 100};
        
        QStringList errors;
        
        // 有效值
        QVERIFY(ParameterValidator::validate(param.type, param.constraints, 50, errors));
        QVERIFY(errors.isEmpty());
        
        // 无效值: 太小
        errors.clear();
        QVERIFY(!ParameterValidator::validate(param.type, param.constraints, 5, errors));
        QVERIFY(!errors.isEmpty());
        
        // 无效值: 太大
        errors.clear();
        QVERIFY(!ParameterValidator::validate(param.type, param.constraints, 150, errors));
        QVERIFY(!errors.isEmpty());
    }
};
```

### 7.2 集成测试

**测试场景**:

1. Host-Driver 完整交互流程
2. introspect 命令
3. validate 命令
4. get-config / set-config 命令
5. 自定义命令调用
6. 错误处理

**测试示例**:

```cpp
class IntegrationTest : public QObject {
    Q_OBJECT
    
private slots:
    void testIntrospect() {
        // 启动测试 Driver
        Driver driver;
        QVERIFY(driver.start("./test-driver"));
        
        // 发送 introspect 请求
        auto task = driver.request("introspect");
        
        Message msg;
        QVERIFY(task.waitNext(msg, 5000));
        QCOMPARE(msg.status, QString("done"));
        
        // 验证元数据结构
        auto data = msg.payload.toObject();
        QVERIFY(data.contains("driver"));
        QVERIFY(data.contains("commands"));
        QVERIFY(data.contains("configs"));
        
        auto cmds = data["commands"].toArray();
        QVERIFY(cmds.size() > 0);
    }
    
    void testCommandExecution() {
        Driver driver;
        QVERIFY(driver.start("./test-driver"));
        
        // 构建请求
        QJsonObject params;
        params["message"] = "Hello";
        
        auto task = driver.request("ping", params);
        
        Message msg;
        QVERIFY(task.waitNext(msg, 5000));
        QCOMPARE(msg.status, QString("done"));
        
        auto result = msg.payload.toObject();
        QCOMPARE(result["echo"].toString(), QString("Hello"));
    }
};
```

### 7.3 性能测试

**测试指标**:

1. 元数据解析时间 < 10ms
2. introspect 命令响应时间 < 100ms
3. 参数验证时间 < 1ms (单个参数)
4. UI 生成时间 < 500ms (10个配置项)
5. 文档生成时间 < 200ms

**测试工具**: QElapsedTimer, QTest::qBenchmark

---

## 8. 示例场景

### 8.1 场景 1: 文件管理 Driver

**需求**: 开发一个文件管理 Driver,支持文件浏览、复制、移动、删除等操作

**元数据定义**:

```cpp
// 配置
STDIOLINK_REGISTER_CONFIG(
    ConfigBuilder("base_path")
        .displayName("基础路径")
        .description("文件操作的根目录")
        .type(Type::String)
        .defaultValue("/home")
        .constraint(Constraint::MinLength, 1)
        .uiHint("widget", "directory-picker")
        .build()
);

// 命令: copy-file
STDIOLINK_REGISTER_COMMAND(
    CommandBuilder("copy-file")
        .displayName("复制文件")
        .category("file-operations")
        .parameter(
            ParameterBuilder("source")
                .type(Type::String)
                .description("源文件路径")
                .required(true)
                .build()
        )
        .parameter(
            ParameterBuilder("destination")
                .type(Type::String)
                .description("目标路径")
                .required(true)
                .build()
        )
        .parameter(
            ParameterBuilder("overwrite")
                .type(Type::Boolean)
                .description("是否覆盖已存在的文件")
                .defaultValue(false)
                .build()
        )
        .returnValue(
            ReturnValueBuilder()
                .type(Type::Object)
                .description("复制结果")
                .build()
        )
        .build()
);
```

### 8.2 场景 2: 数据库 Driver

**需求**: 开发一个数据库 Driver,支持查询、插入、更新、删除操作

**元数据定义**:

```cpp
// 配置: 数据库连接
STDIOLINK_REGISTER_CONFIG(
    ConfigBuilder("connection")
        .displayName("数据库连接")
        .type(Type::Object)
        .objectProperties({
            {"host", Type::String},
            {"port", Type::Integer},
            {"database", Type::String},
            {"username", Type::String},
            {"password", Type::String}
        })
        .required({"host", "database"})
        .build()
);

// 命令: query
STDIOLINK_REGISTER_COMMAND(
    CommandBuilder("query")
        .displayName("执行查询")
        .category("database")
        .parameter(
            ParameterBuilder("sql")
                .type(Type::String)
                .description("SQL 查询语句")
                .required(true)
                .constraint(Constraint::MinLength, 1)
                .build()
        )
        .parameter(
            ParameterBuilder("params")
                .type(Type::Array)
                .description("查询参数")
                .required(false)
                .build()
        )
        .returnValue(
            ReturnValueBuilder()
                .type(Type::Array)
                .description("查询结果集")
                .build()
        )
        .build()
);
```

### 8.3 场景 3: 图像处理 Driver

**需求**: 开发一个图像处理 Driver,支持格式转换、缩放、滤镜等操作

**元数据定义**:

```cpp
// 命令: resize-image
STDIOLINK_REGISTER_COMMAND(
    CommandBuilder("resize-image")
        .displayName("调整图像大小")
        .category("image-processing")
        .parameter(
            ParameterBuilder("input")
                .type(Type::String)
                .description("输入图像路径")
                .required(true)
                .uiHint("widget", "file-picker")
                .uiHint("filter", "Images (*.png *.jpg *.jpeg)")
                .build()
        )
        .parameter(
            ParameterBuilder("output")
                .type(Type::String)
                .description("输出图像路径")
                .required(true)
                .build()
        )
        .parameter(
            ParameterBuilder("width")
                .type(Type::Integer)
                .description("目标宽度(像素)")
                .required(true)
                .constraint(Constraint::Minimum, 1)
                .constraint(Constraint::Maximum, 10000)
                .build()
        )
        .parameter(
            ParameterBuilder("height")
                .type(Type::Integer)
                .description("目标高度(像素)")
                .required(true)
                .constraint(Constraint::Minimum, 1)
                .constraint(Constraint::Maximum, 10000)
                .build()
        )
        .parameter(
            ParameterBuilder("mode")
                .type(Type::Enum)
                .enumValues({"stretch", "fit", "fill", "crop"})
                .enumDescriptions({
                    {"stretch", "拉伸填充"},
                    {"fit", "适应(保持比例)"},
                    {"fill", "填充(可能裁剪)"},
                    {"crop", "居中裁剪"}
                })
                .defaultValue("fit")
                .build()
        )
        .build()
);
```

---

## 9. 兼容性与迁移

### 9.1 向后兼容策略

1. **协议版本化**:
   - 主版本号变更表示不兼容改动
   - 次版本号变更表示兼容性新增
   - Host 和 Driver 协商使用的版本

2. **元数据演进**:
   - 新增字段向后兼容
   - 废弃字段标记但保留
   - 使用 `deprecated` 标记废弃的命令/参数

3. **默认值策略**:
   - 新增参数必须提供默认值
   - 不改变现有参数的默认值

4. **错误处理**:
   - 未知字段忽略(非严格模式)
   - 提供详细的错误信息和建议

### 9.2 从现有 Driver 迁移

**迁移步骤**:

1. **定义元数据**:
   - 为现有命令定义元数据
   - 为现有配置定义元数据

2. **添加内建命令支持**:
   - 实现 introspect 命令
   - 实现 validate 命令
   - 实现 get-config / set-config 命令

3. **集成命令路由**:
   - 使用 CommandRouter 替代手动分发
   - 启用参数验证

4. **测试**:
   - 验证元数据正确性
   - 验证命令行为一致

5. **更新文档**:
   - 使用文档生成器生成新文档
   - 更新示例代码

**迁移示例**:

**迁移前**:

```cpp
void MyHandler::handle(const QString& cmd, const QJsonValue& data, IResponder& resp) {
    if (cmd == "ping") {
        QString msg = data.toObject()["message"].toString("Hello");
        resp.done(0, QJsonObject{{"echo", msg}});
    }
}
```

**迁移后**:

```cpp
// 1. 定义元数据
STDIOLINK_REGISTER_COMMAND(
    CommandBuilder("ping")
        .description("测试连接")
        .parameter(
            ParameterBuilder("message")
                .type(Type::String)
                .defaultValue("Hello")
                .build()
        )
        .build()
);

// 2. 实现保持不变
void MyHandler::handle(const QString& cmd, const QJsonValue& data, IResponder& resp) {
    if (cmd == "ping") {
        QString msg = data.toObject()["message"].toString("Hello");
        resp.done(0, QJsonObject{{"echo", msg}});
    }
}

// 3. 使用命令路由(可选,获得自动验证)
CommandRouter router(new MyHandler());
DriverCore core;
core.setHandler(&router);
```

---

## 10. 附录

### 10.1 术语表

| 术语 | 定义 |
|------|------|
| Driver | 驱动程序,提供特定功能的独立进程 |
| Host | 宿主程序,管理和调用 Driver 的主程序 |
| 元数据 | 描述 Driver 能力的数据(命令、参数、配置等) |
| 内建命令 | 框架自动提供的命令(introspect、validate 等) |
| 约束 | 对参数值的限制条件(范围、长度、格式等) |
| UI Hints | 用于指导 UI 生成的提示信息 |
| 类型描述符 | 描述数据类型的结构 |
| 命令路由 | 根据命令名分发到对应处理器的机制 |

### 10.2 参考资料

1. **JSON Schema**: https://json-schema.org/
2. **OpenAPI Specification**: https://swagger.io/specification/
3. **Qt Meta-Object System**: https://doc.qt.io/qt-5/metaobjects.html
4. **Semantic Versioning**: https://semver.org/

### 10.3 未来扩展

**阶段 2 扩展(可选)**:

1. **异步验证**:
   - 支持自定义异步验证器
   - 远程验证(如检查文件是否存在)

2. **依赖注入**:
   - 命令之间的依赖关系
   - 自动执行前置命令

3. **事务支持**:
   - 多命令原子执行
   - 回滚机制

4. **权限管理**:
   - 命令权限定义
   - 用户角色管理

5. **插件系统**:
   - 动态加载命令
   - 第三方扩展

6. **多语言支持**:
   - 元数据国际化
   - 错误消息本地化

7. **GraphQL 接口**:
   - 提供 GraphQL 查询接口
   - 替代 JSONL 协议(可选)

8. **Web 界面**:
   - 生成 Web 配置界面
   - 远程管理 Driver

---

## 结论

本设计文档详细描述了 stdiolink Driver 元数据自描述架构的完整需求和设计方案。通过引入声明式元数据定义、自动验证、UI 生成和文档生成能力,可以大幅提升 Driver 开发效率和用户体验,实现真正的"一次定义,处处使用"。

**核心价值**:

1. **开发效率提升**: 声明式定义减少样板代码,自动生成文档和 UI
2. **代码质量提升**: 编译期和运行期类型检查,自动参数验证
3. **用户体验提升**: 自动生成的配置界面,清晰的错误提示
4. **可维护性提升**: 代码与文档一致,版本化演进

**实施建议**:

1. 分阶段实施,先实现核心功能(元数据定义、introspect、验证)
2. 提供完善的示例和文档,降低学习曲线
3. 保持与现有框架的向后兼容
4. 持续收集反馈,迭代优化

---

**版本历史**:

- v1.0 (2026-02-04): 初始版本
