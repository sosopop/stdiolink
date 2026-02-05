# UI 表单生成器

`UiGenerator` 类用于从元数据生成 UI 表单描述。

## FormDesc 结构

```cpp
struct FormDesc {
    QString title;
    QString description;
    QJsonArray widgets;
};
```

## UiGenerator 类

```cpp
class UiGenerator {
public:
    static FormDesc generateCommandForm(
        const meta::CommandMeta& cmd);
    static FormDesc generateConfigForm(
        const meta::ConfigSchema& config);
    static QJsonObject toJson(const FormDesc& form);

    // 高级 UI 生成
    static QHash<QString, QVector<meta::FieldMeta>>
        groupFields(const QVector<meta::FieldMeta>& fields);
    static QVector<meta::FieldMeta>
        sortFields(const QVector<meta::FieldMeta>& fields);
};
```

## 使用示例

```cpp
const auto* meta = driver.queryMeta();
const auto* cmd = meta->findCommand("scan");

FormDesc form = UiGenerator::generateCommandForm(*cmd);
QJsonObject json = UiGenerator::toJson(form);

qDebug() << json;
```
