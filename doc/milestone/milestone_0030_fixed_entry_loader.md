# 里程碑 30：固定入口 index.js 加载机制

## 1. 目标

改造 `stdiolink_service` 的入口加载逻辑，从"命令行直接传脚本路径"切换为"传入服务目录路径，固定执行 `<dir>/index.js`"。去除所有可配置入口分支，入口路径由 `ServiceDirectory` 固定拼接。

## 2. 设计概述

### 2.1 调用方式变更

| 对比项 | 变更前（M28） | 变更后（M30） |
|--------|-------------|---------------|
| 命令行 | `stdiolink_service script.js [opts]` | `stdiolink_service <service_dir> [opts]` |
| 入口解析 | `parsed.scriptPath` 直接使用 | `ServiceDirectory(dir).entryPath()` |
| 校验 | 仅检查文件是否存在 | `ServiceDirectory::validate()` 检查三文件完整性 |

### 2.2 新命令行语义

```bash
# 执行服务
stdiolink_service ./my_service --config.port=8080

# 查看帮助（manifest 信息 + 通用帮助）
stdiolink_service ./my_service --help

# 导出配置 schema
stdiolink_service ./my_service --dump-config-schema

# 全局帮助（无服务目录）
stdiolink_service --help
```

## 3. 技术要点

### 3.1 ServiceArgs 改造

将 `scriptPath` 语义改为 `serviceDir`，解析逻辑调整：

```cpp
struct ParseResult {
    QString serviceDir;               // 服务目录路径（替代 scriptPath）
    QJsonObject rawConfigValues;
    QString configFilePath;
    bool dumpSchema = false;
    bool help = false;
    bool version = false;
    QString error;
};
```

### 3.2 main.cpp 核心流程变更

```cpp
int main(int argc, char* argv[]) {
    // ... 初始化省略
    auto parsed = ServiceArgs::parse(app.arguments());

    // 全局帮助（无服务目录）
    if (parsed.help && parsed.serviceDir.isEmpty()) {
        printHelp();
        return 0;
    }

    // 校验服务目录完整性
    ServiceDirectory svcDir(parsed.serviceDir);
    QString dirErr;
    if (!svcDir.validate(dirErr)) {
        QTextStream err(stderr);
        err << "Error: " << dirErr << "\n";
        return 2;
    }

    // 加载 manifest（仅用于显示信息）
    QString mErr;
    auto manifest = ServiceManifest::loadFromFile(svcDir.manifestPath(), mErr);
    if (!mErr.isEmpty()) { /* stderr 报错，退出码 2 */ }

    // 固定入口路径
    QString scriptPath = svcDir.entryPath();  // <dir>/index.js

    // ... 后续 JS 引擎初始化、配置注入、执行脚本 ...
}
```

### 3.3 --help 模式变更

当传入服务目录时，`--help` 不再执行 `index.js` 来收集 `defineConfig()` schema（该逻辑将在 M31 中由 `config.schema.json` 文件替代）。本里程碑中 `--help` 仅显示 manifest 信息和通用帮助。

### 3.4 --dump-config-schema 模式变更

既然 M30 已强制要求服务目录包含 `config.schema.json`，`--dump-config-schema` 应直接读取该文件输出，不执行脚本。这仅统一 dump 通道到文件，运行时 schema 从 `defineConfig` 切换到文件的工作在 M31 中完成。

具体行为：读取 `<service_dir>/config.schema.json` → 校验为合法 JSON → 输出到 stdout → 退出码 0。文件不存在或格式错误时退出码 2。

## 4. 实现步骤

### 4.1 修改 ServiceArgs

- 将 `ParseResult::scriptPath` 重命名为 `serviceDir`
- 解析逻辑中，第一个非 `--` 开头的参数识别为服务目录路径
- 保留 `--config.*`、`--config-file`、`--dump-config-schema`、`--help`、`--version` 的解析

### 4.2 修改 main.cpp

- 引入 `ServiceDirectory` 和 `ServiceManifest`
- 在 JS 引擎初始化前，先调用 `svcDir.validate()` 校验目录完整性
- 加载 `manifest.json` 并在 `--help` 模式下输出服务名称和版本
- 将 `engine.evalFile(parsed.scriptPath)` 改为 `engine.evalFile(svcDir.entryPath())`

### 4.3 修改 printHelp()

- 无服务目录时输出通用帮助
- 有服务目录时额外输出 manifest 中的 name / version / description

### 4.4 更新所有引用 scriptPath 的代码

- 搜索所有使用 `parsed.scriptPath` 的位置，替换为 `svcDir.entryPath()`
- 更新相关测试中的参数构造

## 5. 文件清单

| 操作 | 文件路径 | 说明 |
|------|---------|------|
| 修改 | `src/stdiolink_service/config/service_args.h` | `scriptPath` → `serviceDir` |
| 修改 | `src/stdiolink_service/config/service_args.cpp` | 解析逻辑适配目录路径 |
| 修改 | `src/stdiolink_service/main.cpp` | 集成 ServiceDirectory / ServiceManifest，固定入口 |
| 修改 | `src/tests/test_service_args.cpp` | 更新参数解析测试 |
| 修改 | `src/tests/test_js_integration.cpp` | 最小迁移：writeScript+runServiceScript 改为 createServiceDir+runServiceDir，确保 M30 合入后测试可通过 |
| 新增 | `src/tests/test_service_loader.cpp` | 入口加载集成测试 |
| 修改 | `src/tests/CMakeLists.txt` | 添加新增测试文件 |

## 6. 验收标准

1. `stdiolink_service ./my_service` 正确执行 `my_service/index.js`
2. 服务目录缺少 `index.js` 时退出码 2，stderr 包含明确错误
3. 服务目录缺少 `manifest.json` 时退出码 2，stderr 包含明确错误
4. 服务目录缺少 `config.schema.json` 时退出码 2，stderr 包含明确错误
5. `manifest.json` 格式错误时退出码 2
6. `stdiolink_service --help`（无目录）输出通用帮助
7. `stdiolink_service ./my_service --help` 输出 manifest 中的服务名称和版本
8. `ServiceArgs::parse()` 正确将第一个非选项参数识别为 `serviceDir`
9. 所有现有 `--config.*` 参数解析功能不受影响
10. 不存在的目录路径报错退出码 2

## 7. 单元测试用例

### 7.1 test_service_args.cpp（更新）

```cpp
// 测试服务目录路径解析
TEST_F(ServiceArgsTest, ParseServiceDir) {
    QStringList args = {"stdiolink_service", "./my_service",
                        "--config.port=8080"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.error.isEmpty());
    EXPECT_EQ(result.serviceDir, "./my_service");
    EXPECT_EQ(result.rawConfigValues["port"].toString(), "8080");
}

// 测试无服务目录时 help 标志
TEST_F(ServiceArgsTest, HelpWithoutServiceDir) {
    QStringList args = {"stdiolink_service", "--help"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.help);
    EXPECT_TRUE(result.serviceDir.isEmpty());
}

// 测试有服务目录时 help 标志
TEST_F(ServiceArgsTest, HelpWithServiceDir) {
    QStringList args = {"stdiolink_service", "./svc", "--help"};
    auto result = ServiceArgs::parse(args);
    EXPECT_TRUE(result.help);
    EXPECT_EQ(result.serviceDir, "./svc");
}

// 测试缺少服务目录（非 help/version 模式）
TEST_F(ServiceArgsTest, MissingServiceDir) {
    QStringList args = {"stdiolink_service", "--config.port=8080"};
    auto result = ServiceArgs::parse(args);
    EXPECT_FALSE(result.error.isEmpty());
}
```

### 7.2 test_service_loader.cpp

```cpp
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QFile>
#include <QProcess>
#include <QCoreApplication>

class ServiceLoaderTest : public ::testing::Test {
protected:
    QString servicePath() {
        QString path = QCoreApplication::applicationDirPath()
                       + "/stdiolink_service";
#ifdef Q_OS_WIN
        path += ".exe";
#endif
        return path;
    }

    void createFile(const QString& path, const QByteArray& content) {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(content);
        f.close();
    }

    QByteArray minimalManifest() {
        return R"({"manifestVersion":"1","id":"test","name":"Test","version":"1.0"})";
    }

    QByteArray emptySchema() {
        return R"({})";
    }
};

TEST_F(ServiceLoaderTest, ValidServiceDirExecutesIndexJs) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/manifest.json", minimalManifest());
    createFile(tmp.path() + "/config.schema.json", emptySchema());
    createFile(tmp.path() + "/index.js",
               "console.log('hello from index.js');\n");

    QProcess proc;
    proc.start(servicePath(), {tmp.path()});
    proc.waitForFinished(10000);
    EXPECT_EQ(proc.exitCode(), 0);
    EXPECT_TRUE(proc.readAllStandardError().contains("hello from index.js"));
}

TEST_F(ServiceLoaderTest, MissingIndexJsFails) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/manifest.json", minimalManifest());
    createFile(tmp.path() + "/config.schema.json", emptySchema());
    // 不创建 index.js

    QProcess proc;
    proc.start(servicePath(), {tmp.path()});
    proc.waitForFinished(10000);
    EXPECT_EQ(proc.exitCode(), 2);
}

TEST_F(ServiceLoaderTest, MissingConfigSchemaFails) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/manifest.json", minimalManifest());
    createFile(tmp.path() + "/index.js", "// ok\n");
    // 不创建 config.schema.json

    QProcess proc;
    proc.start(servicePath(), {tmp.path()});
    proc.waitForFinished(10000);
    EXPECT_EQ(proc.exitCode(), 2);
}

TEST_F(ServiceLoaderTest, NonexistentDirFails) {
    QProcess proc;
    proc.start(servicePath(), {"/nonexistent/path"});
    proc.waitForFinished(10000);
    EXPECT_EQ(proc.exitCode(), 2);
}

TEST_F(ServiceLoaderTest, HelpWithServiceDir) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/manifest.json", minimalManifest());
    createFile(tmp.path() + "/config.schema.json", emptySchema());
    createFile(tmp.path() + "/index.js", "// ok\n");

    QProcess proc;
    proc.start(servicePath(), {tmp.path(), "--help"});
    proc.waitForFinished(10000);
    EXPECT_EQ(proc.exitCode(), 0);
    QByteArray err = proc.readAllStandardError();
    EXPECT_TRUE(err.contains("Test"));  // manifest name
}

TEST_F(ServiceLoaderTest, DumpSchemaWithServiceDir) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/manifest.json", minimalManifest());
    createFile(tmp.path() + "/config.schema.json", emptySchema());
    createFile(tmp.path() + "/index.js", "// ok\n");

    QProcess proc;
    proc.start(servicePath(), {tmp.path(), "--dump-config-schema"});
    proc.waitForFinished(10000);
    EXPECT_EQ(proc.exitCode(), 0);
    QByteArray out = proc.readAllStandardOutput();
    EXPECT_FALSE(out.isEmpty());
    // 验证输出为合法 JSON
    QJsonParseError parseErr;
    QJsonDocument::fromJson(out, &parseErr);
    EXPECT_EQ(parseErr.error, QJsonParseError::NoError);
}
```

## 8. 依赖关系

- **前置依赖**：
  - 里程碑 28（Service 配置 Schema 与注入）：提供 ServiceArgs、JsConfigBinding 等基础设施
  - 里程碑 29（Manifest 精简与固定文件名约定）：提供 ServiceManifest、ServiceDirectory
- **后续依赖**：
  - 里程碑 31（config.schema.json 外部 Schema 加载）：依赖本里程碑的目录加载机制
  - 里程碑 32（示例与测试全量迁移）：依赖本里程碑的新命令行语义
