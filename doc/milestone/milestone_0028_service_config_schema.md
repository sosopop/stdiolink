# 里程碑 28：Service 配置 Schema 与注入

> **前置条件**: 里程碑 21-27（JS 引擎基础设施）已完成
> **目标**: 为 stdiolink_service 提供类型安全、可导出的外部配置参数机制，复用 meta 类型系统

---

## 1. 目标

- 定义服务配置 schema，复用 `FieldMeta`/`Constraints` 描述配置参数的类型、约束和默认值
- 实现命令行参数解析器 `ServiceArgs`，支持 `--config.key=value` 形式传入配置
- 实现 JSON 配置文件加载，支持 `--config-file=<path>` 从文件读取配置
- 实现 `ServiceConfigValidator`，对配置值进行类型检查和约束校验
- 将校验后的配置注入 JS 全局对象，脚本内可通过 `getConfig()` 读取
- 提供 JS 端 `import { defineConfig, getConfig } from "stdiolink"` API
- 支持 `--dump-config-schema` 导出配置 schema（JSON 格式）
- 绑定状态按 `JSRuntime/JSContext` 隔离，避免进程级全局静态状态污染

---

## 2. 设计概述

### 2.1 与 Driver MetaData 的关系

| 对比项 | Driver ConfigSchema | Service ConfigSchema |
|--------|-------------------|---------------------|
| 描述对象 | Driver 进程的配置 | stdiolink_service 自身运行参数 |
| 声明位置 | C++ 代码中通过 MetaBuilder 构建 | JS 脚本中通过 `defineConfig()` 声明 |
| 类型系统 | `FieldMeta` + `Constraints` | 复用同一套 `FieldMeta` + `Constraints` |
| 注入方式 | Host 通过 env/args/file/command | 命令行 `--config.x=y` 或配置文件 |
| 校验时机 | Driver 启动时或运行时 | 脚本调用 `defineConfig()` 时一次性校验 |
| 消费方 | Driver C++ 代码 | JS 脚本通过 `getConfig()` |

### 2.2 配置来源优先级（高→低）

| 优先级 | 来源 | 示例 |
|--------|------|------|
| 1 | 命令行参数 | `--config.timeout=5000` |
| 2 | 配置文件 | `--config-file=service.json` |
| 3 | Schema 默认值 | `defineConfig()` 中的 `default` |

### 2.3 支持的字段类型

复用 `FieldType` 枚举：

| FieldType | 命令行值格式 | 示例 |
|-----------|-------------|------|
| String | 原始字符串 | `--config.name=hello` |
| Int | 整数字面量 | `--config.port=8080` |
| Int64 | 64位整数字面量 | `--config.traceId=9007199254740991` |
| Double | 浮点字面量 | `--config.ratio=0.5` |
| Bool | `true`/`false` | `--config.debug=true` |
| Enum | 枚举值字符串 | `--config.mode=fast` |
| Array | JSON 数组文本 | `--config.tags=[1,2,3]` |
| Object | JSON 对象文本 | `--config.opts={"a":1}` |
| Any | 任意 JSON 字面量 | `--config.extra={"k":1}` |

### 2.4 整体流程

```
命令行参数 / 配置文件
        ↓
  ServiceArgs（解析 --config.* 和 --config-file，保留 raw literal）
        ↓
  原始配置 QJsonObject 存入 C++ 侧
        ↓
  JS 脚本执行 defineConfig(schema) → 收集 Schema
        ↓
  ServiceConfigValidator（按 schema 严格类型转换 + 类型检查 + 约束校验）
        ↓
  合并配置（命令行 > 文件 > 默认值）→ 冻结
        ↓
  getConfig() 返回只读配置对象
```

---

## 3. 技术要点

### 3.1 JS 端配置声明 API

```javascript
import { defineConfig, getConfig } from 'stdiolink';

defineConfig({
    port: {
        type: 'int', required: true,
        description: '监听端口',
        constraints: { min: 1, max: 65535 }
    },
    debug: {
        type: 'bool', default: false,
        description: '是否启用调试模式'
    },
    logLevel: {
        type: 'enum', default: 'info',
        constraints: { enumValues: ['debug', 'info', 'warn', 'error'] }
    },
    serverName: {
        type: 'string', required: true,
        constraints: { minLength: 1, maxLength: 64 }
    },
    retryCount: {
        type: 'int', default: 3,
        constraints: { min: 0, max: 10 }
    },
    tags: {
        type: 'array', default: [],
        items: { type: 'string' },
        constraints: { maxItems: 20 }
    }
});

const config = getConfig();
console.log(config.port);     // 8080（命令行传入）
console.log(config.debug);    // false（默认值）
```

### 3.2 命令行调用示例

```bash
stdiolink_service example.js \
    --config.port=8080 \
    --config.serverName=myService \
    --config.debug=true

# 使用配置文件
stdiolink_service example.js --config-file=config.json

# 配置文件 + 命令行覆盖（命令行优先）
stdiolink_service example.js --config-file=config.json --config.debug=true

# 导出 schema
stdiolink_service example.js --dump-config-schema
```

### 3.3 命令行参数解析规则

`--config.` 前缀后的部分作为配置键名，`=` 后的部分作为值：

```
--config.key=value        → { "key": value }
--config.a.b=value        → { "a": { "b": value } }（嵌套路径）
```

值的解析/转换策略：
- `ServiceArgs` 仅解析键路径与原始字符串，不做 schema 推断
- `defineConfig` 后由 `ServiceConfigValidator` 按 schema 严格转换
- 禁止“未知 schema 下自动推断”为 Bool/Int/Double 的策略
- 未知字段默认拒绝（`rejectUnknownFields=true`）
- 合并规则固定：object 深合并，array/scalar 整值覆盖

### 3.4 错误处理策略

| 错误场景 | 行为 |
|----------|------|
| 必填字段缺失 | `defineConfig()` 抛出 JS 异常，进程退出码 1 |
| 类型不匹配 | `defineConfig()` 抛出异常，含字段名和期望类型 |
| 约束校验失败 | `defineConfig()` 抛出异常，含字段名和约束描述 |
| 配置文件不存在 | 进程启动时 stderr 报错，退出码 2 |
| 配置文件 JSON 格式错误 | 进程启动时 stderr 报错，退出码 2 |
| 重复调用 `defineConfig()` | 抛出 JS 异常（仅允许调用一次） |
| `defineConfig()` 前调用 `getConfig()` | 返回空对象 `{}` |
| `--dump-config-schema` 模式下调用 `openDriver`/`exec` | 抛出异常，退出码 2 |
| `--dump-config-schema` 且脚本未调用 `defineConfig()` | stderr 报错，退出码 2 |

---

## 4. 实现步骤

### 4.1 创建 config/service_config_schema.h

复用 `FieldMeta` 作为字段描述，将 JS 端声明转换为 C++ 可操作的 schema。
实现上推荐直接封装/复用 `stdiolink::meta::ConfigSchema`，避免重复定义类型系统。

```cpp
#pragma once

#include <QJsonObject>
#include <QVector>
#include "stdiolink/protocol/meta_types.h"

namespace stdiolink_service {

struct ServiceConfigSchema {
    QVector<stdiolink::meta::FieldMeta> fields;

    /// 从 JS defineConfig() 的参数对象解析 schema
    static ServiceConfigSchema fromJsObject(const QJsonObject& obj);

    /// 导出为 JSON（用于 --dump-config-schema）
    QJsonObject toJson() const;

    /// 按名称查找字段，未找到返回 nullptr
    const stdiolink::meta::FieldMeta* findField(const QString& name) const;
};

} // namespace stdiolink_service
```

### 4.2 创建 config/service_args.h

```cpp
#pragma once

#include <QJsonObject>
#include <QStringList>

namespace stdiolink_service {

class ServiceArgs {
public:
    struct ParseResult {
        QString scriptPath;
        QJsonObject rawConfigValues; // --config.* 解析结果（叶子值均为 raw string）
        QString configFilePath;     // --config-file 路径
        bool dumpSchema = false;    // --dump-config-schema
        bool help = false;
        bool version = false;
        QString error;              // 解析错误信息
    };

    static ParseResult parse(const QStringList& appArgs); // app.arguments()，包含 argv[0]
    static QJsonObject loadConfigFile(const QString& filePath,
                                      QString& error);
private:
    static bool setNestedRawValue(QJsonObject& root,
                                  const QStringList& path,
                                  const QString& rawValue,
                                  QString& error);
};

} // namespace stdiolink_service
```

### 4.3 创建 config/service_config_validator.h

```cpp
#pragma once

#include <QJsonObject>
#include "service_config_schema.h"
#include "stdiolink/protocol/meta_validator.h"

namespace stdiolink_service {

using stdiolink::meta::ValidationResult;

enum class UnknownFieldPolicy {
    Reject,
    Allow
};

class ServiceConfigValidator {
public:
    /// 合并配置源并校验（cli > file > defaults）
    /// - object: 深合并
    /// - array/scalar: 高优先级整值覆盖
    static ValidationResult mergeAndValidate(
        const ServiceConfigSchema& schema,
        const QJsonObject& fileConfig,
        const QJsonObject& rawCliConfig,
        UnknownFieldPolicy unknownFieldPolicy,
        QJsonObject& mergedOut);

    /// 校验配置对象是否符合 schema
    static ValidationResult validate(
        const ServiceConfigSchema& schema,
        const QJsonObject& config);

    /// 用 schema 默认值填充缺失字段
    static QJsonObject fillDefaults(
        const ServiceConfigSchema& schema,
        const QJsonObject& config);
};

} // namespace stdiolink_service
```

### 4.4 创建 bindings/js_config.h

JS 端 `defineConfig()` 和 `getConfig()` 的 C++ 绑定。

```cpp
#pragma once

#include <quickjs.h>
#include <QJsonObject>
#include "config/service_config_schema.h"

namespace stdiolink_service {

class JsConfigBinding {
public:
    /// 绑定状态按 runtime 维度隔离
    static void attachRuntime(JSRuntime* rt);
    static void detachRuntime(JSRuntime* rt);

    /// 获取 defineConfig() 函数对象
    static JSValue getDefineConfigFunction(JSContext* ctx);

    /// 获取 getConfig() 函数对象
    static JSValue getGetConfigFunction(JSContext* ctx);

    /// 是否已调用过 defineConfig()
    static bool hasSchema(JSContext* ctx);

    /// 获取已声明的 schema
    static ServiceConfigSchema getSchema(JSContext* ctx);

    /// 设置原始配置（main 中解析后传入）
    static void setRawConfig(JSContext* ctx,
                             const QJsonObject& rawCli,
                             const QJsonObject& file,
                             bool dumpSchemaMode);

    /// 当前是否为 schema dump mode
    static bool isDumpSchemaMode(JSContext* ctx);

    /// 重置状态（测试用）
    static void reset(JSContext* ctx);
};

} // namespace stdiolink_service
```

### 4.5 修改 main.cpp 集成配置流程

```cpp
// main.cpp 核心流程变更（伪代码）
int main(int argc, char* argv[]) {
    // ... 初始化省略
    auto parsed = ServiceArgs::parse(app.arguments());
    if (!parsed.error.isEmpty()) { /* stderr 报错，退出码 2 */ }

    QJsonObject fileConfig;
    if (!parsed.configFilePath.isEmpty()) {
        QString err;
        fileConfig = ServiceArgs::loadConfigFile(
            parsed.configFilePath, err);
        if (!err.isEmpty()) { /* stderr 报错，退出码 2 */ }
    }

    JsEngine engine;
    ConsoleBridge::install(engine.context());

    JsConfigBinding::attachRuntime(engine.runtime());
    // 将原始配置传入绑定层
    JsConfigBinding::setRawConfig(engine.context(),
                                  parsed.rawConfigValues,
                                  fileConfig,
                                  parsed.dumpSchema);
    engine.registerModule("stdiolink", jsInitStdiolinkModule);

    int ret = engine.evalFile(parsed.scriptPath);

    // --dump-config-schema 模式
    if (parsed.dumpSchema) {
        if (ret != 0) { return ret; }
        if (!JsConfigBinding::hasSchema(engine.context())) {
            /* stderr 报错：dump 模式下脚本未调用 defineConfig */
            return 2;
        }
        auto schema = JsConfigBinding::getSchema(engine.context());
        QTextStream out(stdout);
        out << QJsonDocument(schema.toJson())
               .toJson(QJsonDocument::Indented);
        return 0;
    }
    // ... 事件循环省略
}
```

### 4.6 修改 js_stdiolink_module.cpp 注册导出

在 stdiolink 内置模块中新增 `defineConfig` 和 `getConfig` 两个导出项：

```cpp
// 现有导出：Driver, openDriver, exec
// 新增导出：
JS_SetModuleExport(ctx, m, "defineConfig",
                   JsConfigBinding::getDefineConfigFunction(ctx));
JS_SetModuleExport(ctx, m, "getConfig",
                   JsConfigBinding::getGetConfigFunction(ctx));
```

并且在 `--dump-config-schema` 模式下，`openDriver` / `Driver.start` / `exec` 必须直接抛错，
防止导出 schema 时触发外部副作用。

---

## 5. 验收标准

1. JS 脚本通过 `defineConfig()` 声明配置 schema，支持 `FieldType` 全量类型（String/Int/Int64/Double/Bool/Enum/Array/Object/Any）
2. `--config.key=value` 命令行参数正确解析，CLI 阶段仅保留 raw 值
3. raw CLI 值在 `defineConfig()` 后按 schema 严格转换
4. `--config-file=<path>` 正确加载 JSON 配置文件
5. 配置来源优先级正确：命令行 > 配置文件 > 默认值
6. 深合并规则生效：对象递归合并；数组/标量整值覆盖
7. 必填字段缺失时 `defineConfig()` 抛出明确错误信息
8. 类型不匹配时输出字段名和期望类型
9. 约束校验（min/max/minLength/maxLength/pattern/enumValues/minItems/maxItems）全部生效
10. `getConfig()` 返回只读的合并后配置对象
11. `defineConfig()` 重复调用时抛出错误
12. `--dump-config-schema` 输出完整 JSON schema 到 stdout
13. `--dump-config-schema` 模式下禁止 `openDriver` / `Driver.start` / `exec`
14. 未调用 `defineConfig()` 时，`getConfig()` 返回空对象，不影响正常运行

---

## 6. 单元测试用例

### 6.1 test_service_config_schema.cpp

测试 `ServiceConfigSchema` 的 JS 对象解析与序列化。

```cpp
// --- Schema 解析 ---

TEST(ServiceConfigSchema, ParseBasicTypes) {
    // 输入包含 string/int/double/bool 四种基本类型的 JSON 对象
    // 验证 fromJsObject() 正确生成对应 FieldMeta
    // 验证每个字段的 type、name、description、required、defaultValue
}

TEST(ServiceConfigSchema, ParseEnumType) {
    // 输入含 type:"enum" + constraints.enumValues 的字段
    // 验证 FieldMeta.type == Enum
    // 验证 constraints.enumValues 正确解析
}

TEST(ServiceConfigSchema, ParseArrayWithItems) {
    // 输入含 type:"array" + items:{type:"string"} 的字段
    // 验证 FieldMeta.type == Array
    // 验证 items 子字段正确设置
    // 验证 constraints.maxItems 正确解析
}

TEST(ServiceConfigSchema, ParseObjectType) {
    // 输入含 type:"object" 的字段
    // 验证 FieldMeta.type == Object
}

TEST(ServiceConfigSchema, ParseConstraints) {
    // 输入含 min/max/minLength/maxLength/pattern 的字段
    // 验证 Constraints 各属性正确映射
}

TEST(ServiceConfigSchema, ParseDefaultValues) {
    // 输入含 default 值的字段（各类型）
    // 验证 FieldMeta.defaultValue 正确设置
}

TEST(ServiceConfigSchema, ParseRequiredField) {
    // 输入 required:true 的字段
    // 验证 FieldMeta.required == true
}

TEST(ServiceConfigSchema, FindFieldByName) {
    // 构建含多个字段的 schema
    // findField("存在的名称") 返回非空指针
    // findField("不存在的名称") 返回 nullptr
}

TEST(ServiceConfigSchema, ToJsonRoundTrip) {
    // fromJsObject() → toJson() → 验证输出 JSON 结构完整
    // 包含 fields 数组，每个元素含 name/type/description/constraints/default
}

TEST(ServiceConfigSchema, EmptySchema) {
    // 输入空对象 {}
    // 验证 fields 为空，toJson() 返回空 fields 数组
}
```

### 6.2 test_service_args.cpp

命令行参数解析测试。

```cpp
#include <gtest/gtest.h>
#include "config/service_args.h"

using namespace stdiolink_service;

class ServiceArgsTest : public ::testing::Test {};

// 测试基本 --config.key=value 解析
TEST_F(ServiceArgsTest, ParseSimpleConfigArg) {
    QStringList args = {"stdiolink_service", "script.js", "--config.port=8080"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.error.isEmpty());
    EXPECT_EQ(result.scriptPath, "script.js");
    EXPECT_EQ(result.rawConfigValues["port"].toString(), "8080");
}

// 测试嵌套路径解析
TEST_F(ServiceArgsTest, ParseNestedConfigArg) {
    QStringList args = {"stdiolink_service", "script.js",
                        "--config.server.host=localhost",
                        "--config.server.port=3000"};
    auto result = ServiceArgs::parse(args);
    auto server = result.rawConfigValues["server"].toObject();
    EXPECT_EQ(server["host"].toString(), "localhost");
    EXPECT_EQ(server["port"].toString(), "3000");
}

// 测试非法路径段（如 --config..port=1）报错
TEST_F(ServiceArgsTest, RejectInvalidPathSegment) {
    QStringList args = {"stdiolink_service", "script.js", "--config..port=1"};
    auto result = ServiceArgs::parse(args);
    EXPECT_FALSE(result.error.isEmpty());
}

// 测试 bool 字面量保留为 raw string
TEST_F(ServiceArgsTest, KeepBoolLiteralAsRawString) {
    QStringList args = {"stdiolink_service", "script.js", "--config.debug=true"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.rawConfigValues["debug"].isString());
    EXPECT_EQ(result.rawConfigValues["debug"].toString(), "true");
}

// 测试 double 字面量保留为 raw string
TEST_F(ServiceArgsTest, KeepDoubleLiteralAsRawString) {
    QStringList args = {"stdiolink_service", "script.js", "--config.ratio=0.75"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.rawConfigValues["ratio"].isString());
    EXPECT_EQ(result.rawConfigValues["ratio"].toString(), "0.75");
}

// 测试字符串值（raw string）
TEST_F(ServiceArgsTest, KeepStringLiteralAsRawString) {
    QStringList args = {"stdiolink_service", "script.js", "--config.name=hello"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.rawConfigValues["name"].isString());
    EXPECT_EQ(result.rawConfigValues["name"].toString(), "hello");
}

// 测试 --config-file 提取
TEST_F(ServiceArgsTest, ExtractConfigFilePath) {
    QStringList args = {"stdiolink_service", "script.js",
                        "--config-file=config.json"};
    auto result = ServiceArgs::parse(args);
    EXPECT_EQ(result.configFilePath, "config.json");
}

// 测试 --dump-config-schema 标志
TEST_F(ServiceArgsTest, DumpSchemaFlag) {
    QStringList args = {"stdiolink_service", "script.js", "--dump-config-schema"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.dumpSchema);
}

// 测试缺少脚本路径
TEST_F(ServiceArgsTest, MissingScriptPath) {
    QStringList args = {"stdiolink_service", "--config.port=8080"};
    auto result = ServiceArgs::parse(args);
    EXPECT_FALSE(result.error.isEmpty());
}

// 测试 JSON 数组字面量保留为 raw string
TEST_F(ServiceArgsTest, KeepJsonArrayLiteralAsRawString) {
    QStringList args = {"stdiolink_service", "script.js", "--config.tags=[1,2,3]"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.rawConfigValues["tags"].isString());
    EXPECT_EQ(result.rawConfigValues["tags"].toString(), "[1,2,3]");
}

// 测试 JSON 对象字面量保留为 raw string
TEST_F(ServiceArgsTest, KeepJsonObjectLiteralAsRawString) {
    QStringList args = {"stdiolink_service", "script.js", R"(--config.opts={"a":1})"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.rawConfigValues["opts"].isString());
    EXPECT_EQ(result.rawConfigValues["opts"].toString(), R"({"a":1})");
}

// 测试多个 config 参数同时传入
TEST_F(ServiceArgsTest, MultipleConfigArgs) {
    QStringList args = {"stdiolink_service", "script.js",
                        "--config.port=8080",
                        "--config.name=test",
                        "--config.debug=false"};
    auto result = ServiceArgs::parse(args);
    EXPECT_EQ(result.rawConfigValues.size(), 3);
}

// 测试配置文件加载：正常 JSON
TEST_F(ServiceArgsTest, LoadConfigFileValid) {
    // 写入临时 JSON 文件，调用 loadConfigFile()
    // 验证返回的 QJsonObject 内容正确
}

// 测试配置文件加载：文件不存在
TEST_F(ServiceArgsTest, LoadConfigFileNotFound) {
    QString err;
    auto obj = ServiceArgs::loadConfigFile("nonexistent.json", err);
    EXPECT_FALSE(err.isEmpty());
}

// 测试配置文件加载：JSON 格式错误
TEST_F(ServiceArgsTest, LoadConfigFileMalformed) {
    // 写入非法 JSON 内容的临时文件
    // 验证 error 非空
}
```

### 6.3 test_service_config_validator.cpp

配置校验与合并测试。

```cpp
#include <gtest/gtest.h>
#include "config/service_config_validator.h"
#include "config/service_config_schema.h"

using namespace stdiolink_service;
using namespace stdiolink::meta;

class ServiceConfigValidatorTest : public ::testing::Test {
protected:
    ServiceConfigSchema makeSchema() {
        // 构建含 port(int,required), debug(bool,default=false),
        // mode(enum), name(string) 的 schema
        ServiceConfigSchema schema;
        FieldMeta port;
        port.name = "port"; port.type = FieldType::Int;
        port.required = true;
        port.constraints.min = 1; port.constraints.max = 65535;

        FieldMeta debug;
        debug.name = "debug"; debug.type = FieldType::Bool;
        debug.defaultValue = false;

        FieldMeta mode;
        mode.name = "mode"; mode.type = FieldType::Enum;
        mode.defaultValue = "normal";
        mode.constraints.enumValues = QJsonArray{"fast","normal","slow"};

        FieldMeta name;
        name.name = "name"; name.type = FieldType::String;
        name.required = true;
        name.constraints.minLength = 1;
        name.constraints.maxLength = 64;

        schema.fields = {port, debug, mode, name};
        return schema;
    }
};
```

```cpp
// 测试必填字段缺失
TEST_F(ServiceConfigValidatorTest, RequiredFieldMissing) {
    auto schema = makeSchema();
    QJsonObject config{{"debug", true}};  // 缺少 port 和 name
    auto r = ServiceConfigValidator::validate(schema, config);
    EXPECT_FALSE(r.valid);
    EXPECT_FALSE(r.errorField.isEmpty());
}

// 测试类型不匹配
TEST_F(ServiceConfigValidatorTest, TypeMismatch) {
    auto schema = makeSchema();
    QJsonObject config{{"port", "not_a_number"}, {"name", "test"}};
    auto r = ServiceConfigValidator::validate(schema, config);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.errorField, "port");
}

// 测试数值范围约束
TEST_F(ServiceConfigValidatorTest, RangeConstraint) {
    auto schema = makeSchema();
    QJsonObject config{{"port", 99999}, {"name", "test"}};
    auto r = ServiceConfigValidator::validate(schema, config);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.errorField, "port");
}

// 测试字符串长度约束
TEST_F(ServiceConfigValidatorTest, StringLengthConstraint) {
    auto schema = makeSchema();
    QString longName(65, 'x');
    QJsonObject config{{"port", 8080}, {"name", longName}};
    auto r = ServiceConfigValidator::validate(schema, config);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.errorField, "name");
}

// 测试枚举值约束
TEST_F(ServiceConfigValidatorTest, EnumConstraint) {
    auto schema = makeSchema();
    QJsonObject config{{"port", 8080}, {"name", "test"},
                       {"mode", "invalid"}};
    auto r = ServiceConfigValidator::validate(schema, config);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.errorField, "mode");
}

// 测试默认值填充
TEST_F(ServiceConfigValidatorTest, FillDefaults) {
    auto schema = makeSchema();
    QJsonObject config{{"port", 8080}, {"name", "test"}};
    auto filled = ServiceConfigValidator::fillDefaults(schema, config);
    EXPECT_EQ(filled["debug"].toBool(), false);
    EXPECT_EQ(filled["mode"].toString(), "normal");
    EXPECT_EQ(filled["port"].toInt(), 8080);  // 保留原值
}

// 测试合并优先级（cli > file > defaults）
TEST_F(ServiceConfigValidatorTest, MergePriority) {
    auto schema = makeSchema();
    QJsonObject fileConfig{{"port", 3000}, {"name", "file"},
                           {"debug", true}};
    QJsonObject cliConfig{{"port", 8080}};
    QJsonObject merged;
    auto r = ServiceConfigValidator::mergeAndValidate(
        schema, fileConfig, cliConfig, merged);
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(merged["port"].toInt(), 8080);   // cli 优先
    EXPECT_EQ(merged["name"].toString(), "file"); // file 补充
    EXPECT_EQ(merged["debug"].toBool(), true);    // file 补充
    EXPECT_EQ(merged["mode"].toString(), "normal"); // default 补充
}

// 测试全部合法配置通过校验
TEST_F(ServiceConfigValidatorTest, ValidConfigPasses) {
    auto schema = makeSchema();
    QJsonObject config{{"port", 8080}, {"name", "myService"},
                       {"debug", false}, {"mode", "fast"}};
    auto r = ServiceConfigValidator::validate(schema, config);
    EXPECT_TRUE(r.valid);
}

// 测试对象深合并（object 字段递归合并）
TEST_F(ServiceConfigValidatorTest, DeepMergeObject) {
    // file: {"server":{"host":"127.0.0.1","port":3000}}
    // cli : {"server":{"port":"8080"}} (raw，后续按 schema 转换)
    // 结果应为 {"server":{"host":"127.0.0.1","port":8080}}
}

// 测试数组整值覆盖（不做元素级 merge）
TEST_F(ServiceConfigValidatorTest, ArrayReplaceInsteadOfMerge) {
    // file: {"tags":["a","b"]}
    // cli : {"tags":"[\"x\"]"} (raw)
    // 结果应为 {"tags":["x"]}
}

// 测试未知字段默认拒绝
TEST_F(ServiceConfigValidatorTest, RejectUnknownFieldByDefault) {
    // schema 不含 unknown
    // cli/file 含 unknown 时应返回 invalid
}
```

### 6.4 test_service_config_js.cpp

通过 JsEngine 端到端测试 `defineConfig()` 和 `getConfig()` 的 JS 绑定。

```cpp
#include <gtest/gtest.h>
#include <QJsonObject>
#include "engine/js_engine.h"
#include "engine/console_bridge.h"
#include "bindings/js_config.h"
#include "bindings/js_stdiolink_module.h"

using namespace stdiolink_service;

class ServiceConfigJsTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        ConsoleBridge::install(engine->context());
        engine->registerModule("stdiolink", jsInitStdiolinkModule);
    }
    void TearDown() override {
        JsConfigBinding::reset(engine->context());
        engine.reset();
    }
    std::unique_ptr<JsEngine> engine;
};
```

```cpp
// --- defineConfig 基本功能 ---

// 测试 defineConfig 声明后 getConfig 返回正确值
TEST_F(ServiceConfigJsTest, DefineAndGetConfig) {
    // 注入命令行配置
    JsConfigBinding::setRawConfig(
        engine->context(),
        QJsonObject{{"port", 8080}, {"name", "test"}},
        QJsonObject{},
        false);
    // 执行 JS 脚本
    int ret = engine->evalScript(R"(
        import { defineConfig, getConfig } from 'stdiolink';
        defineConfig({
            port: { type: 'int', required: true },
            name: { type: 'string', required: true },
            debug: { type: 'bool', default: false }
        });
        const cfg = getConfig();
        if (cfg.port !== 8080) throw new Error('port mismatch');
        if (cfg.name !== 'test') throw new Error('name mismatch');
        if (cfg.debug !== false) throw new Error('debug mismatch');
    )");
    EXPECT_EQ(ret, 0);
}

// 测试 getConfig 返回的对象是只读的（冻结）
TEST_F(ServiceConfigJsTest, ConfigIsReadOnly) {
    JsConfigBinding::setRawConfig(
        engine->context(),
        QJsonObject{{"port", 3000}},
        QJsonObject{},
        false);
    int ret = engine->evalScript(R"(
        import { defineConfig, getConfig } from 'stdiolink';
        defineConfig({
            port: { type: 'int', required: true }
        });
        const cfg = getConfig();
        try {
            cfg.port = 9999;
            throw new Error('should not reach');
        } catch (e) {
            if (e.message === 'should not reach') throw e;
            // TypeError expected — frozen object
        }
    )");
    EXPECT_EQ(ret, 0);
}

// --- defineConfig 错误场景 ---

// 测试重复调用 defineConfig 抛出异常
TEST_F(ServiceConfigJsTest, DuplicateDefineConfigThrows) {
    JsConfigBinding::setRawConfig(engine->context(), QJsonObject{}, QJsonObject{}, false);
    int ret = engine->evalScript(R"(
        import { defineConfig } from 'stdiolink';
        defineConfig({ a: { type: 'string', default: '' } });
        try {
            defineConfig({ b: { type: 'int', default: 0 } });
            throw new Error('should not reach');
        } catch (e) {
            if (e.message === 'should not reach') throw e;
            // 预期抛出 "defineConfig() can only be called once"
        }
    )");
    EXPECT_EQ(ret, 0);
}

// 测试必填字段缺失时 defineConfig 抛出异常
TEST_F(ServiceConfigJsTest, RequiredFieldMissingThrows) {
    JsConfigBinding::setRawConfig(engine->context(), QJsonObject{}, QJsonObject{}, false);
    // 脚本应因 defineConfig 内部校验失败而返回非 0
    int ret = engine->evalScript(R"(
        import { defineConfig } from 'stdiolink';
        defineConfig({
            port: { type: 'int', required: true }
        });
    )");
    EXPECT_NE(ret, 0);
}

// 测试类型不匹配时 defineConfig 抛出异常
TEST_F(ServiceConfigJsTest, TypeMismatchThrows) {
    JsConfigBinding::setRawConfig(
        engine->context(),
        QJsonObject{{"port", "not_a_number"}},
        QJsonObject{},
        false);
    int ret = engine->evalScript(R"(
        import { defineConfig } from 'stdiolink';
        defineConfig({
            port: { type: 'int', required: true }
        });
    )");
    EXPECT_NE(ret, 0);
}

// 测试约束校验失败时 defineConfig 抛出异常
TEST_F(ServiceConfigJsTest, ConstraintViolationThrows) {
    JsConfigBinding::setRawConfig(
        engine->context(),
        QJsonObject{{"port", 99999}},
        QJsonObject{},
        false);
    int ret = engine->evalScript(R"(
        import { defineConfig } from 'stdiolink';
        defineConfig({
            port: { type: 'int', required: true,
                    constraints: { min: 1, max: 65535 } }
        });
    )");
    EXPECT_NE(ret, 0);
}

// --- 配置来源优先级 ---

// 测试命令行配置覆盖配置文件
TEST_F(ServiceConfigJsTest, CliOverridesFileConfig) {
    JsConfigBinding::setRawConfig(
        engine->context(),
        QJsonObject{{"port", 9090}},           // cli
        QJsonObject{{"port", 3000}, {"name", "fromFile"}}, // file
        false);
    int ret = engine->evalScript(R"(
        import { defineConfig, getConfig } from 'stdiolink';
        defineConfig({
            port: { type: 'int', required: true },
            name: { type: 'string', required: true }
        });
        const cfg = getConfig();
        if (cfg.port !== 9090) throw new Error('cli should override file');
        if (cfg.name !== 'fromFile') throw new Error('file should fill name');
    )");
    EXPECT_EQ(ret, 0);
}

// --- getConfig 边界场景 ---

// 测试 defineConfig 前调用 getConfig 返回空对象
TEST_F(ServiceConfigJsTest, GetConfigBeforeDefineReturnsEmpty) {
    JsConfigBinding::setRawConfig(engine->context(), QJsonObject{}, QJsonObject{}, false);
    int ret = engine->evalScript(R"(
        import { getConfig } from 'stdiolink';
        const cfg = getConfig();
        if (Object.keys(cfg).length !== 0)
            throw new Error('expected empty');
    )");
    EXPECT_EQ(ret, 0);
}

// --- 全类型支持 ---

// 测试所有 FieldType 的声明与读取（含 Int64 / Any）
TEST_F(ServiceConfigJsTest, AllFieldTypesSupported) {
    JsConfigBinding::setRawConfig(
        engine->context(),
        QJsonObject{
            {"s", "hello"}, {"i", 42}, {"d", 3.14},
            {"i64", 9007199254740991.0},
            {"b", true}, {"e", "fast"},
            {"arr", QJsonArray{1, 2, 3}},
            {"obj", QJsonObject{{"k", "v"}}},
            {"anyv", QJsonObject{{"k", 1}}}
        },
        QJsonObject{},
        false);
    int ret = engine->evalScript(R"(
        import { defineConfig, getConfig } from 'stdiolink';
        defineConfig({
            s:   { type: 'string', required: true },
            i:   { type: 'int',    required: true },
            d:   { type: 'double', required: true },
            i64: { type: 'int64',  required: true },
            b:   { type: 'bool',   required: true },
            e:   { type: 'enum',   required: true,
                   constraints: { enumValues: ['fast','slow'] } },
            arr: { type: 'array',  default: [] },
            obj: { type: 'object', default: {} },
            anyv:{ type: 'any',    required: true }
        });
        const cfg = getConfig();
        if (cfg.s !== 'hello') throw new Error('string');
        if (cfg.i !== 42) throw new Error('int');
        if (Math.abs(cfg.d - 3.14) > 0.001) throw new Error('double');
        if (cfg.i64 !== 9007199254740991) throw new Error('int64');
        if (cfg.b !== true) throw new Error('bool');
        if (cfg.e !== 'fast') throw new Error('enum');
        if (cfg.arr.length !== 3) throw new Error('array');
        if (cfg.obj.k !== 'v') throw new Error('object');
        if (cfg.anyv.k !== 1) throw new Error('any');
    )");
    EXPECT_EQ(ret, 0);
}

// --- Schema 导出 ---

// 测试 defineConfig 后 C++ 侧可获取 schema
TEST_F(ServiceConfigJsTest, SchemaAccessibleFromCpp) {
    JsConfigBinding::setRawConfig(
        engine->context(),
        QJsonObject{{"port", 8080}},
        QJsonObject{},
        false);
    engine->evalScript(R"(
        import { defineConfig } from 'stdiolink';
        defineConfig({
            port: { type: 'int', required: true,
                    description: '监听端口',
                    constraints: { min: 1, max: 65535 } }
        });
    )");
    EXPECT_TRUE(JsConfigBinding::hasSchema(engine->context()));
    auto schema = JsConfigBinding::getSchema(engine->context());
    EXPECT_EQ(schema.fields.size(), 1);
    EXPECT_EQ(schema.fields[0].name, "port");
    auto json = schema.toJson();
    EXPECT_FALSE(json.isEmpty());
}
```

### 6.5 test_js_integration.cpp（补充）

补充以下集成测试，验证 `--dump-config-schema` 语义完整：

```cpp
TEST_F(JsIntegrationTest, DumpSchemaOutputsJson) {
    // 脚本仅 defineConfig，启动 service 时传 --dump-config-schema
    // 断言 exitCode==0 且 stdout 为合法 schema JSON
}

TEST_F(JsIntegrationTest, DumpSchemaWithoutDefineConfigFails) {
    // 脚本未调用 defineConfig
    // 断言 exitCode==2 且 stderr 包含明确提示
}

TEST_F(JsIntegrationTest, DumpSchemaModeBlocksOpenDriverAndExec) {
    // 脚本在 dump 模式尝试 openDriver / exec
    // 断言 exitCode==2，且 stderr 包含 side effect blocked
}
```

---

## 7. 依赖关系

### 7.1 前置里程碑

| 里程碑 | 依赖内容 |
|--------|---------|
| M7-M8 | `FieldMeta`、`FieldType`、`Constraints` 类型定义 |
| M10 | `MetaValidator`、`ValidationResult`、`DefaultFiller` |
| M21-M27 | JS 引擎基础设施（JsEngine、模块加载、stdiolink 内置模块） |

### 7.2 后续里程碑

无直接后续依赖。本里程碑为独立功能模块。

---

## 8. 文件清单

| 操作 | 文件路径 | 说明 |
|------|---------|------|
| 新增 | `src/stdiolink_service/config/service_config_schema.h` | Schema 结构定义 |
| 新增 | `src/stdiolink_service/config/service_config_schema.cpp` | Schema 解析与序列化实现 |
| 新增 | `src/stdiolink_service/config/service_args.h` | 命令行参数解析器声明 |
| 新增 | `src/stdiolink_service/config/service_args.cpp` | 参数解析、raw 值保存、配置文件加载实现 |
| 新增 | `src/stdiolink_service/config/service_config_validator.h` | 配置校验器声明 |
| 新增 | `src/stdiolink_service/config/service_config_validator.cpp` | strict 转换、深合并、校验、默认值填充实现 |
| 新增 | `src/stdiolink_service/bindings/js_config.h` | JS 绑定声明（defineConfig/getConfig） |
| 新增 | `src/stdiolink_service/bindings/js_config.cpp` | JS 绑定实现（runtime 作用域状态） |
| 修改 | `src/stdiolink_service/bindings/js_stdiolink_module.cpp` | 注册 defineConfig/getConfig 导出 |
| 修改 | `src/stdiolink_service/bindings/js_driver.cpp` | dump 模式下阻止副作用调用 |
| 修改 | `src/stdiolink_service/bindings/js_process.cpp` | dump 模式下阻止副作用调用 |
| 修改 | `src/stdiolink_service/main.cpp` | 集成 ServiceArgs 解析与配置注入流程 |
| 新增 | `src/tests/test_service_config_schema.cpp` | Schema 解析单元测试 |
| 新增 | `src/tests/test_service_args.cpp` | 命令行参数解析单元测试 |
| 新增 | `src/tests/test_service_config_validator.cpp` | 配置校验单元测试 |
| 新增 | `src/tests/test_service_config_js.cpp` | JS 端到端集成测试 |
| 修改 | `src/tests/test_js_integration.cpp` | dump 模式集成测试 |
| 修改 | `src/stdiolink_service/CMakeLists.txt` | 添加新增源文件到构建目标 |
| 修改 | `src/tests/CMakeLists.txt` | 添加新增测试文件到测试目标 |
