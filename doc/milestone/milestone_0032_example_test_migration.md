# 里程碑 32：示例与测试全量迁移

## 1. 目标

将受影响的示例脚本、demo 项目和相关测试从"单脚本直跑"模式全量迁移到"服务目录"模式。每个示例和测试场景均使用标准的 `manifest.json` + `index.js` + `config.schema.json` 三文件结构。同时删除/改写所有与 `defineConfig` 相关的测试用例。

## 2. 设计概述

### 2.1 迁移范围

| 类别 | 当前状态 | 迁移目标 |
|------|---------|---------|
| js_runtime_demo 示例脚本 | 单 .js 文件直跑 | 服务目录结构 |
| config_demo 示例脚本 | 单 .js 文件 + defineConfig() | 服务目录结构 + config.schema.json |
| 集成测试脚本 | QTemporaryDir + writeScript() 创建临时 .js | QTemporaryDir + createServiceDir() 创建临时服务目录 |
| test_service_config_js | 依赖 defineConfig() | 改用 setMergedConfig() |
| test_js_integration | runServiceScript(scriptPath) | runServiceDir(dirPath) |
| M28 defineConfig 相关测试 | 存在 | 删除或改写 |

### 2.2 js_runtime_demo 迁移方案

现有 `src/demo/js_runtime_demo/scripts/` 下的示例脚本将重组为服务目录结构：

```
src/demo/js_runtime_demo/services/
├── basic_demo/
│   ├── manifest.json
│   ├── index.js              ← 原 00_all_in_one.js
│   └── config.schema.json
├── engine_modules/
│   ├── manifest.json
│   ├── index.js              ← 原 01_engine_modules.js
│   └── config.schema.json
├── driver_task/
│   ├── manifest.json
│   ├── index.js              ← 原 02_driver_task.js
│   └── config.schema.json
├── proxy_scheduler/
│   ├── manifest.json
│   ├── index.js              ← 原 03_proxy_scheduler.js
│   └── config.schema.json
└── process_types/
    ├── manifest.json
    ├── index.js              ← 原 04_process_and_types.js
    └── config.schema.json
```

### 2.3 config_demo 迁移方案

现有 `src/demo/config_demo/scripts/` 下的 7 个示例脚本全部使用 `defineConfig()`，需同步迁移：

```
src/demo/config_demo/services/
├── basic_types/
│   ├── manifest.json
│   ├── index.js              ← 原 01_basic_types.js（移除 defineConfig）
│   └── config.schema.json    ← 从 defineConfig() 参数提取
├── constraints/
│   ├── manifest.json
│   ├── index.js              ← 原 02_constraints.js
│   └── config.schema.json
├── nested_object/
│   ├── manifest.json
│   ├── index.js              ← 原 03_nested_object.js
│   └── config.schema.json
├── array_and_enum/
│   ├── manifest.json
│   ├── index.js              ← 原 04_array_and_enum.js
│   └── config.schema.json
├── config_file_merge/
│   ├── manifest.json
│   ├── index.js              ← 原 05_config_file_merge.js
│   └── config.schema.json
├── readonly_and_errors/
│   ├── manifest.json
│   ├── index.js              ← 原 06_readonly_and_errors.js
│   └── config.schema.json
└── all_in_one/
    ├── manifest.json
    ├── index.js              ← 原 00_all_in_one.js
    └── config.schema.json
```

### 2.4 集成测试辅助函数迁移

现有测试中通过 `writeScript()` 在 QTemporaryDir 中创建临时 `.js` 文件的模式，需改为 `createServiceDir()` 创建临时服务目录：

```cpp
// 迁移前
QString writeScript(const QTemporaryDir& dir,
                    const QString& name, const QString& code);

// 迁移后
QString createServiceDir(const QString& code,
                         const QByteArray& schema = "{}");
```

## 3. 技术要点

### 3.1 示例脚本迁移规则

- 每个原始 `.js` 脚本重命名为 `index.js` 放入对应服务目录
- 脚本中所有 `import { defineConfig, getConfig } from 'stdiolink'` 改为 `import { getConfig } from 'stdiolink'`
- 移除脚本中的 `defineConfig({...})` 调用
- 将原 `defineConfig()` 中的 schema 声明提取到 `config.schema.json`
- 共享模块（`lib/`、`modules/`）迁移到 `services/` 同级的 `shared/` 目录，各服务通过 `../shared/` 相对路径引用

### 3.2 集成测试辅助函数

```cpp
class JsIntegrationTest : public ::testing::Test {
protected:
    // ... 现有 servicePath() / driverPath() ...

    /// 创建临时服务目录，返回目录路径
    QString createServiceDir(const QString& jsCode,
                             const QByteArray& schema = "{}") {
        auto* dir = new QTemporaryDir();
        dir->setAutoRemove(true);
        tempDirs.append(dir);

        // manifest.json
        QFile mf(dir->path() + "/manifest.json");
        mf.open(QIODevice::WriteOnly);
        mf.write(R"({"manifestVersion":"1","id":"test","name":"Test","version":"1.0"})");
        mf.close();

        // config.schema.json
        QFile sf(dir->path() + "/config.schema.json");
        sf.open(QIODevice::WriteOnly);
        sf.write(schema);
        sf.close();

        // index.js
        QFile jf(dir->path() + "/index.js");
        jf.open(QIODevice::WriteOnly);
        jf.write(jsCode.toUtf8());
        jf.close();

        return dir->path();
    }

    struct RunResult {
        int exitCode;
        QString stdoutStr;
        QString stderrStr;
    };

    RunResult runServiceDir(const QString& dirPath,
                            const QStringList& extraArgs = {}) {
        QProcess proc;
        QStringList args = {dirPath};
        args.append(extraArgs);
        proc.start(servicePath(), args);
        proc.waitForFinished(30000);
        return {
            proc.exitCode(),
            QString::fromUtf8(proc.readAllStandardOutput()),
            QString::fromUtf8(proc.readAllStandardError())
        };
    }

    QList<QTemporaryDir*> tempDirs;
};
```

### 3.3 需删除的测试用例

以下 M28 测试用例因 `defineConfig` 移除而需删除：

- `DuplicateDefineConfigThrows` — 不再适用
- `RequiredFieldMissingThrows`（JS 端）— 校验已移至 C++ 侧
- `TypeMismatchThrows`（JS 端）— 校验已移至 C++ 侧
- `ConstraintViolationThrows`（JS 端）— 校验已移至 C++ 侧
- `GetConfigBeforeDefineReturnsEmpty` — 不再适用
- `SchemaAccessibleFromCpp` — schema 不再由 JS 产生
- `DumpSchemaWithoutDefineConfigFails` — dump 模式不再执行脚本
- `DumpSchemaModeBlocksOpenDriverAndExec` — 副作用拦截已移除

### 3.4 需改写的测试用例

以下测试用例需从"单脚本"模式改写为"服务目录"模式：

| 原测试 | 改写方式 |
|--------|---------|
| `BasicDriverUsage` | `createServiceDir()` 替代 `writeScript()` |
| `ProxyDriverUsage` | 同上 |
| `ProcessExecUsage` | 同上 |
| `DriverStartFailureIsCatchable` | 同上 |
| `ModuleNotFoundFailsProcess` | 同上 |
| `UncaughtExceptionExitsWithError` | 同上 |
| `ConsoleOutputDoesNotPolluteStdout` | 同上 |
| `CrossFileImport` | 服务目录内放置 lib.js + index.js |
| `DefineAndGetConfig` | 改为 `setMergedConfig` + `getConfig` |
| `ConfigIsReadOnly` | 改为 `setMergedConfig` + 冻结验证 |
| `CliOverridesFileConfig` | 改为 `runServiceDir` + `--config.*` 参数 |
| `AllFieldTypesSupported` | 改为 `setMergedConfig` 注入全类型 |

## 4. 实现步骤

### 4.1 迁移 js_runtime_demo 示例

1. 在 `src/demo/js_runtime_demo/` 下创建 `services/` 目录
2. 为每个原始脚本创建对应的服务子目录
3. 将原始 `.js` 文件重命名为 `index.js` 并移入服务目录
4. 为每个服务目录创建 `manifest.json`（填写 id/name/version）
5. 为每个服务目录创建 `config.schema.json`（无配置项的用 `{}`）
6. 从脚本中移除 `defineConfig()` 调用，提取 schema 到文件
7. 更新 `README.md` 中的运行命令示例
8. 删除原 `scripts/` 目录下的旧文件

### 4.2 迁移 config_demo 示例

1. 在 `src/demo/config_demo/` 下创建 `services/` 目录
2. 为每个原始脚本创建对应的服务子目录（basic_types / constraints / nested_object / array_and_enum / config_file_merge / readonly_and_errors / all_in_one）
3. 将原始 `.js` 文件重命名为 `index.js` 并移入服务目录
4. 从脚本中移除 `defineConfig({...})` 调用，将 schema 声明提取到 `config.schema.json`
5. 为每个服务目录创建 `manifest.json`
6. 更新 `README.md` 中的运行命令示例
7. 更新 `CMakeLists.txt` 资产复制路径
8. 删除原 `scripts/` 目录

### 4.3 改写集成测试基础设施

1. `test_js_integration.cpp` 中将 `writeScript()` 替换为 `createServiceDir()`
2. `runServiceScript(scriptPath)` 替换为 `runServiceDir(dirPath, extraArgs)`
3. 所有测试用例中的脚本路径参数改为服务目录路径

### 4.4 删除 defineConfig 相关测试

1. 删除 3.3 节列出的所有废弃测试用例
2. 确认无编译错误和链接错误

### 4.5 新增固定命名约束测试

1. 缺 `index.js` 时启动失败（exit code 2）
2. 缺 `config.schema.json` 时启动失败（exit code 2）
3. `config.schema.json` 非法 JSON 时启动失败（exit code 2）

### 4.6 更新文档

1. 更新 `doc/manual/` 中涉及 `defineConfig` 的章节
2. 更新 js_runtime_demo 的 `README.md`
3. 确保所有文档中的命令行示例使用服务目录模式

## 5. 文件清单

| 操作 | 文件路径 | 说明 |
|------|---------|------|
| 新增 | `src/demo/js_runtime_demo/services/basic_demo/` | 基础示例服务目录 |
| 新增 | `src/demo/js_runtime_demo/services/engine_modules/` | 引擎模块示例服务目录 |
| 新增 | `src/demo/js_runtime_demo/services/driver_task/` | Driver/Task 示例服务目录 |
| 新增 | `src/demo/js_runtime_demo/services/proxy_scheduler/` | Proxy 调度示例服务目录 |
| 新增 | `src/demo/js_runtime_demo/services/process_types/` | 进程调用示例服务目录 |
| 新增 | `src/demo/js_runtime_demo/shared/` | 共享模块（原 scripts/lib/ 和 scripts/modules/ 迁入） |
| 删除 | `src/demo/js_runtime_demo/scripts/` | 整个旧脚本目录 |
| 新增 | `src/demo/config_demo/services/basic_types/` | 基本类型配置示例服务目录 |
| 新增 | `src/demo/config_demo/services/constraints/` | 约束配置示例服务目录 |
| 新增 | `src/demo/config_demo/services/nested_object/` | 嵌套对象配置示例服务目录 |
| 新增 | `src/demo/config_demo/services/array_and_enum/` | 数组与枚举配置示例服务目录 |
| 新增 | `src/demo/config_demo/services/config_file_merge/` | 配置文件合并示例服务目录 |
| 新增 | `src/demo/config_demo/services/readonly_and_errors/` | 只读与错误处理示例服务目录 |
| 新增 | `src/demo/config_demo/services/all_in_one/` | 综合示例服务目录 |
| 删除 | `src/demo/config_demo/scripts/` | 整个旧脚本目录 |
| 修改 | `src/demo/config_demo/README.md` | 更新运行命令，移除 defineConfig 引用 |
| 修改 | `src/demo/js_runtime_demo/README.md` | 更新运行命令 |
| 修改 | `src/demo/js_runtime_demo/CMakeLists.txt` | 资产复制路径从 scripts/ 改为 services/ + shared/ |
| 修改 | `src/demo/config_demo/CMakeLists.txt` | 资产复制路径从 scripts/ 改为 services/ |
| 修改 | `src/tests/test_js_integration.cpp` | 全量改写为服务目录模式 |
| 修改 | `src/tests/test_service_config_js.cpp` | 删除 defineConfig 测试，改写为 setMergedConfig |
| 修改 | `doc/manual/` 相关章节 | 移除 defineConfig 引用 |

## 6. 验收标准

1. 所有 js_runtime_demo 示例均以服务目录模式运行，`stdiolink_service ./services/xxx` 退出码 0
2. 所有 config_demo 示例均以服务目录模式运行，README 中的命令可复现
3. 每个示例服务目录包含完整的 `manifest.json` + `index.js` + `config.schema.json`
4. 示例脚本中不再包含 `defineConfig` 调用
5. 所有集成测试通过，使用 `createServiceDir()` 替代 `writeScript()`
6. 所有 defineConfig 相关测试已删除，无编译错误
7. 新增固定命名约束测试全部通过
8. `config.schema.json` 缺失时退出码 2
9. `index.js` 缺失时退出码 2
10. `config.schema.json` 非法 JSON 时退出码 2
11. 文档中不再引用 `defineConfig` API
12. 全量测试套件通过（`stdiolink_tests.exe`）

## 7. 单元测试用例

### 7.1 固定命名约束测试

```cpp
TEST_F(JsIntegrationTest, MissingIndexJsFails) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/manifest.json", minimalManifest());
    createFile(tmp.path() + "/config.schema.json", "{}");
    // 不创建 index.js

    auto r = runServiceDir(tmp.path());
    EXPECT_EQ(r.exitCode, 2);
    EXPECT_TRUE(r.stderrStr.contains("index.js"));
}

TEST_F(JsIntegrationTest, MissingConfigSchemaFails) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/manifest.json", minimalManifest());
    createFile(tmp.path() + "/index.js", "// ok\n");
    // 不创建 config.schema.json

    auto r = runServiceDir(tmp.path());
    EXPECT_EQ(r.exitCode, 2);
    EXPECT_TRUE(r.stderrStr.contains("config.schema.json"));
}

TEST_F(JsIntegrationTest, InvalidConfigSchemaJsonFails) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/manifest.json", minimalManifest());
    createFile(tmp.path() + "/index.js", "// ok\n");
    createFile(tmp.path() + "/config.schema.json", "not json");

    auto r = runServiceDir(tmp.path());
    EXPECT_EQ(r.exitCode, 2);
}
```

### 7.2 迁移后的集成测试示例

```cpp
TEST_F(JsIntegrationTest, BasicDriverUsage) {
    QString dir = createServiceDir(QString(
        "import { Driver } from 'stdiolink';\n"
        "const d = new Driver();\n"
        "d.start('%1');\n"
        "const task = d.request('add', { a: 10, b: 20 });\n"
        "const msg = task.waitNext(5000);\n"
        "if (!msg || msg.status !== 'done')\n"
        "    throw new Error('unexpected status');\n"
        "console.log('result:', JSON.stringify(msg.data));\n"
        "d.terminate();\n"
    ).arg(driverPath()));

    auto r = runServiceDir(dir);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrStr.contains("result:"));
}
```

### 7.3 配置注入集成测试

```cpp
TEST_F(JsIntegrationTest, ConfigInjectionViaServiceDir) {
    QByteArray schema = R"({
        "port": { "type": "int", "required": true },
        "name": { "type": "string", "default": "default" }
    })";

    QString dir = createServiceDir(
        "import { getConfig } from 'stdiolink';\n"
        "const cfg = getConfig();\n"
        "console.log('port:', cfg.port);\n"
        "console.log('name:', cfg.name);\n",
        schema);

    auto r = runServiceDir(dir, {"--config.port=8080"});
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrStr.contains("port: 8080"));
    EXPECT_TRUE(r.stderrStr.contains("name: default"));
}
```

## 8. 依赖关系

- **前置依赖**：
  - 里程碑 29（Manifest 精简与固定文件名约定）
  - 里程碑 30（固定入口 index.js 加载机制）
  - 里程碑 31（config.schema.json 外部 Schema 加载）
- **后续依赖**：
  - 无（本里程碑为服务目录方案 v4 系列的终点）
