# 里程碑 31：config.schema.json 外部 Schema 加载

## 1. 目标

将配置 Schema 的来源从 JS 脚本内 `defineConfig()` 声明切换为外部文件 `config.schema.json`。移除 `defineConfig` JS API，改为系统启动时直接读取 `<service_dir>/config.schema.json` 文件解析 Schema。`getConfig()` 保留为只读配置访问接口，但不再依赖 `defineConfig()` 调用。

## 2. 设计概述

### 2.1 变更对比

| 对比项 | M28（当前） | M31（本里程碑） |
|--------|-----------|---------------|
| Schema 声明 | JS 脚本内 `defineConfig({...})` | 外部文件 `config.schema.json` |
| Schema 解析时机 | JS 运行时执行 `defineConfig()` | C++ 启动阶段读取文件 |
| `defineConfig` API | 存在，必须调用 | **移除** |
| `getConfig` API | 需先调用 `defineConfig` | 直接可用，配置在引擎初始化前已就绪 |
| `--dump-config-schema` | 需执行脚本收集 schema | 直接读取文件输出，不执行脚本 |
| `--help` 配置文档 | 需执行脚本收集 schema | 直接读取文件生成，不执行脚本 |

### 2.2 config.schema.json 文件格式

```json
{
  "port": {
    "type": "int",
    "required": true,
    "description": "监听端口",
    "constraints": { "min": 1, "max": 65535 }
  },
  "debug": {
    "type": "bool",
    "default": false,
    "description": "是否启用调试模式"
  },
  "logLevel": {
    "type": "enum",
    "default": "info",
    "constraints": { "enumValues": ["debug", "info", "warn", "error"] }
  }
}
```

格式与原 `defineConfig()` 参数对象完全一致，复用 `ServiceConfigSchema::fromJsObject()` 的解析逻辑。

### 2.3 整体流程变更

```
启动 stdiolink_service <service_dir>
        ↓
  ServiceDirectory::validate() 校验三文件
        ↓
  读取 config.schema.json → ServiceConfigSchema::fromJsonFile()
        ↓
  ServiceArgs 解析 --config.* / --config-file
        ↓
  ServiceConfigValidator 合并 + 校验（cli > file > defaults）
        ↓
  冻结配置注入 JS 引擎
        ↓
  getConfig() 直接返回只读配置
        ↓
  执行 index.js（无需调用 defineConfig）
```

## 3. 技术要点

### 3.1 ServiceConfigSchema 新增文件加载方法

```cpp
// service_config_schema.h 新增
struct ServiceConfigSchema {
    // ... 现有成员 ...

    /// 从 config.schema.json 文件加载 schema
    static ServiceConfigSchema fromJsonFile(const QString& filePath,
                                            QString& error);
};
```

实现逻辑：读取文件 → `QJsonDocument::fromJson()` → 校验为 JSON Object → 调用现有 `fromJsObject()` 解析。

### 3.2 JsConfigBinding 改造

- **移除** `getDefineConfigFunction()` 方法
- **移除** `hasSchema()` / `getSchema()` 方法（schema 不再由 JS 端产生）
- **保留** `getGetConfigFunction()` — `getConfig()` 仍为 JS 端读取配置的唯一接口
- **新增** `setMergedConfig()` — 在 C++ 侧完成合并校验后，将最终配置注入 JS 引擎
- **移除** `isDumpSchemaMode()` / `markBlockedSideEffect()` / `takeBlockedSideEffectFlag()` — dump 模式不再执行脚本，无需副作用拦截

```cpp
// js_config.h 改造后
class JsConfigBinding {
public:
    static void attachRuntime(JSRuntime* rt);
    static void detachRuntime(JSRuntime* rt);

    /// 获取 getConfig() 函数对象
    static JSValue getGetConfigFunction(JSContext* ctx);

    /// 注入已合并校验的最终配置（C++ 侧调用）
    static void setMergedConfig(JSContext* ctx,
                                const QJsonObject& mergedConfig);

    /// 重置状态（测试用）
    static void reset(JSContext* ctx);
};
```

### 3.3 main.cpp 核心流程变更

```cpp
int main(int argc, char* argv[]) {
    // ... 初始化省略
    auto parsed = ServiceArgs::parse(app.arguments());
    ServiceDirectory svcDir(parsed.serviceDir);

    // --help / --dump-config-schema 不再执行脚本
    // 直接读取 config.schema.json
    QString schemaErr;
    auto schema = ServiceConfigSchema::fromJsonFile(
        svcDir.configSchemaPath(), schemaErr);
    if (!schemaErr.isEmpty()) { /* stderr 报错，退出码 2 */ }

    if (parsed.dumpSchema) {
        QTextStream out(stdout);
        out << QJsonDocument(schema.toJson())
               .toJson(QJsonDocument::Indented);
        return 0;
    }

    if (parsed.help) {
        printHelp(manifest);
        QString configHelp = ServiceConfigHelp::generate(schema);
        if (!configHelp.isEmpty()) {
            QTextStream err(stderr);
            err << "\n" << configHelp;
        }
        return 0;
    }

    // 合并校验配置
    QJsonObject merged;
    auto vr = ServiceConfigValidator::mergeAndValidate(
        schema, fileConfig, parsed.rawConfigValues,
        UnknownFieldPolicy::Reject, merged);
    if (!vr.valid) { /* stderr 报错，退出码 1 */ }

    // 注入配置到 JS 引擎
    JsConfigBinding::setMergedConfig(engine.context(), merged);

    // 执行 index.js
    int ret = engine.evalFile(svcDir.entryPath());
    // ...
}
```

### 3.4 js_stdiolink_module.cpp 变更

- 移除 `defineConfig` 导出项
- 保留 `getConfig` 导出项
- 移除 dump 模式下对 `openDriver` / `exec` 的副作用拦截逻辑（dump 模式不再执行脚本）

### 3.5 错误处理变更

| 错误场景 | M28 行为 | M31 行为 |
|----------|---------|---------|
| config.schema.json 不存在 | N/A | 退出码 2，stderr 报错 |
| config.schema.json 非法 JSON | N/A | 退出码 2，stderr 报错 |
| config.schema.json 字段类型未知 | N/A | 退出码 2，stderr 报错 |
| 必填配置缺失 | `defineConfig()` 抛 JS 异常 | C++ 侧校验失败，退出码 1 |
| 类型不匹配 | `defineConfig()` 抛 JS 异常 | C++ 侧校验失败，退出码 1 |
| 重复调用 `defineConfig()` | 抛 JS 异常 | **不适用，已移除** |
| `defineConfig()` 前调用 `getConfig()` | 返回空对象 | `getConfig()` 始终返回已注入的配置 |

## 4. 实现步骤

### 4.1 ServiceConfigSchema 新增 fromJsonFile()

- 读取文件内容
- 解析为 QJsonDocument，校验为 Object
- 调用现有 `fromJsObject()` 复用解析逻辑
- 文件不存在 / 非法 JSON / 非 Object 均通过 error 参数返回

### 4.2 改造 JsConfigBinding

- 移除 `getDefineConfigFunction()` 及其内部实现
- 移除 `hasSchema()` / `getSchema()` / `setRawConfig()`
- 移除 `isDumpSchemaMode()` / `markBlockedSideEffect()` / `takeBlockedSideEffectFlag()`
- 新增 `setMergedConfig(ctx, mergedConfig)` — 将 QJsonObject 转为冻结的 JS 对象存入 context 状态
- `getGetConfigFunction()` 返回的函数直接读取已注入的冻结对象

### 4.3 改造 js_stdiolink_module.cpp

- 移除 `defineConfig` 的 `JS_SetModuleExport` 注册
- 移除 dump 模式下 `openDriver` / `Driver.start` / `exec` 的副作用拦截代码
- 保留 `getConfig` 导出

### 4.4 改造 main.cpp

- 在 JS 引擎初始化前完成 schema 加载、配置合并校验
- `--help` 和 `--dump-config-schema` 直接读取文件，不启动 JS 引擎
- 调用 `JsConfigBinding::setMergedConfig()` 注入最终配置
- 移除原有的 `setRawConfig()` 调用和 dump 模式脚本执行逻辑

### 4.5 清理废弃代码

- 删除 `js_config.cpp` 中 `defineConfig` 相关的所有实现代码
- 删除 `ConfigState` 中 `schemaDefined` / `rawCli` / `rawFile` / `dumpSchemaMode` / `blockedSideEffect` 等字段
- 简化 `ConfigState` 为仅包含 `mergedConfig` JSValue

## 5. 文件清单

| 操作 | 文件路径 | 说明 |
|------|---------|------|
| 修改 | `src/stdiolink_service/config/service_config_schema.h` | 新增 fromJsonFile() |
| 修改 | `src/stdiolink_service/config/service_config_schema.cpp` | 实现 fromJsonFile() |
| 修改 | `src/stdiolink_service/bindings/js_config.h` | 移除 defineConfig 相关，新增 setMergedConfig |
| 修改 | `src/stdiolink_service/bindings/js_config.cpp` | 大幅简化，移除 defineConfig 实现 |
| 修改 | `src/stdiolink_service/bindings/js_stdiolink_module.cpp` | 移除 defineConfig 导出 |
| 修改 | `src/stdiolink_service/bindings/js_driver.cpp` | 移除 dump 模式副作用拦截 |
| 修改 | `src/stdiolink_service/bindings/js_process.cpp` | 移除 dump 模式副作用拦截 |
| 修改 | `src/stdiolink_service/main.cpp` | 重构配置加载流程 |
| 修改 | `src/tests/test_service_config_schema.cpp` | 新增 fromJsonFile 测试 |
| 修改 | `src/tests/test_service_config_js.cpp` | 移除 defineConfig 测试，改写为 setMergedConfig 测试 |
| 修改 | `src/tests/test_js_integration.cpp` | 更新集成测试 |
| 修改 | `src/stdiolink_service/CMakeLists.txt` | 如有新文件则更新 |

## 6. 验收标准

1. `config.schema.json` 正确解析为 `ServiceConfigSchema`，支持全量 FieldType
2. `config.schema.json` 不存在时退出码 2，stderr 包含明确错误
3. `config.schema.json` 非法 JSON 时退出码 2
4. `config.schema.json` 字段类型未知时退出码 2
5. `--dump-config-schema` 输出经 `schema.toJson()` 标准化后的 JSON（不执行脚本），退出码 0。**注意：这是相对 M30 的行为收敛变更**——M30 中 dump 直接透传文件内容，M31 改为先解析再通过 `toJson()` 输出标准化结果，确保输出格式与内部类型系统一致
6. `--help` 直接从文件生成配置文档（不执行脚本）
7. `getConfig()` 在 JS 脚本中直接可用，返回已合并校验的配置
8. `getConfig()` 返回的对象是只读的（冻结）
9. `defineConfig` 不再作为 stdiolink 模块导出项
10. 配置合并校验在 C++ 侧完成，必填缺失 / 类型不匹配 / 约束违反均在启动阶段报错
11. 副作用拦截代码（js_driver / js_process 中的 dump 模式检查）已清理

## 7. 单元测试用例

### 7.1 test_service_config_schema.cpp（补充）

```cpp
// --- fromJsonFile 测试 ---

TEST(ServiceConfigSchema, FromJsonFileValid) {
    // 创建临时 config.schema.json 文件
    // 内容含 port(int,required), debug(bool,default)
    // 验证 fromJsonFile() 返回正确 schema
    // 验证 fields 数量、类型、约束
}

TEST(ServiceConfigSchema, FromJsonFileNotFound) {
    QString err;
    ServiceConfigSchema::fromJsonFile("nonexistent.json", err);
    EXPECT_FALSE(err.isEmpty());
}

TEST(ServiceConfigSchema, FromJsonFileMalformedJson) {
    // 创建包含非法 JSON 的临时文件
    // 验证 error 非空
}

TEST(ServiceConfigSchema, FromJsonFileNotObject) {
    // 创建包含 JSON 数组 "[]" 的临时文件
    // 验证 error 非空（期望 Object）
}

TEST(ServiceConfigSchema, FromJsonFileEmptyObject) {
    // 创建包含 "{}" 的临时文件
    // 验证 fields 为空，无错误
}
```

### 7.2 test_service_config_js.cpp（改写）

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

    /// 将 JS 代码写入临时文件并通过 evalFile 执行
    int evalCode(const QString& code) {
        QString path = m_tmpDir.path() + "/test_script.js";
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(code.toUtf8());
        f.close();
        return engine->evalFile(path);
    }

    QTemporaryDir m_tmpDir;
    std::unique_ptr<JsEngine> engine;
};
```

```cpp
// 测试 getConfig 直接返回已注入的配置
TEST_F(ServiceConfigJsTest, GetConfigReturnsInjectedConfig) {
    JsConfigBinding::attachRuntime(engine->runtime());
    JsConfigBinding::setMergedConfig(
        engine->context(),
        QJsonObject{{"port", 8080}, {"name", "test"}, {"debug", false}});

    int ret = evalCode(R"(
        import { getConfig } from 'stdiolink';
        const cfg = getConfig();
        if (cfg.port !== 8080) throw new Error('port mismatch');
        if (cfg.name !== 'test') throw new Error('name mismatch');
        if (cfg.debug !== false) throw new Error('debug mismatch');
    )");
    EXPECT_EQ(ret, 0);
}
```

```cpp
// 测试 getConfig 返回的对象是只读的
TEST_F(ServiceConfigJsTest, ConfigIsReadOnly) {
    JsConfigBinding::attachRuntime(engine->runtime());
    JsConfigBinding::setMergedConfig(
        engine->context(),
        QJsonObject{{"port", 3000}});

    int ret = evalCode(R"(
        import { getConfig } from 'stdiolink';
        const cfg = getConfig();
        try {
            cfg.port = 9999;
            throw new Error('should not reach');
        } catch (e) {
            if (e.message === 'should not reach') throw e;
        }
    )");
    EXPECT_EQ(ret, 0);
}

// 测试 defineConfig 不再可用（ESM 导入不存在的命名导出会在链接阶段报错）
TEST_F(ServiceConfigJsTest, DefineConfigNotExported) {
    JsConfigBinding::attachRuntime(engine->runtime());
    JsConfigBinding::setMergedConfig(engine->context(), QJsonObject{});

    // 导入不存在的命名导出，模块链接阶段即报错，evalCode 应返回非 0
    int ret = evalCode(R"(
        import { defineConfig } from 'stdiolink';
    )");
    EXPECT_NE(ret, 0);
}
```

### 7.3 test_js_integration.cpp（更新）

```cpp
TEST_F(JsIntegrationTest, DumpSchemaFromFile) {
    // 创建服务目录，含 manifest.json + config.schema.json + index.js
    // 启动 service 时传 --dump-config-schema
    // 断言 exitCode==0 且 stdout 为合法 JSON
    // 断言 index.js 未被执行（通过 stderr 无脚本输出验证）
}

TEST_F(JsIntegrationTest, InvalidConfigSchemaFileFails) {
    // 创建服务目录，config.schema.json 内容为非法 JSON
    // 断言 exitCode==2
}

TEST_F(JsIntegrationTest, ConfigValidationFailsBeforeScript) {
    // 创建服务目录，schema 要求 port(int,required)
    // 不传 --config.port
    // 断言 exitCode==1，stderr 包含 "port" 相关错误
}
```

## 8. 依赖关系

- **前置依赖**：
  - 里程碑 28（Service 配置 Schema 与注入）：提供 ServiceConfigSchema、ServiceConfigValidator 基础设施
  - 里程碑 29（Manifest 精简与固定文件名约定）：提供 ServiceDirectory
  - 里程碑 30（固定入口 index.js 加载机制）：提供目录加载机制
- **后续依赖**：
  - 里程碑 32（示例与测试全量迁移）：依赖本里程碑完成 defineConfig 移除
