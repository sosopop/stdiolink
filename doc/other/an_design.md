# Driver 元数据自描述系统设计

## 1. 背景与目标

为了降低 Driver 开发门槛，提高 Host 对 Driver 的集成效率，我们需要实现一套 **Driver 自描述（Self-Description）机制**。
核心目标包括：
1.  **自描述（Self-Describing）**：Driver 能通过标准接口导出自身支持的命令、参数定义、配置项等元数据。
2.  **自文档（Self-Documenting）**：元数据可直接转化为人类可读的文档。
3.  **UI 自动生成（Auto-UI）**：Host 端可根据元数据自动生成配置界面和命令调试界面。
4.  **开发契约（Contract-First）**：鼓励“先定义接口模板，后实现代码”的开发模式。

---

## 2. 核心设计概要

### 2.1 总体架构
系统分为三层：
1.  **Meta Definition (Driver 端)**：通过 C++ 模板/宏或 DSL 定义命令与参数。
2.  **Meta Export (协议层)**：Driver 响应标准命令 `meta.get`，返回标准化 JSON 描述。
3.  **Meta Consumption (Host 端)**：Host 解析元数据，动态构建调用表单和校验逻辑。

### 2.2 工作流
1.  开发者在 C++ 中声明命令结构（Command/Params/Result）。
2.  编译 Driver。
3.  Host 启动 Driver，发送 `{"cmd": "meta.get"}`。
4.  Driver 返回完整元数据 JSON。
5.  Host 根据 JSON 渲染 UI，供用户配置和调试。

---

## 3. 元数据协议规范 (Schema)

`meta.get` 命令的响应 Payload 必须符合以下 JSON Schema：

### 3.1 顶层结构
```json
{
  "name": "vision_scanner_driver",  // Driver 名称
  "version": "1.0.0",               // 版本
  "description": "3D Vision Scanner Control Driver", // 描述
  "config": { ... },                // 1. 初始化/启动配置模版
  "commands": [ ... ]               // 2. 支持的命令列表
}
```

### 3.2 Config 模版 (Startup Configuration)
定义 Driver 启动或初始化所需的全局配置（通常对应命令行参数或环境变量）。

```json
"config": {
  "fields": [
    {
      "name": "device_ip",
      "type": "string",
      "default": "192.168.1.10",
      "description": "Scanner IP Address",
      "required": true,
      "format": "ipv4"
    },
    {
      "name": "timeout_ms",
      "type": "integer",
      "default": 3000,
      "min": 100,
      "max": 60000,
      "description": "Connection timeout in ms"
    }
  ]
}
```

### 3.3 Command 模版
定义每个具体命令的输入（Params）和输出（Result）。

```json
"commands": [
  {
    "name": "scan.capture",
    "description": "Trigger a single capture",
    "mode": "request", // request | stream (one-way)
    "params": {
      "fields": [
        {
          "name": "exposure_time",
          "type": "integer",
          "description": "Exposure time in microseconds",
          "default": 5000,
          "min": 100,
          "max": 100000
        },
        {
          "name": "roi",
          "type": "object",
          "description": "Region of Interest",
          "fields": [ // 嵌套结构
             { "name": "x", "type": "integer" },
             { "name": "y", "type": "integer" },
             { "name": "w", "type": "integer" },
             { "name": "h", "type": "integer" }
          ]
        }
      ]
    },
    "returns": {
      "type": "object",
      "description": "Capture result metadata",
      "fields": [
         { "name": "image_path", "type": "string", "format": "path" },
         { "name": "timestamp", "type": "integer" }
      ]
    }
  }
]
```

### 3.4 支持的数据类型 (Type System)
支持以下基础类型，用于 UI 渲染器选择组件：
- `string`: 文本框
- `integer`: 整数微调框（支持 min/max/step）
- `float`: 浮点数输入
- `boolean`: Checkbox / Switch
- `enum`: 下拉框 (Combo Box)
- `file/path`: 文件选择器
- `object`: 下一级折叠面板
- `array`: 动态增删列表

---

## 4. Driver 端详细设计 (SDK)

为了方便开发者，我们需要提供一套 C++ Helper 类库，通过“声明式”写法注册命令。

### 4.1 注册中心 (Registry)

```cpp
// 伪代码示例：在 Driver 初始化时注册
void MyDriver::registerMeta() {
    using namespace xmeta;

    // 1. 注册全局配置
    registry.config()
        .addField(Field("device_ip", Type::String).desc("Device IP").def("192.168.1.10").required())
        .addField(Field("debug_mode", Type::Bool).desc("Enable debug logs").def(false));

    // 2. 注册命令
    registry.command("scan.start")
        .desc("Start scanning process")
        .param(Field("fps", Type::Integer).min(1).max(60).def(30).desc("Frames per second"))
        .param(Field("mode", Type::Enum).options({"fast", "high_quality"}).def("fast"))
        .handler([this](const QJsonObject& params, Context& ctx) {
            // 自动参数校验已由框架完成
            int fps = params["fps"].toInt();
            QString mode = params["mode"].toString();
            return this->doScan(fps, mode); 
        });
}
```

### 4.2 自动实现 `get_meta`
Driver SDK 内部自动拦截 `"cmd": "meta.get"` 请求，序列化 Registry 中的数据结构为 JSON 并返回，开发者无需手动实现该命令。

### 4.3 强类型绑定 (Advanced)
进一步，利用 C++ 模板技术，可以做到参数到结构体的自动映射：

```cpp
struct ScanParams {
    int fps;
    std::string mode;
};
// 宏反射定义 (类似 NLOHMANN_JSON)
META_STRUCT(ScanParams, fps, mode);

// 注册
registry.command<ScanParams>("scan.start", [](const ScanParams& p, Context& ctx) {
    // p 已经是反序列化好的结构体
});
```
*注：第一阶段可先实现 4.1 的动态注册方式，更加灵活且无编译期反射负担。*

---

## 5. Host 端详细设计

### 5.1 Meta 解析器
Host 收到 JSON 后，解析为内部的 `MetaModel` 树状结构。

```cpp
struct MetaField {
    QString name;
    QString type;
    QVariant defaultValue;
    // ... constraints
};

struct MetaCommand {
    QString name;
    QList<MetaField> params;
    // ...
};
```

### 5.2 自动 UI 生成器 (Auto-UI Generator)
实现一个基于 Qt (Widgets/QML) 或 Web 的渲染引擎：

1.  **FormBuilder**: 输入 `List<MetaField>`，输出 `QWidget*` 或 HTML 表单。
2.  **Validator**: 输入用户填写的 Form 数据 + Meta 约束，输出是否合法。
3.  **CommandInvoker**: 点击“发送”时，将表单数据组装为 `{"cmd": "...", "data": {...}}` JSON 自动发出。

### 5.3 交互体验优化
-   **默认值填充**：UI 首次加载时自动填入 default 值。
-   **校验反馈**：输入超出 min/max 时实时变红。
-   **枚举展示**：自动使用 ComboBox。
-   **文件类型**：自动调用系统文件选择对话框。

---

## 6. 开发流程 (Workflow)

### 阶段 1：定义契约
开发者新建 Driver 时，先在 `main.cpp` 或 `Init` 函数中写 Meta 定义代码。这是“设计”过程。

### 阶段 2：验证 UI
编译并运行 Driver，Host 连接后立即就能看到生成的 UI。开发者可以在不写具体业务逻辑的情况下，先检查参数定义是否合理，UI 交互是否顺畅。

### 阶段 3：实现逻辑
在 Handler lambda 或回调函数中填充真正的业务代码。

---

## 7. 下一步实施计划

1.  **SDK Core 开发** (Driver 端):
    -   实现 `MetadataRegistry` 类。
    -   实现 JSON 序列化器。
    -   集成到现有的 `DriverCore` 中，自动处理 `meta.get`。

2.  **Host 适配**:
    -   实现 `MetaClient`，在连接 Driver 后尝试拉取 Metadata。
    -   开发通用调试面板 `MetaDebugPanel`，根据 Meta 动态生成界面。
3.  **Demo 验证**:
    -   改造现有的 `stdio_driver` 示例，增加 Meta 定义，验证整条链路。

---

## 补充：示例 JSON

```json
{
    "cmd": "meta.get",
    "data": {}
}

// Response
{
    "status": "done",
    "code": 0,
    "version": "1.0",
    "commands": [
        {
            "name": "greet",
            "desc": "Say hello to someone",
            "params": [
                { "name": "who", "type": "string", "def": "World" },
                { "name": "times", "type": "int", "min": 1, "max": 10, "def": 1 }
            ]
        }
    ]
}
```
