# Driver Meta 自描述与模板化接口设计（下一阶段）

> 目标：让 Driver 具备“自描述 + 自文档 + 可注册 + 可生成 UI + 可自动构造调用请求”的能力。
> 核心思想：**先定义模板，再实现代码**。Driver 通过标准 Meta 模板声明命令、参数、类型、配置与 UI 提示；Host 读取模板后完成注册、校验、UI 与调用构建。

---

## 1. 背景与动机

当前 stdiolink 已具备稳定的 JSONL 协议与 Driver/Host 调用模型，但 Driver 对外接口缺少标准化的“结构化描述”，导致：

- Host 无法自动识别 Driver 支持哪些命令与参数。
- UI 无法从 Driver 端获取字段类型、默认值、校验规则、配置项。
- 调用请求只能人工拼装，难以自动化或低代码。
- 文档分散在 README 或代码里，不可机器理解。

因此需要一个标准化的 **Meta 模板**，让 Driver 自描述其能力，并形成统一的注册、校验、UI 生成与调用生成链路。

---

## 2. 目标与范围

### 2.1 目标

- Driver 可以导出标准 Meta 模板。
- Host 能注册 Driver，并读取、解析、缓存其 Meta。
- Meta 能描述所有命令、参数类型、约束、默认值、示例与错误码。
- Meta 能描述 Driver 配置模板并驱动配置 UI 生成。
- UI 能根据命令参数模板生成调用表单，并自动构造请求。
- 形成可自动生成文档的“自文档”能力。

### 2.2 非目标

- 不引入复杂的跨语言反射机制。
- 不强制 Driver 一定在运行时返回 Meta（允许静态文件导出）。
- 不改变现有 JSONL 协议的基本形态（仍是 cmd + data）。

---

## 3. 术语

- **Meta 模板**：Driver 自描述的标准 JSON 文档。
- **DriverSpec**：Host 解析 Meta 后得到的内部结构。
- **CommandSpec**：单个命令的结构化描述。
- **ConfigSpec**：Driver 全局配置模板描述。
- **TypeSpec**：字段/参数的数据类型与约束描述。

---

## 4. 总体需求（可验收）

### 4.1 功能需求

1. Driver 端可导出 Meta 模板（文件或命令返回）。
2. Host 可读取 Meta，并注册到 DriverRegistry。
3. Host 可根据 Meta 自动校验请求参数。
4. Host 可生成配置 UI 模型，并导出表单字段。
5. Host 可生成命令调用 UI 模型，并构造请求 JSON。
6. Meta 提供命令输入、输出、事件流、错误码的结构描述。
7. Meta 可被渲染为 Markdown 文档。

### 4.2 非功能需求

- 解析与校验在毫秒级完成（单个 Meta < 200KB）。
- Meta 需版本化，支持向后兼容。
- 对未提供 Meta 的 Driver 提供兼容模式（手动输入）。

---

## 5. Meta 模板规范（Meta Schema v1）

Meta 模板是一个 JSON 文档，版本化，结构稳定。文件名建议 `driver.meta.json`。

### 5.1 顶层结构

```json
{
  "schemaVersion": "1.0",
  "driver": {
    "id": "vendor.device.scan",
    "name": "Scan Driver",
    "version": "2.3.0",
    "vendor": "ACME",
    "description": "设备扫描驱动",
    "entry": {
      "program": "scan_driver.exe",
      "defaultArgs": ["--mode=stdio", "--profile=keepalive"]
    },
    "capabilities": ["stdio", "console"],
    "profiles": ["oneshot", "keepalive"]
  },
  "config": { "schema": { /* TypeSpec */ }, "apply": { /* ConfigApply */ } },
  "commands": [ /* CommandSpec[] */ ],
  "errors": [ /* ErrorSpec[] 可选 */ ],
  "examples": [ /* Example[] 可选 */ ]
}
```

### 5.2 TypeSpec 定义

TypeSpec 用于描述任意字段的类型、约束与 UI 提示，语义类似 JSON Schema 的简化子集。

```json
{
  "type": "string|int|number|bool|object|array|enum|any",
  "title": "字段名",
  "description": "字段说明",
  "required": true,
  "default": "...",
  "enum": ["A", "B"],
  "minimum": 0,
  "maximum": 100,
  "minLength": 1,
  "maxLength": 32,
  "pattern": "^[a-z]+$",
  "items": { /* TypeSpec */ },
  "properties": { "key": { /* TypeSpec */ } },
  "ui": { /* UIHint */ }
}
```

TypeSpec 约束说明：

- `required` 仅作用于该字段。
- `object` 类型使用 `properties` 描述子字段。
- `array` 类型使用 `items` 描述元素类型。
- `enum` 类型必须同时给出 `enum`。

### 5.3 UIHint 定义

```json
{
  "widget": "text|textarea|number|slider|checkbox|select|file|folder|json|password|date|time",
  "group": "分组名",
  "order": 10,
  "placeholder": "提示",
  "advanced": false,
  "readonly": false,
  "visibleIf": "expr"
}
```

约定：

- `visibleIf` 支持简单表达式，例如 `mode == 'fast'`。
- Host 端可选择忽略不支持的 UIHint。

### 5.4 CommandSpec 定义

```json
{
  "name": "scan",
  "title": "扫描",
  "summary": "启动扫描",
  "description": "启动设备扫描并输出进度",
  "request": { "schema": { /* TypeSpec */ } },
  "response": {
    "event": { "schema": { /* TypeSpec */ } },
    "done":  { "schema": { /* TypeSpec */ } },
    "error": { "schema": { /* TypeSpec */ } }
  },
  "errors": [ { "code": 1001, "name": "InvalidParam", "message": "..." } ],
  "examples": [
    { "title": "默认扫描", "cmd": "scan", "data": {"fps": 10} }
  ],
  "ui": { "group": "Scan", "order": 1 }
}
```

说明：

- `request.schema` 对应 JSONL 请求中的 `data` 结构。
- `response.event/done/error` 描述 payload 结构。
- `errors` 可放在命令内部或顶层共享。

### 5.5 ConfigSpec 与 Apply

Driver 配置模板用于驱动配置 UI，并指导 Host 如何把配置送入 Driver。

```json
{
  "schema": { /* TypeSpec */ },
  "apply": {
    "method": "startupArgs|env|command|file",
    "command": "config.set",
    "fileName": "driver_config.json",
    "envPrefix": "DRIVER_"
  }
}
```

约定：

- `startupArgs` 表示 Host 以 `--key=value` 注入。
- `env` 表示 Host 以环境变量注入。
- `command` 表示 Host 通过 stdiolink 调用 `config.set`。
- `file` 表示 Host 生成配置文件并传入路径。

### 5.6 Meta 校验规则（强约束）

Host 端必须执行以下校验，失败即注册失败：

- `schemaVersion` 必填，格式 `主版本.次版本`，主版本必须为 `1`。
- `driver.id` 必填，正则 `^[a-zA-Z0-9_.-]+$`，全局唯一。
- `driver.name`、`driver.version` 必填。
- `commands` 为数组且命令名唯一，命令名正则 `^[a-zA-Z0-9_.-]+$`。
- `request.schema` 必填且类型为 `object` 或 `any`。
- `TypeSpec.type` 必填且必须在允许集合内。
- `enum` 类型必须包含非空 `enum` 数组。
- `array` 类型必须包含 `items`。
- `object` 类型可省略 `properties`，表示允许自由字段。

### 5.7 命名与保留域

- 保留命令前缀：`meta.` 与 `config.`。
- Driver 业务命令不得使用上述前缀。
- Host 可在 UI 中隐藏保留命令，仅用于框架调用。

---

## 6. Meta 导出与注册流程

### 6.1 Driver 端导出方式

支持两种互补方式，均需实现至少一种：

1. **静态文件**：随 Driver 包发布 `driver.meta.json`。
2. **动态命令**：保留命令 `meta.describe`，返回 Meta JSON。

命令约定：

- 请求：`{"cmd":"meta.describe","data":{}}`
- 响应：`done` + Meta JSON payload

### 6.2 Host 端注册流程

1. 发现 Driver（目录扫描或手工添加）。
2. 尝试读取 `driver.meta.json`。
3. 如无文件，则启动 Driver 并调用 `meta.describe`。
4. 校验 Meta 是否满足 Meta Schema v1。
5. 生成 DriverSpec，存入 DriverRegistry。
6. 生成可缓存的 Meta Hash，用于变更检测。

---

## 7. Host 侧结构设计

### 7.1 DriverRegistry

```cpp
class DriverRegistry {
public:
    bool registerFromFile(const QString& path);
    bool registerFromDriver(const QString& program, const QStringList& args);

    bool hasDriver(const QString& id) const;
    DriverSpec driver(const QString& id) const;
    QList<DriverSpec> allDrivers() const;
};
```

### 7.2 DriverSpec（核心结构）

```cpp
struct DriverSpec {
    QString id;
    QString name;
    QString version;
    QString vendor;
    QString description;

    EntrySpec entry;
    ConfigSpec config;
    QList<CommandSpec> commands;
};
```

### 7.3 校验与默认值

- Host 在发送请求前执行本地校验。
- 缺失字段按 `default` 填充。
- 对不满足 TypeSpec 的值直接拒绝发送。

---

## 8. UI 生成规则

### 8.1 字段类型到控件映射

| TypeSpec.type | 默认控件 | 说明 |
|---|---|---|
| string | text | 普通文本输入 |
| int | number | 整数输入 |
| number | number | 浮点输入 |
| bool | checkbox | 勾选框 |
| enum | select | 下拉选择 |
| array | list | 可增删列表 |
| object | group | 子表单 |

### 8.2 UI 分组与排序

- 通过 `ui.group` 实现分组。
- 通过 `ui.order` 控制字段排序。

### 8.3 配置 UI 生成

- 使用 `config.schema` 生成配置面板。
- UI 保存配置后由 Host 按 `config.apply` 规则注入 Driver。

### 8.4 调用 UI 生成

- 使用 `command.request.schema` 生成参数面板。
- UI 收集值后构造 `data` JSON 并调用 `cmd`。

---

## 9. 请求构建与运行流程

1. UI 读取 CommandSpec，生成表单。
2. 用户输入参数，Host 执行本地校验与默认值补齐。
3. Host 形成 `{cmd, data}` 请求。
4. Driver 执行并返回 event/done/error。
5. Host 可用 `response.schema` 解析并渲染输出。

### 9.1 数据映射规则

- `request.schema` 描述的对象即为 `data` 根对象。
- 未提供值且有 `default` 的字段必须自动填充。
- 未提供值且无 `default` 的 `required` 字段视为校验失败。
- `data` 中允许存在未在 schema 中声明的字段，但默认应给出告警。

### 9.2 类型校验规则

- `int` 必须为整数且在 32 位有符号范围内。
- `number` 允许整数与浮点。
- `enum` 必须精确匹配 `enum` 数组成员。
- `object` 可递归校验子字段。
- `array` 必须逐项校验 `items`。

---

## 10. 版本化与兼容性

- Meta 模板必须包含 `schemaVersion`。
- Host 支持向后兼容同一主版本的 schema。
- Driver 版本变化时必须同步更新 `driver.version`。

---

## 11. 错误处理与降级策略

- Meta 校验失败：注册失败，Host 标记为不可用。
- Meta 缺失：进入“手动模式”，允许用户自定义 cmd/data。
- Driver 不支持 `meta.describe`：只使用静态文件或手动模式。

---

## 12. 安全与性能

- Meta 不允许执行表达式，仅限 UI 提示。
- Host 读取 Meta 后应缓存并限制大小（默认 200KB）。
- 对 `visibleIf` 表达式需白名单解析，禁止脚本执行。

---

## 13. 测试与验收标准

### 13.1 必要测试

- Meta Schema 校验单测。
- DriverRegistry 注册流程单测。
- 参数校验与默认值填充单测。
- UI 模型生成单测。
- meta.describe 集成测试。

### 13.2 验收标准

- Host 能通过 Meta 列出所有 Driver 和命令。
- UI 能生成配置界面与调用界面。
- 调用请求由 UI 自动构造并可正确执行。
- Meta 变更后 Host 能检测并刷新缓存。

---

## 14. 实施建议（下一阶段里程碑）

1. **里程碑 A：Meta Schema 与导出机制**
2. **里程碑 B：Host 注册与校验**
3. **里程碑 C：UI 模型生成与调用构建**
4. **里程碑 D：文档生成与示例 Driver**

---

## 15. 附录：最小 Meta 示例

```json
{
  "schemaVersion": "1.0",
  "driver": {
    "id": "demo.echo",
    "name": "Echo Driver",
    "version": "1.0.0",
    "vendor": "stdiolink",
    "description": "回显输入",
    "entry": {
      "program": "echo_driver.exe",
      "defaultArgs": ["--mode=stdio", "--profile=keepalive"]
    },
    "capabilities": ["stdio"],
    "profiles": ["keepalive"]
  },
  "config": {
    "schema": {
      "type": "object",
      "properties": {
        "logLevel": {
          "type": "enum",
          "enum": ["debug","info","warn","error"],
          "default": "info"
        }
      }
    },
    "apply": { "method": "env", "envPrefix": "ECHO_" }
  },
  "commands": [
    {
      "name": "echo",
      "title": "回显",
      "summary": "回显输入",
      "request": {
        "schema": {
          "type": "object",
          "properties": {
            "msg": { "type": "string", "required": true }
          }
        }
      },
      "response": {
        "done": {
          "schema": {
            "type": "object",
            "properties": {
              "msg": { "type": "string" }
            }
          }
        }
      }
    }
  ]
}
```

---

> 本文档定义了下一阶段的开发需求与详细设计，为 Driver 自描述、自文档与自动化 UI/调用生成提供标准。
