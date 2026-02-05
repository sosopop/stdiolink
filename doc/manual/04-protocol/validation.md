# 参数验证

stdiolink 提供自动参数验证功能，确保输入数据符合元数据定义的约束。

## MetaValidator 类

参数验证器，定义在 `stdiolink/protocol/meta_validator.h`。

### 验证结果

```cpp
struct ValidationResult {
    bool valid = true;
    QString errorField;
    QString errorMessage;
    int errorCode = 0;

    static ValidationResult ok();
    static ValidationResult fail(const QString& field,
                                 const QString& msg,
                                 int code = 400);
    QString toString() const;
};
```

### 静态方法

| 方法 | 说明 |
|------|------|
| `validateParams` | 验证命令参数 |
| `validateField` | 验证单个字段 |
| `validateConfig` | 验证配置对象 |

### validateParams

```cpp
static ValidationResult validateParams(
    const QJsonValue& data,
    const CommandMeta& cmd,
    bool allowUnknown = true
);
```

**参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| data | QJsonValue | 待验证的参数数据 |
| cmd | CommandMeta | 命令元数据 |
| allowUnknown | bool | 是否允许未定义的参数 |

**示例：**

```cpp
auto result = MetaValidator::validateParams(data, cmdMeta);
if (!result.valid) {
    qDebug() << "Validation failed:" << result.toString();
}
```

## DefaultFiller 类

默认值填充器，自动为缺失的可选参数填充默认值。

### 静态方法

```cpp
static QJsonObject fillDefaults(const QJsonObject& data,
                                const QVector<FieldMeta>& fields);

static QJsonObject fillDefaults(const QJsonObject& data,
                                const CommandMeta& cmd);
```

**示例：**

```cpp
QJsonObject params = {{"fps", 30}};
QJsonObject filled = DefaultFiller::fillDefaults(params, cmdMeta);
// filled 现在包含所有定义了默认值的字段
```

## 验证规则

### 类型检查

| 类型 | 验证规则 |
|------|----------|
| String | 必须是字符串 |
| Int | 必须是整数 |
| Double | 必须是数字 |
| Bool | 必须是布尔值 |
| Object | 必须是对象 |
| Array | 必须是数组 |
| Enum | 必须是枚举值之一 |

### 约束检查

| 约束 | 适用类型 | 说明 |
|------|----------|------|
| min/max | Int, Double | 数值范围 |
| minLength/maxLength | String | 字符串长度 |
| pattern | String | 正则表达式匹配 |
| enumValues | Enum | 枚举值列表 |
| minItems/maxItems | Array | 数组元素数量 |
