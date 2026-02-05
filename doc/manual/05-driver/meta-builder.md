# 元数据 Builder API

Builder API 提供流畅的接口来构建 Driver 元数据。

## FieldBuilder

字段构建器，用于定义参数字段：

```cpp
namespace stdiolink::meta {

class FieldBuilder {
public:
    explicit FieldBuilder(const QString& name, FieldType type);

    // 基本属性
    FieldBuilder& required(bool req = true);
    FieldBuilder& defaultValue(const QJsonValue& val);
    FieldBuilder& description(const QString& desc);

    // 数值约束
    FieldBuilder& range(double minVal, double maxVal);
    FieldBuilder& min(double val);
    FieldBuilder& max(double val);

    // 字符串约束
    FieldBuilder& minLength(int len);
    FieldBuilder& maxLength(int len);
    FieldBuilder& pattern(const QString& regex);

    // 枚举值
    FieldBuilder& enumValues(const QJsonArray& values);
    FieldBuilder& enumValues(const QStringList& values);

    // 格式提示
    FieldBuilder& format(const QString& fmt);

    // UI 提示
    FieldBuilder& widget(const QString& w);
    FieldBuilder& group(const QString& g);
    FieldBuilder& order(int o);
    FieldBuilder& placeholder(const QString& p);
    FieldBuilder& unit(const QString& u);
    FieldBuilder& advanced(bool adv = true);
    FieldBuilder& readonly(bool ro = true);

    // Object 类型
    FieldBuilder& addField(const FieldBuilder& field);
    FieldBuilder& requiredKeys(const QStringList& keys);
    FieldBuilder& additionalProperties(bool allowed);

    // Array 类型
    FieldBuilder& items(const FieldBuilder& item);
    FieldBuilder& minItems(int n);
    FieldBuilder& maxItems(int n);

    FieldMeta build() const;
};

}
```

## CommandBuilder

命令构建器：

```cpp
class CommandBuilder {
public:
    explicit CommandBuilder(const QString& name);

    CommandBuilder& description(const QString& desc);
    CommandBuilder& title(const QString& t);
    CommandBuilder& summary(const QString& s);

    // 参数
    CommandBuilder& param(const FieldBuilder& field);

    // 返回值
    CommandBuilder& returns(FieldType type, const QString& desc = {});
    CommandBuilder& returnField(const FieldBuilder& field);

    // 事件
    CommandBuilder& event(const QString& name, const QString& desc = {});

    // UI
    CommandBuilder& group(const QString& g);
    CommandBuilder& order(int o);

    CommandMeta build() const;
};
```

## DriverMetaBuilder

驱动元数据构建器：

```cpp
class DriverMetaBuilder {
public:
    DriverMetaBuilder& schemaVersion(const QString& ver);
    DriverMetaBuilder& info(const QString& id,
                            const QString& name,
                            const QString& version,
                            const QString& desc = {});
    DriverMetaBuilder& vendor(const QString& v);
    DriverMetaBuilder& entry(const QString& program,
                             const QStringList& defaultArgs = {});
    DriverMetaBuilder& capability(const QString& cap);
    DriverMetaBuilder& profile(const QString& prof);
    DriverMetaBuilder& configField(const FieldBuilder& field);
    DriverMetaBuilder& configApply(const QString& method,
                                   const QString& command = {});
    DriverMetaBuilder& command(const CommandBuilder& cmd);

    DriverMeta build() const;
};
```

## 使用示例

```cpp
using namespace stdiolink::meta;

DriverMeta meta = DriverMetaBuilder()
    .schemaVersion("1.0")
    .info("com.example.scanner", "Scanner", "1.0.0")
    .command(CommandBuilder("scan")
        .description("执行扫描")
        .param(FieldBuilder("fps", FieldType::Int)
            .required()
            .range(1, 60)
            .defaultValue(30)
            .unit("fps"))
        .param(FieldBuilder("duration", FieldType::Double)
            .range(0.1, 10.0)
            .defaultValue(1.0)))
    .build();
```
