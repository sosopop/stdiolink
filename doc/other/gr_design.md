以下是根据您提供的文件内容整理出的《StdioLink Driver 自描述与接口模板化支持》开发需求与详细设计文档全文：

---

# 下一阶段开发需求与详细设计：StdioLink Driver 自描述与接口模板化支持

## 一、开发目标概述

当前StdioLink框架已经实现了可靠的Host-Driver进程间JSONL通信协议，支持单次和持续模式。但Driver的接口（支持的命令、参数结构、类型、描述、默认值等）完全是“黑盒”，Host或上层UI无法提前获知，只能通过硬编码或外部文档了解。

**改进目标：**

实现Driver自描述（self-describing）和接口模板化能力，使Driver能够在运行时向Host暴露：

1. 支持的所有命令列表  
2. 每个命令的参数JSON Schema（类型、必填、默认值、描述等）  
3. 可选的全局配置JSON Schema  
4. 可选的命令返回值Schema（用于UI显示预期输出结构）

**从而实现：**

- 自文档：自动生成API文档  
- 自动验证：Host可在发送请求前验证参数合法性  
- 动态UI生成：GUI Host可根据Schema自动生成配置界面、命令调用表单、参数输入控件（文本、数字、下拉、嵌套对象等）  
- 开发流程优化：Driver开发者先定义接口模板（Schema），再实现业务逻辑，接口先行、类型安全

---

## 二、功能需求规格

### 核心功能

#### 1. Introspect 命令
- Driver 必须支持标准命令 `meta.introspect`  
- Host 发送该命令后，Driver 返回完整的接口元数据  
- 支持部分查询（commands / config / capabilities）

#### 2. 标准化的接口描述格式
- 使用 JSON Schema Draft 07 子集描述命令参数和全局配置  
- 支持类型：null、boolean、integer、number、string、object、array  
- 支持关键词：type, description, title, default, enum, const, minLength/maxLength, minimum/maximum, required, properties, items, additionalProperties=false（推荐封闭对象）

#### 3. 全局配置支持（可选但推荐）
- Driver 可声明全局配置 Schema  
- 支持标准命令 `meta.get_config` 和 `meta.set_config`  
- 配置变更后可触发重新加载或事件通知

#### 4. 返回值描述（可选）
- 每个命令 Schema 中可包含 `response` 字段，描述预期 done payload 的 Schema

#### 5. 版本与能力声明
- Introspect 响应包含协议版本和支持的功能标志（如是否支持 config）

### 非功能需求
- 向后兼容：现有 Driver 不实现 meta 命令时，Host 应优雅降级（视作无自描述能力）  
- 性能：Introspect 响应应轻量，建议缓存 Schema（静态构建）  
- 安全性：Schema 本身不执行代码，仅描述结构；禁止 format 等可能触发验证侧效的关键词  
- 冲突避免：所有框架保留命令以 `meta.` 开头，建议用户命令避免此前缀  
- 开发者友好：提供便捷方式声明 Schema（Builder 或宏）

---

## 三、协议扩展设计

### 1. 新增保留命令

| 命令名 | 方向 | 数据 (data) | 响应 payload 示例 |
|--------|------|-------------|-------------------|
| meta.introspect | Host → Driver | 可选对象 { "targets": ["commands", "config"] } (默认全要) | { "version": "1.0", "capabilities": ["config"], "commands": { ... }, "config": { ... } } |
| meta.get_config | Host → Driver | null 或 { | 当前配置对象 (符合 config schema) |
| meta.set_config | Host → Driver | 部分或完整配置对象 | { "status": "applied" } 或错误 |
| meta.ping (可选) | Host → Driver | null | { "pong": true, "timestamp": 1234567890 } |

### 2. Introspect响应结构（JSON Schema）

```json
{
  "type": "object",
  "properties": {
    "version": { "type": "string", "const": "1.0" },
    "capabilities": {
      "type": "array",
      "items": { "type": "string", "enum": ["config", "response-schema"] }
    },
    "commands": {
      "type": "object",
      "additionalProperties": { "$ref": "#/definitions/commandSchema" }
    },
    "config": { "$ref": "#/definitions/configSchema" }
  },
  "required": ["version", "commands"],
  "definitions": {
    "commandSchema": {
      "type": "object",
      "properties": {
        "description": { "type": "string" },
        "parameters": { "$ref": "#/definitions/paramSchema" },
        "response": { "$ref": "#/definitions/paramSchema" }
      },
      "required": ["parameters"]
    },
    "paramSchema": {
      "type": "object",
      "properties": {
        "type": { "type": "string", "enum": ["object", "array", "string", "number", "integer", "boolean", "null"] },
        "description": { "type": "string" },
        "title": { "type": "string" },
        "default": {},
        "enum": { "type": "array" },
        "properties": { "type": "object" },
        "required": { "type": "array", "items": { "type": "string" } },
        "items": { "$ref": "#/definitions/paramSchema" },
        "minimum": { "type": "number" },
        "maximum": { "type": "number" },
        "minLength": { "type": "integer" },
        "maxLength": { "type": "integer" },
        "additionalProperties": { "type": "boolean" }
      },
      "required": ["type"]
    },
    "configSchema": { "$ref": "#/definitions/paramSchema" }
  }
}
```

---

## 四、API与实现设计

### 1. 新增核心类（仍在单头文件内）

```cpp
struct CommandSchema {
    QString name;
    QString description;
    QJsonObject parameterSchema; // 符合 paramSchema
    QJsonObject responseSchema; // 可选
};

class CommandRegistry {
public:
    void registerCommand(const CommandSchema& schema);
    QJsonObject getCommandsSchema() const; // 返回 {"cmd_name": full_command_schema_object }
private:
    QMap<QString, CommandSchema> m_commands;
};
```

### 2. Schema Builder（开发者友好工具）

提供 fluent-style builder，降低手动构造 JSON 的负担：

```cpp
class SchemaBuilder {
public:
    SchemaBuilder& object();
    SchemaBuilder& array();
    SchemaBuilder& string();
    SchemaBuilder& integer();
    SchemaBuilder& number();
    SchemaBuilder& boolean();
    SchemaBuilder& nullType();
    SchemaBuilder& description(const QString& desc);
    SchemaBuilder& title(const QString& t);
    SchemaBuilder& defaultValue(const QJsonValue& val);
    SchemaBuilder& enumValues(const QJsonArray& values);
    SchemaBuilder& property(const QString& name, SchemaBuilder&& child, bool required = false);
    SchemaBuilder& required(const QString& name);
    SchemaBuilder& items(SchemaBuilder&& child); // for array
    SchemaBuilder& minLength(int n);
    SchemaBuilder& maxLength(int n);
    SchemaBuilder& minimum(double n);
    SchemaBuilder& maximum(double n);
    SchemaBuilder& additionalProperties(bool allowed);
    QJsonObject build() const;
private:
    QJsonObject m_obj;
    QList<QString> m_required;
};
```

### 3. Driver 侧实现要求

推荐模式：使用全局静态 Registry

```cpp
// 在 Driver 实现文件中
static void registerMyDriverCommands(stdiolink::CommandRegistry& reg) {
    using SB = stdiolink::SchemaBuilder;
    reg.registerCommand({
        "analyze_image",
        "分析图像并返回结果",
        SB()
            .object()
            .property("path", SB().string().description("图片路径"))
            .property("threshold", SB().number().minimum(0).maximum(1).defaultValue(0.5))
            .build(),
        SB()
            .object()
            .property("result", SB().string().description("分析结果"))
            .build()
    });
    // 更多命令...
}

// 在 Driver 的 main 或初始化处调用一次
void initDriver() {
    static bool initialized = false;
    if (!initialized) {
        registerMyDriverCommands(stdiolink::CommandRegistry::instance());
        initialized = true;
    }
}
```

在 ICommandHandler 中处理 meta 命令：

```cpp
class MyDriverHandler : public ICommandHandler {
public:
    void handle(const QString& cmd, const QJsonValue& data, IResponder& responder) override {
        if (cmd == "meta.introspect") {
            QJsonObject result;
            result["version"] = "1.0";
            result["capabilities"] = QJsonArray{"config"}; // 如支持
            result["commands"] = CommandRegistry::instance().getCommandsSchema();
            // result["config"] = m_configSchema; // 如有
            responder.done(0, result);
            return;
        }
        if (cmd == "meta.get_config") {
            responder.done(0, currentConfig());
            return;
        }
        if (cmd == "meta.set_config") {
            // 验证并应用 data
            applyConfig(data.toObject());
            responder.done(0, QJsonObject{{"status", "applied"}});
            return;
        }
        // 正常业务命令处理...
        if (cmd == "analyze_image") {
            // 提取并验证 data (可选: 使用 QJsonSchemaValidator 第三方库)
            // ...
        }
    }
};
```

### 4. Host 侧增强
- Driver 类新增 `Task introspect()` 方法，封装发送 `meta.introspect`  
- 新增 `QJsonObject driverSchema()` 缓存查询结果  
- 提供工具函数 `bool validateRequest(const QString& cmd, const QJsonObject& data, const QJsonObject& allSchemas)`

### 5. 可选：参数验证
未来可集成轻量 JSON Schema 验证器（单文件实现或第三方如 qjson-schema-validator），在 Host 发送前和 Driver 接收后双向验证。

---

## 五、开发阶段计划

1. **阶段1：协议与核心类实现**  
   实现 CommandRegistry、SchemaBuilder，实现 `meta.introspect` 基本响应（commands only）

2. **阶段2：全局配置支持**  
   添加 config schema、get/set 命令

3. **阶段3：开发者工具完善**  
   完善 SchemaBuilder（支持更多关键词），编写详细示例 Driver

4. **阶段4：Host 侧增强与 UI 集成准备**  
   提供 schema 解析工具，示例 Qt UI 动态表单生成（QFormLayout + QWidget 根据 type 创建控件）

5. **阶段5：文档与迁移指南**  
   更新 README，说明新命令为保留命令，提供迁移模板

---

## 六、风险与注意事项

- **命令名冲突**：强制 `meta.` 前缀，文档明确警告  
- **Schema复杂性**：限制支持的关键词子集，避免实现验证器时爆炸  
- **性能**：Schema建议静态构建，缓存返回  
- **向后兼容**：不实现meta的旧Driver在Host查询时返回空schema，UI降级为手动输入

---

此方案将显著提升 StdioLink 框架的接口描述与工具化支持能力，真正实现“先定义接口模板，再写实现代码”的现代开发流程。

---