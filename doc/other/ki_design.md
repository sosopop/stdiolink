 基于你当前的 `stdiolink` 框架和你提出的**Driver 自描述（Self-Describing）**架构构想，我将为你生成一份详细的下一阶段开发需求与设计文档。这个设计的核心目标是实现 **"Schema-Driven Development"**：先定义接口契约，后实现业务逻辑。

---

# stdiolink v2.0 自描述驱动架构设计文档

## 1. 核心目标与架构愿景

### 1.1 设计哲学
```
┌─────────────────────────────────────────────────────────────┐
│  Schema First, Code Second                                  │
│  接口即文档，契约即代码                                      │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 核心能力矩阵
| 能力 | 描述 | 受益方 |
|------|------|--------|
| **自描述** | Driver 暴露自身的元数据能力 | Host/IDE |
| **自文档** | 自动生成 API 文档和 SDK | 开发者 |
| **自配置** | 基于 Schema 生成配置 UI | 终端用户 |
| **自验证** | 运行时参数校验与类型安全 | 运行时系统 |

---

## 2. 元数据协议设计（Meta-Schema）

### 2.1 协议分层架构

```
┌────────────────────────────────────────────────────────────┐
│  Layer 3: Application Schema (业务层)                      │
│  - 具体 Driver 的命令定义、参数结构、返回值定义              │
├────────────────────────────────────────────────────────────┤
│  Layer 2: Meta-Schema Protocol (元协议层)                  │
│  - 描述如何描述命令的元语言                                 │
│  - 类型系统、验证规则、UI 渲染提示                          │
├────────────────────────────────────────────────────────────┤
│  Layer 1: Transport Protocol (传输层)                      │
│  - 现有的 JSONL 协议 (stdiolink v1.0)                      │
└────────────────────────────────────────────────────────────┘
```

### 2.2 Meta-Schema 核心结构（JSON Schema 扩展）

```json
{
  "$schema": "https://stdiolink.io/meta-schema/v1",
  "driver": {
    "id": "com.example.image-processor",
    "name": "Image Processor",
    "version": "2.1.0",
    "description": "Advanced image processing driver with AI enhancement",
    "vendor": "Example Corp",
    "license": "MIT",
    "minHostVersion": "2.0.0",
    "capabilities": ["stream", "batch", "preview"]
  },
  "types": {
    // 可复用的类型定义
    "ImageFormat": {
      "type": "string",
      "enum": ["jpeg", "png", "webp", "raw"],
      "default": "jpeg",
      "ui": {
        "widget": "select",
        "label": "输出格式",
        "description": "选择目标图像格式"
      }
    },
    "Resolution": {
      "type": "object",
      "properties": {
        "width": { "type": "integer", "minimum": 1, "maximum": 16384 },
        "height": { "type": "integer", "minimum": 1, "maximum": 16384 },
        "aspectRatio": { 
          "type": "string", 
          "pattern": "^(\\d+):(\\d+)$",
          "ui": { "widget": "aspectPicker" }
        }
      },
      "required": ["width", "height"]
    }
  },
  "commands": {
    "resize": {
      "category": "transform",
      "description": "调整图像尺寸",
      "documentation": "https://docs.example.com/resize",
      "deprecated": false,
      "permissions": ["file.read", "file.write"],
      
      "parameters": {
        "type": "object",
        "properties": {
          "input": {
            "type": "string",
            "format": "uri",
            "description": "输入图像路径或 URL",
            "ui": {
              "widget": "filePicker",
              "filters": ["image/*"],
              "preview": true
            }
          },
          "output": {
            "type": "string",
            "format": "uri",
            "description": "输出路径",
            "ui": { "widget": "saveFile" }
          },
          "size": { "$ref": "#/types/Resolution" },
          "format": { "$ref": "#/types/ImageFormat" },
          "quality": {
            "type": "number",
            "minimum": 0,
            "maximum": 100,
            "default": 85,
            "ui": {
              "widget": "slider",
              "step": 1,
              "suffix": "%"
            }
          },
          "algorithm": {
            "type": "string",
            "enum": ["nearest", "bilinear", "bicubic", "lanczos"],
            "default": "lanczos",
            "ui": {
              "widget": "radio",
              "options": [
                {"value": "nearest", "label": "最近邻", "description": "速度快，质量低"},
                {"value": "lanczos", "label": "Lanczos", "description": "质量最佳"}
              ]
            }
          }
        },
        "required": ["input", "size"],
        "dependencies": {
          "quality": ["format"]  // quality 只在指定 format 时有效
        }
      },
      
      "returns": {
        "type": "object",
        "properties": {
          "output": { "type": "string", "description": "实际输出路径" },
          "bytesProcessed": { "type": "integer" },
          "timeMs": { "type": "number" }
        }
      },
      
      "streaming": {
        "enabled": true,
        "events": {
          "progress": {
            "description": "处理进度更新",
            "schema": {
              "type": "object",
              "properties": {
                "percent": { "type": "number" },
                "stage": { "type": "string" }
              }
            }
          },
          "preview": {
            "description": "实时预览",
            "schema": {
              "type": "string",
              "format": "base64",
              "contentType": "image/png"
            }
          }
        }
      },
      
      "examples": [
        {
          "name": "基本缩放",
          "description": "将图像缩放至 800x600",
          "request": {
            "cmd": "resize",
            "data": {
              "input": "/path/to/image.jpg",
              "size": {"width": 800, "height": 600}
            }
          }
        }
      ],
      
      "configTemplate": {
        "preset": {
          "type": "string",
          "enum": ["web", "print", "thumbnail"],
          "description": "使用预设配置",
          "ui": { "widget": "presetSelector" }
        }
      }
    }
  },
  
  "configuration": {
    "schema": {
      "type": "object",
      "properties": {
        "tempDir": {
          "type": "string",
          "format": "path",
          "default": "/tmp/stdiolink",
          "description": "临时文件目录"
        },
        "maxMemoryMB": {
          "type": "integer",
          "default": 1024,
          "minimum": 256,
          "ui": { "widget": "memoryPicker" }
        },
        "gpuAcceleration": {
          "type": "boolean",
          "default": true,
          "ui": { "widget": "toggle" }
        },
        "logLevel": {
          "type": "string",
          "enum": ["debug", "info", "warn", "error"],
          "default": "info"
        }
      }
    },
    "groups": [
      {
        "id": "performance",
        "label": "性能设置",
        "properties": ["maxMemoryMB", "gpuAcceleration"]
      },
      {
        "id": "debugging",
        "label": "调试选项",
        "properties": ["logLevel"]
      }
    ]
  }
}
```

### 2.3 UI Schema 规范（扩展 JSON Schema）

```typescript
// UI 元数据标准
interface UISchema {
  widget: 'text' | 'number' | 'slider' | 'select' | 'radio' | 
          'checkbox' | 'filePicker' | 'colorPicker' | 'datePicker' |
          'codeEditor' | 'jsonEditor' | 'richtext' | 'toggle' |
          'range' | 'vector2' | 'vector3' | 'matrix' | 'table';
  
  // 布局控制
  layout?: {
    row?: number;
    col?: number;
    span?: number;
    group?: string;
    section?: string;
    collapsible?: boolean;
    defaultCollapsed?: boolean;
  };
  
  // 验证与提示
  validation?: {
    async?: boolean;           // 是否需要异步验证
    endpoint?: string;         // 验证端点（如用户名查重）
    debounceMs?: number;       // 防抖时间
  };
  
  // 动态行为
  condition?: {
    showWhen?: string;         // JSON Logic 表达式
    enableWhen?: string;
    optionsFrom?: string;      // 动态选项数据源
  };
  
  // 国际化
  i18n?: {
    [lang: string]: {
      label?: string;
      description?: string;
      placeholder?: string;
    }
  };
  
  // 高级特性
  advanced?: {
    unit?: string;             // 单位（如 px, ms, MB）
    step?: number;
    precision?: number;
    min?: number;
    max?: number;
    accept?: string;           // 文件类型
    multiple?: boolean;        // 多文件选择
    preview?: boolean;         // 实时预览
    search?: boolean;          // 可搜索选项
    creatable?: boolean;       // 可创建新选项
  };
}
```

---

## 3. 运行时架构设计

### 3.1 Driver 侧实现

#### 3.1.1 C++ 模板元编程接口

```cpp
// stdiolink/meta.hpp - 新增加的元编程层

#pragma once
#include <string_view>
#include <tuple>
#include <type_traits>
#include <QJsonObject>

namespace stdiolink::meta {

// 基础类型标记
template<typename T>
struct TypeTag {
    static constexpr std::string_view name = "unknown";
    static QJsonObject schema() { return {{"type", "object"}}; }
};

// 特化常见类型
template<> struct TypeTag<int> {
    static constexpr std::string_view name = "integer";
    static QJsonObject schema() { 
        return {{"type", "integer"}}; 
    }
};

template<> struct TypeTag<double> {
    static constexpr std::string_view name = "number";
    static QJsonObject schema() { 
        return {{"type", "number"}}; 
    }
};

template<> struct TypeTag<QString> {
    static constexpr std::string_view name = "string";
    static QJsonObject schema() { 
        return {{"type", "string"}}; 
    }
};

template<> struct TypeTag<bool> {
    static constexpr std::string_view name = "boolean";
    static QJsonObject schema() { 
        return {{"type", "boolean"}}; 
    }
};

// 属性描述符
template<typename T, typename... Args>
struct Property {
    using Type = T;
    std::string_view name;
    std::string_view description;
    bool required = true;
    std::optional<T> defaultValue;
    std::function<QJsonObject()> customSchema;
    
    // UI 元数据
    struct UI {
        std::string_view widget = "text";
        std::string_view label;
        std::string_view placeholder;
        QJsonObject advanced;
    } ui;
    
    // 验证器链
    std::vector<std::function<bool(const T&)>> validators;
    
    QJsonObject toSchema() const {
        QJsonObject prop;
        prop["type"] = QString::fromUtf8(TypeTag<T>::name.data(), 
                                         TypeTag<T>::name.size());
        if (!description.empty()) {
            prop["description"] = QString::fromUtf8(description.data(), 
                                                    description.size());
        }
        if (defaultValue) {
            // 序列化默认值...
        }
        
        // 添加 UI 元数据
        QJsonObject uiObj;
        uiObj["widget"] = QString::fromUtf8(ui.widget.data(), ui.widget.size());
        if (!ui.label.empty()) {
            uiObj["label"] = QString::fromUtf8(ui.label.data(), ui.label.size());
        }
        if (!ui.advanced.isEmpty()) {
            for (auto it = ui.advanced.begin(); it != ui.advanced.end(); ++it) {
                uiObj[it.key()] = it.value();
            }
        }
        prop["ui"] = uiObj;
        
        // 如果有自定义 schema，合并
        if (customSchema) {
            QJsonObject custom = customSchema();
            for (auto it = custom.begin(); it != custom.end(); ++it) {
                prop[it.key()] = it.value();
            }
        }
        
        return prop;
    }
};

// 命令描述符（编译期构建）
template<typename ParamStruct, typename ReturnStruct>
struct CommandDef {
    std::string_view name;
    std::string_view category;
    std::string_view description;
    std::string_view documentation;
    bool streaming = false;
    bool deprecated = false;
    
    // 使用结构体反射生成参数 schema
    static QJsonObject parametersSchema() {
        return reflectStruct<ParamStruct>();
    }
    
    static QJsonObject returnsSchema() {
        return reflectStruct<ReturnStruct>();
    }
    
    // 编译期生成完整命令 schema
    constexpr QJsonObject toSchema() const {
        QJsonObject cmd;
        cmd["category"] = QString::fromUtf8(category.data(), category.size());
        cmd["description"] = QString::fromUtf8(description.data(), description.size());
        cmd["streaming"] = streaming;
        cmd["deprecated"] = deprecated;
        cmd["parameters"] = parametersSchema();
        cmd["returns"] = returnsSchema();
        return cmd;
    }
};

// 驱动描述符
struct DriverManifest {
    std::string_view id;
    std::string_view name;
    std::string_view version;
    std::string_view description;
    std::string_view vendor;
    
    std::vector<CommandDef> commands;
    
    QJsonObject toSchema() const {
        QJsonObject manifest;
        manifest["id"] = QString::fromUtf8(id.data(), id.size());
        manifest["name"] = QString::fromUtf8(name.data(), name.size());
        manifest["version"] = QString::fromUtf8(version.data(), version.size());
        
        QJsonObject cmds;
        for (const auto& cmd : commands) {
            cmds[QString::fromUtf8(cmd.name.data(), cmd.name.size())] = 
                cmd.toSchema();
        }
        manifest["commands"] = cmds;
        
        return manifest;
    }
};

// CRTP 基类：自动注册元数据
template<typename Derived>
class MetaDriver : public ICommandHandler {
public:
    // 自动响应 meta 命令
    void handle(const QString& cmd, const QJsonValue& data, 
                IResponder& responder) override {
        if (cmd == "stdiolink.meta") {
            handleMetaCommand(data, responder);
            return;
        }
        if (cmd == "stdiolink.schema") {
            handleSchemaCommand(data, responder);
            return;
        }
        if (cmd == "stdiolink.config") {
            handleConfigCommand(data, responder);
            return;
        }
        if (cmd == "stdiolink.validate") {
            handleValidateCommand(data, responder);
            return;
        }
        
        // 派生类处理业务命令
        static_cast<Derived*>(this)->handleBusinessCommand(
            cmd, data, responder);
    }
    
private:
    void handleMetaCommand(const QJsonValue& data, IResponder& responder) {
        // 返回 Driver 元数据
        QJsonObject meta = static_cast<Derived*>(this)->manifest().toSchema();
        responder.done(0, meta);
    }
    
    void handleSchemaCommand(const QJsonValue& data, IResponder& responder) {
        // 返回特定命令的 schema
        QString targetCmd = data.toObject()["command"].toString();
        auto manifest = static_cast<Derived*>(this)->manifest();
        
        for (const auto& cmd : manifest.commands) {
            if (QString::fromUtf8(cmd.name.data(), cmd.name.size()) == targetCmd) {
                QJsonObject result;
                result["parameters"] = cmd.parametersSchema();
                result["returns"] = cmd.returnsSchema();
                responder.done(0, result);
                return;
            }
        }
        responder.error(404, {{"message", "Command not found"}});
    }
    
    void handleValidateCommand(const QJsonValue& data, IResponder& responder) {
        // 验证参数而不执行
        QString cmd = data.toObject()["command"].toString();
        QJsonObject params = data.toObject()["parameters"].toObject();
        
        // 使用 schema 验证
        ValidationResult result = validateParams(cmd, params);
        if (result.valid) {
            responder.done(0, {{"valid", true}});
        } else {
            responder.done(0, {
                {"valid", false},
                {"errors", result.errors}
            });
        }
    }
};

} // namespace stdiolink::meta
```

#### 3.1.2 使用示例：定义自描述 Driver

```cpp
// mydriver.cpp - 使用宏和模板简化定义

#include <stdiolink/meta.hpp>

// 1. 定义参数结构体（使用宏生成反射信息）
STDIOLINK_STRUCT(ResizeParams) {
    STDIOLINK_FIELD(QString, input, "输入路径")
        .ui<FilePicker>({
            .filters = {"image/*"},
            .preview = true
        })
        .required();
    
    STDIOLINK_FIELD(Resolution, size, "目标尺寸")
        .ui<Vector2Input>({
            .min = {1, 1},
            .max = {16384, 16384},
            .lockAspectRatio = true
        });
    
    STDIOLINK_FIELD(std::optional<ImageFormat>, format, "输出格式")
        .defaultValue(ImageFormat::JPEG)
        .ui<Select>({
            .options = {
                {"jpeg", "JPEG - 有损压缩"},
                {"png", "PNG - 无损压缩"},
                {"webp", "WebP - 现代格式"}
            }
        });
    
    STDIOLINK_FIELD(double, quality, "图像质量")
        .range(0, 100)
        .defaultValue(85)
        .ui<Slider>({
            .step = 1,
            .suffix = "%",
            .showValue = true
        })
        .condition("format != 'png'"); // 仅在非 PNG 时显示
};

// 2. 定义返回值结构体
STDIOLINK_STRUCT(ResizeResult) {
    STDIOLINK_FIELD(QString, output, "输出路径");
    STDIOLINK_FIELD(quint64, bytesProcessed, "处理字节数");
    STDIOLINK_FIELD(double, timeMs, "耗时(ms)");
};

// 3. 定义 Driver 类
class ImageDriver : public MetaDriver<ImageDriver> {
public:
    // 声明元数据（编译期生成）
    constexpr static DriverManifest manifest() {
        return {
            .id = "com.example.image-driver",
            .name = "Image Processor",
            .version = "1.0.0",
            .description = "High-performance image processing driver",
            .commands = {
                CommandDef<ResizeParams, ResizeResult>{
                    .name = "resize",
                    .category = "transform",
                    .description = "Resize image with various algorithms",
                    .streaming = true
                },
                CommandDef<ConvertParams, ConvertResult>{
                    .name = "convert",
                    .category = "format",
                    .description = "Convert between image formats"
                }
            }
        };
    }
    
    // 业务命令处理
    void handleBusinessCommand(const QString& cmd, const QJsonValue& data,
                              IResponder& responder) override {
        if (cmd == "resize") {
            // 参数自动反序列化和验证
            auto params = parseParams<ResizeParams>(data);
            if (!params.valid()) {
                responder.error(400, {{"errors", params.errors()}});
                return;
            }
            
            // 执行业务逻辑...
            executeResize(params.value(), responder);
        }
    }
    
private:
    void executeResize(const ResizeParams& params, IResponder& responder) {
        // 发送进度事件
        responder.event(0, {
            {"stage", "loading"},
            {"percent", 25}
        });
        
        // 实际处理...
        
        // 返回结果（自动序列化）
        ResizeResult result{
            .output = "/path/to/output.jpg",
            .bytesProcessed = 1024000,
            .timeMs = 150.5
        };
        responder.done(0, serialize(result));
    }
};

STDIOLINK_REGISTER_DRIVER(ImageDriver)
```

---

### 3.2 Host 侧实现

#### 3.2.1 Driver 代理与发现

```cpp
// stdiolink/host/driver_proxy.hpp

#pragma once
#include <QObject>
#include <QJsonSchema>
#include <memory>

namespace stdiolink::host {

// Driver 能力代理
class DriverProxy : public QObject {
    Q_OBJECT
public:
    explicit DriverProxy(const QString& program, 
                        const QStringList& args = {},
                        QObject* parent = nullptr);
    
    // 生命周期管理
    bool initialize();  // 启动并获取元数据
    bool isReady() const;
    void shutdown();
    
    // 元数据访问
    QJsonObject manifest() const;
    QJsonObject commandSchema(const QString& cmd) const;
    QStringList availableCommands() const;
    QJsonObject configurationSchema() const;
    
    // 类型安全调用接口
    template<typename T = QJsonObject>
    Task call(const QString& cmd, const T& params = {});
    
    // UI 生成接口
    QWidget* createConfigWidget(QWidget* parent = nullptr);
    QWidget* createCommandWidget(const QString& cmd, 
                                  QWidget* parent = nullptr);
    
    // 智能提示与验证
    ValidationResult validate(const QString& cmd, 
                             const QJsonObject& params);
    QJsonObject autocomplete(const QString& cmd,
                            const QString& path,
                            const QJsonObject& partial);
    
    // 代码生成
    QString generateExampleCode(const QString& language,
                               const QString& cmd,
                               const QJsonObject& params);
    
signals:
    void capabilityChanged();
    void commandAdded(const QString& cmd);
    void commandDeprecated(const QString& cmd);
    
private:
    std::unique_ptr<Driver> m_driver;
    QJsonObject m_manifest;
    QHash<QString, QJsonObject> m_commandSchemas;
    
    // 缓存编译后的 schema 用于快速验证
    QHash<QString, std::shared_ptr<QJsonSchema>> m_validators;
};

// Driver 注册表（管理多个 Driver）
class DriverRegistry : public QObject {
    Q_OBJECT
public:
    static DriverRegistry& instance();
    
    void registerDriver(const QString& id, 
                       std::function<DriverProxy*()> factory);
    void unregisterDriver(const QString& id);
    
    QStringList availableDrivers() const;
    DriverProxy* createDriver(const QString& id);
    
    // 全局搜索能力
    QList<DriverCapability> searchCapabilities(const QString& keyword);
    
private:
    QHash<QString, std::function<DriverProxy*()>> m_factories;
};

} // namespace stdiolink::host
```

#### 3.2.2 动态 UI 生成器

```cpp
// stdiolink/host/ui_generator.hpp

#pragma once
#include <QWidget>
#include <QFormLayout>
#include <QJsonObject>

namespace stdiolink::host {

// 动态表单生成器
class DynamicForm : public QWidget {
    Q_OBJECT
public:
    explicit DynamicForm(const QJsonObject& schema, 
                        QWidget* parent = nullptr);
    
    QJsonObject values() const;
    void setValues(const QJsonObject& values);
    
    bool validate();
    QList<ValidationError> errors() const;
    
    // 动态行为
    void setCondition(const QString& field, 
                     const QString& expression,
                     VisibilityAction action);
    
signals:
    void valueChanged(const QString& field, const QJsonValue& value);
    void validationChanged(bool valid);
    
private:
    void buildForm(const QJsonObject& properties);
    QWidget* createWidget(const QString& type, 
                         const QJsonObject& ui,
                         const QJsonObject& validation);
    
    QFormLayout* m_layout;
    QHash<QString, QWidget*> m_fields;
    QJsonObject m_schema;
};

// 具体控件实现示例
class FilePickerWidget : public QWidget {
    Q_OBJECT
public:
    explicit FilePickerWidget(const QJsonObject& ui, QWidget* parent);
    
    QString value() const;
    void setValue(const QString& path);
    
signals:
    void valueChanged(const QString& path);
    void previewRequested(const QString& path);
    
private:
    void setupUI(const QJsonObject& config);
    void onBrowse();
    
    QLineEdit* m_pathEdit;
    QPushButton* m_browseBtn;
    QLabel* m_preview;
    QStringList m_filters;
    bool m_allowMultiple;
};

class Vector2Widget : public QWidget {
    Q_OBJECT
public:
    explicit Vector2Widget(const QJsonObject& ui, QWidget* parent);
    
    QJsonObject value() const;
    void setValue(const QJsonObject& val);
    
    void setLockAspectRatio(bool lock);
    
signals:
    void valueChanged(const QJsonObject& value);
    
private:
    QDoubleSpinBox* m_xSpin;
    QDoubleSpinBox* m_ySpin;
    QCheckBox* m_lockAspect;
    double m_aspectRatio;
};

// 预设管理器
class PresetManager : public QObject {
    Q_OBJECT
public:
    void savePreset(const QString& name, 
                   const QString& command,
                   const QJsonObject& config);
    
    QJsonObject loadPreset(const QString& name);
    QStringList availablePresets(const QString& command) const;
    
    // 从 schema 的 configTemplate 生成默认预设
    void generateDefaultPresets(const QJsonObject& schema);
};

} // namespace stdiolink::host
```

---

## 4. 开发工具链设计

### 4.1 Schema 验证与代码生成 CLI

```bash
# stdiolink-dev 工具集

# 验证 schema 合法性
$ stdiolink-dev validate ./driver-schema.json
✓ Schema syntax valid
✓ All $refs resolved
✓ UI widgets exist
✓ No circular dependencies
⚠ Warning: Command 'resize' missing documentation URL

# 从 schema 生成 C++ 骨架代码
$ stdiolink-dev generate \
    --schema ./driver-schema.json \
    --language cpp \
    --output ./src-gen/
    
Generated files:
  - include/my_driver.hpp      (Driver 类声明)
  - src/my_driver.cpp          (实现骨架)
  - src/params_resize.cpp      (参数序列化)
  - src/params_convert.cpp     
  - tests/auto_validation.cpp  (参数验证测试)

# 从 schema 生成文档
$ stdiolink-dev docs \
    --schema ./driver-schema.json \
    --format markdown \
    --output ./docs/api.md

# 生成 TypeScript 类型定义（用于前端）
$ stdiolink-dev generate --language typescript --output ./web-sdk/

# 启动交互式配置编辑器
$ stdiolink-dev edit ./driver-schema.json
# 启动本地 Web 服务器，提供可视化编辑界面

# 测试 Driver 响应
$ stdiolink-dev test \
    --driver ./my-driver \
    --command resize \
    --params '{"input":"test.jpg","size":{"width":800}}' \
    --expect-streaming
```

### 4.2 IDE 集成

```json
// .vscode/settings.json
{
  "stdiolink.schemaPath": "./schema",
  "stdiolink.autoGenerate": true,
  "stdiolink.lint.strict": true
}

// 提供的功能：
// 1. Schema 自动补全
// 2. 实时验证（参数类型检查）
// 3. 跳转到定义（命令 -> schema -> 实现）
// 4. 重构支持（重命名命令自动更新 schema 和代码）
// 5. 内置预览（在 IDE 内渲染生成的 UI）
```

---

## 5. 配置管理架构

### 5.1 分层配置系统

```
┌─────────────────────────────────────────────────────────┐
│  Layer 4: Runtime Override (运行时覆盖)                  │
│  命令行参数、环境变量                                     │
├─────────────────────────────────────────────────────────┤
│  Layer 3: Session Config (会话配置)                      │
│  当前工作空间的临时设置                                   │
├─────────────────────────────────────────────────────────┤
│  Layer 2: User Config (用户配置)                         │
│  ~/.config/stdiolink/drivers/<driver-id>.json           │
├─────────────────────────────────────────────────────────┤
│  Layer 1: Driver Default (驱动默认)                      │
│  内置于 Driver 的 configTemplate                         │
└─────────────────────────────────────────────────────────┘
```

### 5.2 配置界面生成流程

```cpp
// 配置界面生成流程
void ConfigurationDialog::setupForDriver(DriverProxy* driver) {
    auto configSchema = driver->configurationSchema();
    
    // 1. 创建动态表单
    auto form = new DynamicForm(configSchema["schema"], this);
    
    // 2. 应用分组布局
    for (const auto& group : configSchema["groups"].toArray()) {
        auto groupBox = new CollapsibleGroupBox(
            group["label"].toString());
        
        for (const auto& prop : group["properties"].toArray()) {
            auto widget = form->field(prop.toString());
            groupBox->addRow(widget);
        }
        
        m_layout->addWidget(groupBox);
    }
    
    // 3. 加载当前配置（分层合并）
    auto config = ConfigManager::load(driver->id());
    form->setValues(config);
    
    // 4. 连接实时验证
    connect(form, &DynamicForm::valueChanged,
            this, [this, driver](const QString& field, 
                                const QJsonValue& value) {
        // 增量验证
        auto result = driver->validate("stdiolink.config", 
                                      {{field, value}});
        updateValidationState(field, result);
    });
    
    // 5. 预设选择器
    auto presetCombo = new QComboBox();
    for (const auto& preset : configSchema["presets"].toArray()) {
        presetCombo->addItem(preset["label"].toString(),
                            preset["data"]);
    }
    connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [form](int index) {
        form->setValues(presetCombo->itemData(index).toJsonObject());
    });
}
```

---

## 6. 详细接口规范

### 6.1 新增内置命令

| 命令 | 方向 | 描述 | 权限 |
|------|------|------|------|
| `stdiolink.meta` | Host → Driver | 获取完整元数据 | 无 |
| `stdiolink.schema` | Host → Driver | 获取特定命令 schema | 无 |
| `stdiolink.config` | Host → Driver | 获取/设置配置 | 管理员 |
| `stdiolink.validate` | Host → Driver | 验证参数 | 无 |
| `stdiolink.preview` | Host → Driver | 请求实时预览 | 视命令 |
| `stdiolink.introspect` | Host → Driver | 反射查询 | 调试 |

### 6.2 错误码扩展

```cpp
enum class MetaErrorCode {
    // 原有错误码...
    
    // Schema 相关 (2000-2099)
    InvalidSchema = 2000,        // Schema 语法错误
    UnknownCommand = 2001,       // 命令不存在
    ValidationFailed = 2002,     // 参数验证失败
    MissingRequiredField = 2003, // 缺少必填字段
    TypeMismatch = 2004,         // 类型不匹配
    ValueOutOfRange = 2005,      // 值超出范围
    InvalidReference = 2006,     // $ref 引用无效
    
    // 配置相关 (2100-2199)
    InvalidConfig = 2100,        // 配置无效
    ConfigReadOnly = 2101,       // 配置项只读
    UnsafePath = 2102,           // 路径安全检查失败
    
    // UI 相关 (2200-2299)
    WidgetNotSupported = 2200,   // 请求的控件不支持
    PreviewUnavailable = 2201    // 预览不可用
};
```

---

## 7. 实现路线图

### Phase 1: 基础设施（2-3周）
- [ ] 设计并冻结 Meta-Schema 规范
- [ ] 实现 JSON Schema 验证器（基于 QJsonSchema）
- [ ] 实现 `stdiolink.meta` 等内置命令
- [ ] 更新 Driver 基类支持元数据导出

### Phase 2: 代码生成工具（2周）
- [ ] 开发 `stdiolink-dev` CLI 工具
- [ ] 实现 C++ 代码生成器
- [ ] 实现 TypeScript 定义生成器
- [ ] 添加 IDE 插件基础功能

### Phase 3: Host 侧 UI 系统（3周）
- [ ] 实现 DynamicForm 框架
- [ ] 实现标准控件库（15+ 个控件）
- [ ] 实现预设管理系统
- [ ] 实现配置分层管理器

### Phase 4: 高级功能（2周）
- [ ] 条件渲染与动态表单
- [ ] 异步验证与实时预览
- [ ] 多语言国际化支持
- [ ] 文档自动生成系统

### Phase 5: 示例与生态（1周）
- [ ] 重构现有 Driver 使用新架构
- [ ] 编写完整开发指南
- [ ] 提供 React/Vue 的 UI 组件库
- [ ] 发布 v2.0 正式版

---

## 8. 技术风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| C++ 反射限制 | 高 | 使用代码生成 + 宏，避免依赖编译器 RTTI |
| Schema 版本兼容 | 中 | 内置版本协商机制，支持降级模式 |
| UI 渲染性能 | 中 | 虚拟化长表单，延迟加载复杂控件 |
| 安全验证绕过 | 高 | Host 侧二次验证，不信任 Driver 验证结果 |

---

这份设计为你提供了从底层协议到上层工具的完整蓝图。是否需要我深入某个具体模块（如代码生成器的实现细节，或特定控件的渲染逻辑）进行更详细的设计？