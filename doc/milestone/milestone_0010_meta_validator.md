# 里程碑 10：参数验证与默认值填充

> **前置条件**: 里程碑 7、8 已完成
> **目标**: 实现自动参数校验和默认值填充

---

## 1. 目标

- 实现 `MetaValidator` 类进行参数类型和约束验证
- 实现 `DefaultFiller` 类填充缺省参数
- 实现 `TypeConverter` 类支持类型自动转换（可选）
- 在 DriverCore 中集成验证逻辑

---

## 2. 技术要点

### 2.1 严格类型匹配规则

| FieldType | 接受的 JSON 类型 | 拒绝的 JSON 类型 |
|-----------|------------------|------------------|
| String | string | number, bool, null |
| Int | number (整数) | number (小数), string |
| Int64 | number (整数) | number (小数), string |
| Double | number | string, bool, null |
| Bool | bool | string, number |
| Object | object | array, string |
| Array | array | object, string |
| Enum | string (在枚举列表中) | 不在列表中的 string |
| Any | 任意类型 | 无 |

### 2.2 类型自动转换规则（宽松模式）

| 源类型 | 目标类型 | 转换规则 |
|--------|----------|----------|
| string | Int | 解析为整数 |
| string | Double | 解析为浮点数 |
| string | Bool | "true"/"false"/"1"/"0" |
| number | String | 转为字符串 |
| number (int) | Bool | 0→false, 非0→true |

### 2.3 验证执行边界

1. Host 可做预转换与预校验（可选）
2. Driver 必须做最终校验
3. 若启用自动转换，必须在校验前进行

---

## 3. 实现步骤

### 3.1 创建 protocol/meta_validator.h

```cpp
#pragma once

#include "meta_types.h"

namespace stdiolink::meta {

/**
 * 验证结果
 */
struct ValidationResult {
    bool valid = true;
    QString errorField;
    QString errorMessage;
    int errorCode = 0;

    static ValidationResult ok() {
        return {true, {}, {}, 0};
    }

    static ValidationResult fail(const QString& field,
                                 const QString& msg,
                                 int code = 400) {
        return {false, field, msg, code};
    }

    QString toString() const {
        if (valid) return "OK";
        if (errorField.isEmpty()) return errorMessage;
        return QString("%1: %2").arg(errorField, errorMessage);
    }
};

/**
 * 元数据验证器
 */
class MetaValidator {
public:
    /**
     * 验证命令参数
     */
    static ValidationResult validateParams(
        const QJsonValue& data,
        const CommandMeta& cmd,
        bool allowUnknown = true);

    /**
     * 验证单个字段
     */
    static ValidationResult validateField(
        const QJsonValue& value,
        const FieldMeta& field);

    /**
     * 验证配置
     */
    static ValidationResult validateConfig(
        const QJsonObject& config,
        const ConfigSchema& schema);

private:
    static ValidationResult checkType(
        const QJsonValue& value,
        FieldType type);

    static ValidationResult checkConstraints(
        const QJsonValue& value,
        const FieldMeta& field);

    static ValidationResult validateObject(
        const QJsonObject& obj,
        const QVector<FieldMeta>& fields,
        bool allowUnknown);

    static ValidationResult validateArray(
        const QJsonArray& arr,
        const FieldMeta& field);
};

} // namespace stdiolink::meta
```

### 3.2 创建 protocol/meta_converter.h

```cpp
#pragma once

#include "meta_types.h"

namespace stdiolink::meta {

/**
 * 类型转换器
 */
class TypeConverter {
public:
    enum class Mode {
        Strict,  // 严格模式：不进行自动转换
        Lenient  // 宽松模式：允许自动转换
    };

    static QJsonValue convert(const QJsonValue& value,
                              FieldType targetType,
                              Mode mode,
                              bool& ok);

    static QJsonObject convertParams(const QJsonObject& data,
                                     const QVector<FieldMeta>& fields,
                                     Mode mode);
};

} // namespace stdiolink::meta
```

### 3.3 DefaultFiller 实现

```cpp
// protocol/meta_validator.h 中添加

class DefaultFiller {
public:
    static QJsonObject fillDefaults(const QJsonObject& data,
                                    const QVector<FieldMeta>& fields);

    static QJsonObject fillDefaults(const QJsonObject& data,
                                    const CommandMeta& cmd);
};
```

### 3.4 MetaValidator 核心实现

```cpp
// protocol/meta_validator.cpp

ValidationResult MetaValidator::checkType(const QJsonValue& value,
                                          FieldType type) {
    switch (type) {
    case FieldType::String:
        if (!value.isString())
            return ValidationResult::fail("", "expected string");
        break;
    case FieldType::Int:
        if (!value.isDouble())
            return ValidationResult::fail("", "expected integer");
        if (value.toDouble() != static_cast<int>(value.toDouble()))
            return ValidationResult::fail("", "expected integer, got decimal");
        break;
    case FieldType::Int64:
        if (!value.isDouble())
            return ValidationResult::fail("", "expected integer");
        // 检查安全整数范围
        {
            double d = value.toDouble();
            if (d < -9007199254740992.0 || d > 9007199254740992.0)
                return ValidationResult::fail("", "integer out of safe range");
        }
        break;
    case FieldType::Double:
        if (!value.isDouble())
            return ValidationResult::fail("", "expected number");
        break;
    case FieldType::Bool:
        if (!value.isBool())
            return ValidationResult::fail("", "expected boolean");
        break;
    case FieldType::Object:
        if (!value.isObject())
            return ValidationResult::fail("", "expected object");
        break;
    case FieldType::Array:
        if (!value.isArray())
            return ValidationResult::fail("", "expected array");
        break;
    case FieldType::Any:
        break;
    default:
        break;
    }
    return ValidationResult::ok();
}

ValidationResult MetaValidator::checkConstraints(const QJsonValue& value,
                                                 const FieldMeta& field) {
    const auto& c = field.constraints;

    // 数值范围检查
    if (c.min.has_value() && value.isDouble()) {
        if (value.toDouble() < c.min.value())
            return ValidationResult::fail(field.name,
                QString("value %1 < min %2").arg(value.toDouble()).arg(*c.min));
    }
    if (c.max.has_value() && value.isDouble()) {
        if (value.toDouble() > c.max.value())
            return ValidationResult::fail(field.name,
                QString("value %1 > max %2").arg(value.toDouble()).arg(*c.max));
    }

    // 字符串长度检查
    if (value.isString()) {
        int len = value.toString().length();
        if (c.minLength.has_value() && len < *c.minLength)
            return ValidationResult::fail(field.name, "string too short");
        if (c.maxLength.has_value() && len > *c.maxLength)
            return ValidationResult::fail(field.name, "string too long");
        if (!c.pattern.isEmpty()) {
            QRegularExpression re(c.pattern);
            if (!re.match(value.toString()).hasMatch())
                return ValidationResult::fail(field.name, "pattern mismatch");
        }
    }

    // 枚举值检查
    if (field.type == FieldType::Enum && !c.enumValues.isEmpty()) {
        if (!c.enumValues.contains(value))
            return ValidationResult::fail(field.name, "invalid enum value");
    }

    // 数组长度检查
    if (value.isArray()) {
        int size = value.toArray().size();
        if (c.minItems.has_value() && size < *c.minItems)
            return ValidationResult::fail(field.name, "array too short");
        if (c.maxItems.has_value() && size > *c.maxItems)
            return ValidationResult::fail(field.name, "array too long");
    }

    return ValidationResult::ok();
}
```

### 3.5 在 DriverCore 中集成验证

```cpp
// driver_core.cpp 修改

void DriverCore::processOneLine(const QString& line) {
    // ... 解析请求

    // 处理 meta 命令
    if (handleMetaCommand(cmd, data, *m_responder)) {
        return;
    }

    // 自动参数验证
    if (m_metaHandler && m_metaHandler->autoValidateParams()) {
        const auto* cmdMeta = m_metaHandler->driverMeta().findCommand(cmd);
        if (cmdMeta) {
            // 填充默认值
            QJsonObject filledData = DefaultFiller::fillDefaults(
                data.toObject(), *cmdMeta);

            // 验证参数
            auto result = MetaValidator::validateParams(filledData, *cmdMeta);
            if (!result.valid) {
                m_responder->error(400, "ValidationFailed", result.toString());
                return;
            }

            // 使用填充后的数据调用处理器
            m_handler->handle(cmd, filledData, *m_responder);
            return;
        }
    }

    // 正常处理
    if (m_handler) {
        m_handler->handle(cmd, data, *m_responder);
    }
}
```

---

## 4. 验收标准

1. 缺失必填参数返回 400 错误
2. 类型不匹配返回明确错误信息
3. 约束条件（min/max/pattern 等）正确检查
4. 默认值正确填充到缺失字段
5. 嵌套 Object 和 Array 递归验证
6. 枚举值不在列表中时拒绝
7. int64 超出安全范围时拒绝

---

## 5. 单元测试用例

### 5.1 测试文件：tests/meta_validator_test.cpp

```cpp
#include <gtest/gtest.h>
#include "stdiolink/protocol/meta_validator.h"
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink::meta;

class MetaValidatorTest : public ::testing::Test {};

// 测试类型检查 - String
TEST_F(MetaValidatorTest, TypeCheckString) {
    FieldMeta field;
    field.name = "name";
    field.type = FieldType::String;

    auto r1 = MetaValidator::validateField(QJsonValue("hello"), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue(123), field);
    EXPECT_FALSE(r2.valid);
}

// 测试类型检查 - Int
TEST_F(MetaValidatorTest, TypeCheckInt) {
    FieldMeta field;
    field.name = "count";
    field.type = FieldType::Int;

    auto r1 = MetaValidator::validateField(QJsonValue(42), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue(3.14), field);
    EXPECT_FALSE(r2.valid);

    auto r3 = MetaValidator::validateField(QJsonValue("42"), field);
    EXPECT_FALSE(r3.valid);
}

// 测试类型检查 - Bool
TEST_F(MetaValidatorTest, TypeCheckBool) {
    FieldMeta field;
    field.name = "enabled";
    field.type = FieldType::Bool;

    auto r1 = MetaValidator::validateField(QJsonValue(true), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue("true"), field);
    EXPECT_FALSE(r2.valid);
}

// 测试数值范围约束
TEST_F(MetaValidatorTest, RangeConstraint) {
    FieldMeta field;
    field.name = "value";
    field.type = FieldType::Int;
    field.constraints.min = 0;
    field.constraints.max = 100;

    auto r1 = MetaValidator::validateField(QJsonValue(50), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue(-1), field);
    EXPECT_FALSE(r2.valid);

    auto r3 = MetaValidator::validateField(QJsonValue(101), field);
    EXPECT_FALSE(r3.valid);
}

// 测试字符串长度约束
TEST_F(MetaValidatorTest, StringLengthConstraint) {
    FieldMeta field;
    field.name = "username";
    field.type = FieldType::String;
    field.constraints.minLength = 3;
    field.constraints.maxLength = 20;

    auto r1 = MetaValidator::validateField(QJsonValue("alice"), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue("ab"), field);
    EXPECT_FALSE(r2.valid);
}

// 测试枚举值约束
TEST_F(MetaValidatorTest, EnumConstraint) {
    FieldMeta field;
    field.name = "mode";
    field.type = FieldType::Enum;
    field.constraints.enumValues = QJsonArray{"fast", "normal", "slow"};

    auto r1 = MetaValidator::validateField(QJsonValue("fast"), field);
    EXPECT_TRUE(r1.valid);

    auto r2 = MetaValidator::validateField(QJsonValue("invalid"), field);
    EXPECT_FALSE(r2.valid);
}

// 测试必填字段
TEST_F(MetaValidatorTest, RequiredField) {
    CommandMeta cmd;
    cmd.name = "test";
    FieldMeta f1, f2;
    f1.name = "required_field";
    f1.type = FieldType::String;
    f1.required = true;
    f2.name = "optional_field";
    f2.type = FieldType::String;
    f2.required = false;
    cmd.params = {f1, f2};

    QJsonObject data1{{"required_field", "value"}};
    auto r1 = MetaValidator::validateParams(data1, cmd);
    EXPECT_TRUE(r1.valid);

    QJsonObject data2{{"optional_field", "value"}};
    auto r2 = MetaValidator::validateParams(data2, cmd);
    EXPECT_FALSE(r2.valid);
}
```

### 5.2 DefaultFiller 测试

```cpp
// 测试默认值填充
TEST_F(MetaValidatorTest, DefaultFilling) {
    CommandMeta cmd;
    cmd.name = "test";
    FieldMeta f1, f2;
    f1.name = "timeout";
    f1.type = FieldType::Int;
    f1.defaultValue = 5000;
    f2.name = "mode";
    f2.type = FieldType::String;
    f2.defaultValue = "normal";
    cmd.params = {f1, f2};

    QJsonObject data{{"timeout", 3000}};
    QJsonObject filled = DefaultFiller::fillDefaults(data, cmd);

    EXPECT_EQ(filled["timeout"].toInt(), 3000);  // 保留原值
    EXPECT_EQ(filled["mode"].toString(), "normal");  // 填充默认值
}
```

---

## 6. 依赖关系

- **前置依赖**:
  - 里程碑 7（元数据类型定义）
  - 里程碑 8（DriverCore 扩展）
- **后续依赖**: 里程碑 11（Host 侧）可使用验证器

---

## 7. 文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/stdiolink/protocol/meta_validator.h` | 新增 | 验证器定义 |
| `src/stdiolink/protocol/meta_validator.cpp` | 新增 | 验证器实现 |
| `src/stdiolink/protocol/meta_converter.h` | 新增 | 类型转换器（可选） |
| `src/stdiolink/protocol/meta_converter.cpp` | 新增 | 转换器实现（可选） |
| `src/stdiolink/driver/driver_core.cpp` | 修改 | 集成验证逻辑 |
| `src/tests/meta_validator_test.cpp` | 新增 | 单元测试 |
