# 里程碑 26：TypeScript 声明文件生成

## 1. 目标

在现有 `DocGenerator` 类中新增 `toTypeScript()` 方法，支持 Driver 通过 `--export-doc=ts` 导出 `.d.ts` 类型声明文件，为 JS/TS 开发提供类型提示。

## 2. 技术要点

### 2.1 类型映射规则

| FieldType | TypeScript 类型 | 说明 |
|-----------|----------------|------|
| `String` | `string` | |
| `Int` | `number` | |
| `Int64` | `number` | JS 无 int64 |
| `Double` | `number` | |
| `Bool` | `boolean` | |
| `Object` | `{ [field]: type }` | 递归生成嵌套接口 |
| `Array` | `type[]` | 元素类型由 items 决定 |
| `Enum` | `"val1" \| "val2"` | 字符串字面量联合类型 |
| `Any` | `any` | |

### 2.2 生成结构

1. 每个命令生成 `XxxParams` 和 `XxxResult` 接口
2. 可选字段标记 `?`，必填字段无 `?`
3. 默认值写入 JSDoc `@default`
4. 生成 `DriverProxy` 接口汇总所有命令方法签名
5. 包含 `$driver`、`$meta`、`$close()` 内置成员

### 2.3 集成到 ConsoleArgs

- `--export-doc=ts` 输出到 stdout
- `--export-doc=ts=<path>` 写入指定文件
- 与现有 `--export-doc=markdown`、`--export-doc=html` 等格式保持一致的等号分隔语法

## 3. 实现步骤

### 3.1 DocGenerator 扩展

```cpp
// src/stdiolink/doc/doc_generator.h — 新增方法
class DocGenerator {
public:
    // ... 现有方法 ...
    static QString toTypeScript(const meta::DriverMeta& meta);

private:
    // TS helpers
    static QString fieldTypeToTs(meta::FieldType type);
    static QString fieldToTsType(const meta::FieldMeta& field);
    static QString generateInterface(const QString& name,
                                     const QVector<meta::FieldMeta>& fields,
                                     int indent = 0);
};
```

### 3.2 类型映射核心逻辑

```cpp
QString DocGenerator::fieldTypeToTs(meta::FieldType type) {
    switch (type) {
    case meta::FieldType::String: return "string";
    case meta::FieldType::Int:
    case meta::FieldType::Int64:
    case meta::FieldType::Double:  return "number";
    case meta::FieldType::Bool:    return "boolean";
    case meta::FieldType::Any:     return "any";
    default: return "any";
    }
}

QString DocGenerator::fieldToTsType(const meta::FieldMeta& field) {
    if (field.type == meta::FieldType::Enum) {
        QStringList literals;
        for (const auto& v : field.constraints.enumValues)
            literals << QString("\"%1\"").arg(v.toString());
        return literals.join(" | ");
    }
    if (field.type == meta::FieldType::Array) {
        if (field.items)
            return fieldToTsType(*field.items) + "[]";
        return "any[]";
    }
    if (field.type == meta::FieldType::Object) {
        if (field.fields.isEmpty())
            return "Record<string, any>";
        // 内联对象类型
        QStringList members;
        for (const auto& f : field.fields) {
            QString opt = f.required ? "" : "?";
            members << QString("    %1%2: %3;")
                       .arg(f.name, opt, fieldToTsType(f));
        }
        return "{\n" + members.join("\n") + "\n}";
    }
    return fieldTypeToTs(field.type);
}
```

### 3.3 生成输出示例

以 calculator_driver 为例，生成的 `.d.ts` 文件：

```typescript
/**
 * Calculator - 计算器 Driver
 * @version 1.0.0
 * @vendor demo
 */

/** add 命令参数 */
export interface AddParams {
    /** 第一个操作数 */
    a: number;
    /** 第二个操作数 */
    b: number;
}

/** add 命令返回值 */
export interface AddResult {
    result: number;
}

/** Driver 代理接口 */
export interface CalculatorDriver {
    add(params: AddParams): AddResult;
    subtract(params: SubtractParams): SubtractResult;

    readonly $driver: Driver;
    readonly $meta: object;
    $close(): void;
}
```

### 3.4 ConsoleArgs 集成

```cpp
// 在现有 format 判断中新增 "ts" 分支
if (format == "ts" || format == "typescript" || format == "dts") {
    output = DocGenerator::toTypeScript(meta);
}
```

## 4. 文件清单

| 文件 | 说明 |
|------|------|
| `src/stdiolink/doc/doc_generator.h` | 更新：新增 toTypeScript 声明 |
| `src/stdiolink/doc/doc_generator.cpp` | 更新：新增 toTypeScript 实现 |
| `src/stdiolink/driver/driver_core.cpp` | 更新：ConsoleArgs 新增 ts 格式 |

## 5. 验收标准

1. `calculator_driver.exe --export-doc=ts` 输出完整 `.d.ts` 内容到 stdout
2. `calculator_driver.exe --export-doc=ts=calc.d.ts` 写入文件
3. String 字段映射为 `string`
4. Int/Int64/Double 字段映射为 `number`
5. Bool 字段映射为 `boolean`
6. Enum 字段生成字符串字面量联合类型
7. Object 字段递归生成嵌套接口或内联类型
8. Array 字段根据 items 生成 `Type[]`
9. 可选字段标记 `?`
10. 每个命令生成 `XxxParams` 和 `XxxResult` 接口
11. 生成 Driver 代理接口包含所有命令方法签名
12. JSDoc 注释包含 description 信息

## 6. 单元测试用例

### 6.1 类型映射测试

```cpp
#include <gtest/gtest.h>
#include "stdiolink/doc/doc_generator.h"
#include "stdiolink/driver/meta_builder.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class TsDeclGeneratorTest : public ::testing::Test {
protected:
    // 构建一个简单的测试 DriverMeta
    DriverMeta buildSimpleMeta() {
        return DriverMetaBuilder()
            .info("test_driver", "TestDriver", "1.0.0", "Test")
            .command(CommandBuilder("add")
                .description("加法运算")
                .param(FieldBuilder("a", FieldType::Int)
                    .required().description("第一个操作数"))
                .param(FieldBuilder("b", FieldType::Int)
                    .required().description("第二个操作数"))
                .returns(FieldType::Object)
                .returnField(FieldBuilder("result", FieldType::Int)))
            .build();
    }
};
```

### 6.2 基础类型映射测试

```cpp
TEST_F(TsDeclGeneratorTest, BasicTypeMapping) {
    auto meta = buildSimpleMeta();
    QString ts = DocGenerator::toTypeScript(meta);

    // 应包含 interface 定义
    EXPECT_TRUE(ts.contains("interface AddParams"));
    EXPECT_TRUE(ts.contains("interface AddResult"));

    // Int 映射为 number
    EXPECT_TRUE(ts.contains("a: number"));
    EXPECT_TRUE(ts.contains("b: number"));
    EXPECT_TRUE(ts.contains("result: number"));
}
```

### 6.3 字符串与布尔类型测试

```cpp
TEST_F(TsDeclGeneratorTest, StringAndBoolMapping) {
    auto meta = DriverMetaBuilder()
        .info("test", "Test", "1.0.0")
        .command(CommandBuilder("greet")
            .param(FieldBuilder("name", FieldType::String).required())
            .param(FieldBuilder("loud", FieldType::Bool))
            .returns(FieldType::Object)
            .returnField(FieldBuilder("message", FieldType::String)))
        .build();

    QString ts = DocGenerator::toTypeScript(meta);
    EXPECT_TRUE(ts.contains("name: string"));
    EXPECT_TRUE(ts.contains("loud"));
    EXPECT_TRUE(ts.contains("boolean"));
    EXPECT_TRUE(ts.contains("message: string"));
}
```

### 6.4 枚举类型测试

```cpp
TEST_F(TsDeclGeneratorTest, EnumMapping) {
    auto meta = DriverMetaBuilder()
        .info("test", "Test", "1.0.0")
        .command(CommandBuilder("setMode")
            .param(FieldBuilder("mode", FieldType::Enum)
                .required()
                .enumValues(QStringList{"fast", "slow", "auto"})))
        .build();

    QString ts = DocGenerator::toTypeScript(meta);
    EXPECT_TRUE(ts.contains("\"fast\""));
    EXPECT_TRUE(ts.contains("\"slow\""));
    EXPECT_TRUE(ts.contains("\"auto\""));
    EXPECT_TRUE(ts.contains("|"));
}
```

### 6.5 数组类型测试

```cpp
TEST_F(TsDeclGeneratorTest, ArrayMapping) {
    auto meta = DriverMetaBuilder()
        .info("test", "Test", "1.0.0")
        .command(CommandBuilder("batch")
            .param(FieldBuilder("items", FieldType::Array)
                .required()
                .items(FieldBuilder("item", FieldType::String))))
        .build();

    QString ts = DocGenerator::toTypeScript(meta);
    EXPECT_TRUE(ts.contains("string[]"));
}
```

### 6.6 可选字段与 JSDoc 测试

```cpp
TEST_F(TsDeclGeneratorTest, OptionalField) {
    auto meta = DriverMetaBuilder()
        .info("test", "Test", "1.0.0")
        .command(CommandBuilder("scan")
            .param(FieldBuilder("fps", FieldType::Int))
            .param(FieldBuilder("name", FieldType::String).required()))
        .build();

    QString ts = DocGenerator::toTypeScript(meta);
    // fps 可选，应有 ?
    EXPECT_TRUE(ts.contains("fps?"));
    // name 必填，不应有 ?
    EXPECT_TRUE(ts.contains("name: string"));
    EXPECT_FALSE(ts.contains("name?"));
}

TEST_F(TsDeclGeneratorTest, JsDocComments) {
    auto meta = buildSimpleMeta();
    QString ts = DocGenerator::toTypeScript(meta);
    // 应包含描述注释
    EXPECT_TRUE(ts.contains("加法运算") || ts.contains("/**"));
}
```

### 6.7 Driver 代理接口测试

```cpp
TEST_F(TsDeclGeneratorTest, DriverProxyInterface) {
    auto meta = buildSimpleMeta();
    QString ts = DocGenerator::toTypeScript(meta);

    // 应包含 Driver 代理接口
    EXPECT_TRUE(ts.contains("interface"));
    // 应包含 $close 方法
    EXPECT_TRUE(ts.contains("$close"));
    // 应包含 $meta 字段
    EXPECT_TRUE(ts.contains("$meta"));
    // 应包含 $driver 字段
    EXPECT_TRUE(ts.contains("$driver"));
}
```

### 6.8 CLI 集成测试

```cpp
TEST_F(TsDeclGeneratorTest, ExportDocTsFlag) {
    // 通过 QProcess 调用 calculator_driver --export-doc=ts
    QString exe = QCoreApplication::applicationDirPath()
                  + "/calculator_driver";
#ifdef Q_OS_WIN
    exe += ".exe";
#endif
    QProcess proc;
    proc.start(exe, {"--export-doc=ts"});
    proc.waitForFinished(5000);

    EXPECT_EQ(proc.exitCode(), 0);
    QString output = QString::fromUtf8(proc.readAllStandardOutput());
    EXPECT_TRUE(output.contains("interface"));
    EXPECT_TRUE(output.contains("number"));
}

TEST_F(TsDeclGeneratorTest, EmptyMeta) {
    auto meta = DriverMetaBuilder()
        .info("empty", "Empty", "1.0.0")
        .build();
    QString ts = DocGenerator::toTypeScript(meta);
    // 无命令时不应崩溃，应有基本结构
    EXPECT_FALSE(ts.isEmpty());
}
```

## 7. 依赖关系

- **前置依赖**：
  - stdiolink 核心库（DocGenerator、DriverMeta、MetaBuilder）
  - 里程碑 1-20 中的 `--export-doc` 体系
- **后续依赖**：
  - 里程碑 27（集成测试）：TS 声明的端到端验证
