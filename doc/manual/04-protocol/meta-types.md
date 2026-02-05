# 元数据类型

元数据类型定义在 `stdiolink/protocol/meta_types.h` 中，用于描述 Driver 的能力。

## FieldType 枚举

字段类型枚举，定义参数支持的数据类型：

```cpp
enum class FieldType {
    String,  // 字符串
    Int,     // 32位整数
    Int64,   // 64位整数
    Double,  // 浮点数
    Bool,    // 布尔值
    Object,  // 嵌套对象
    Array,   // 数组
    Enum,    // 枚举（字符串限定值）
    Any      // 任意类型
};
```

### 类型转换函数

```cpp
QString fieldTypeToString(FieldType type);
FieldType fieldTypeFromString(const QString& str);
```

## FieldMeta 结构

字段元数据，描述单个参数：

```cpp
struct FieldMeta {
    QString name;                         // 字段名
    FieldType type = FieldType::Any;      // 字段类型
    bool required = false;                // 是否必填
    QJsonValue defaultValue;              // 默认值
    QString description;                  // 描述
    Constraints constraints;              // 约束条件
    UIHint ui;                            // UI 提示
    QVector<FieldMeta> fields;            // Object 嵌套字段
    std::shared_ptr<FieldMeta> items;     // Array 元素 schema
    QStringList requiredKeys;             // Object 必填键
    bool additionalProperties = true;     // 是否允许额外属性
};
```

## Constraints 结构

字段约束条件：

```cpp
struct Constraints {
    std::optional<double> min;       // 最小值
    std::optional<double> max;       // 最大值
    std::optional<int> minLength;    // 最小长度
    std::optional<int> maxLength;    // 最大长度
    QString pattern;                 // 正则表达式
    QJsonArray enumValues;           // 枚举值列表
    QString format;                  // 格式提示
    std::optional<int> minItems;     // 数组最小元素数
    std::optional<int> maxItems;     // 数组最大元素数
};
```

## UIHint 结构

UI 渲染提示：

```cpp
struct UIHint {
    QString widget;       // 控件类型
    QString group;        // 分组名
    int order = 0;        // 排序权重
    QString placeholder;  // 占位符文本
    bool advanced = false;// 是否高级选项
    bool readonly = false;// 是否只读
    QString visibleIf;    // 条件显示表达式
    QString unit;         // 单位
    double step = 0;      // 步进值
};
```

## CommandMeta 结构

命令元数据：

```cpp
struct CommandMeta {
    QString name;                    // 命令名
    QString description;             // 描述
    QString title;                   // 显示标题
    QString summary;                 // 简短摘要
    QVector<FieldMeta> params;       // 参数列表
    ReturnMeta returns;              // 返回值
    QVector<EventMeta> events;       // 事件列表
    QVector<QJsonObject> errors;     // 错误定义
    QVector<QJsonObject> examples;   // 示例
    UIHint ui;                       // UI 提示
};
```

## DriverMeta 结构

驱动元数据（顶层结构）：

```cpp
struct DriverMeta {
    QString schemaVersion = "1.0";       // Schema 版本
    DriverInfo info;                     // 驱动信息
    ConfigSchema config;                 // 配置模式
    QVector<CommandMeta> commands;       // 命令列表
    QHash<QString, FieldMeta> types;     // 自定义类型
    QVector<QJsonObject> errors;         // 全局错误
    QVector<QJsonObject> examples;       // 全局示例

    const CommandMeta* findCommand(const QString& name) const;
};
```

## DriverInfo 结构

驱动基本信息：

```cpp
struct DriverInfo {
    QString id;                  // 唯一标识
    QString name;                // 显示名称
    QString version;             // 版本号
    QString description;         // 描述
    QString vendor;              // 供应商
    QJsonObject entry;           // 入口配置
    QStringList capabilities;    // 能力列表
    QStringList profiles;        // 支持的配置文件
};
```
