# 里程碑 34：Server 脚手架与 ServiceScanner

> **前置条件**: 里程碑 28（Service 配置 Schema）已完成
> **目标**: 搭建 `stdiolink_server` 可执行文件的基础框架，实现命令行参数解析、`config.json` 加载、Service 目录扫描，为后续里程碑提供项目骨架

---

## 1. 目标

- 创建 `src/stdiolink_server/` 目录和 CMake 构建配置
- 实现 `ServerArgs` 命令行参数解析（`--data-root`、`--port`、`--host`、`--log-level`）
- 实现 `ServerConfig` 加载 `<data_root>/config.json` 可选配置文件
- 实现 `ServiceScanner`，扫描 `services/` 目录，加载所有 Service 的 `manifest.json` 和 `config.schema.json`
- 实现 `main.cpp` 启动入口，完成参数解析 → 配置加载 → Service 扫描的基本流程
- 确保 `data_root` 下的标准目录结构（`services/`、`projects/`、`workspaces/`、`logs/`）在启动时自动创建

---

## 2. 技术要点

### 2.1 目录结构约定

`stdiolink_server` 管理的数据根目录结构：

```
<data_root>/
├── config.json                # 管理器配置（可选）
├── drivers/                   # Driver 目录
├── services/                  # Service 模板目录
├── projects/                  # Project 配置文件
├── workspaces/                # 各 Project 的工作目录
├── logs/                      # 日志目录
└── shared/                    # 全局共享目录
```

启动时对 `services/`、`projects/`、`workspaces/`、`logs/` 四个目录做 `mkpath`，不存在则创建，已存在则跳过。`drivers/` 和 `shared/` 不强制创建（由用户按需建立）。

### 2.2 命令行参数与配置优先级

| 参数 | CLI 形式 | config.json 字段 | 默认值 |
|------|---------|-----------------|--------|
| 数据根目录 | `--data-root=<path>` | — | `.`（当前目录） |
| HTTP 端口 | `--port=<port>` | `port` | `8080` |
| 监听地址 | `--host=<addr>` | `host` | `127.0.0.1` |
| 日志级别 | `--log-level=<level>` | `logLevel` | `info` |
| Service 程序路径 | — | `serviceProgram` | 优先使用显式配置，否则自动查找 |

优先级：CLI 参数 > `config.json` > 内置默认值。

`serviceProgram` 解析规则：
1. 若 `config.json.serviceProgram` 非空，直接使用（启动前校验可执行）
2. 否则，尝试与 `stdiolink_server` 同目录下的 `stdiolink_service`
3. 若仍未找到，再从 `PATH` 中查找 `stdiolink_service`

### 2.3 ServiceScanner 设计

`ServiceScanner` 是编排层，组合复用 `stdiolink_service` 中已有的底层类：

| 底层类 | 来源 | 复用内容 |
|--------|------|---------|
| `ServiceDirectory` | `stdiolink_service/config/` | 目录结构校验、路径获取 |
| `ServiceManifest` | `stdiolink_service/config/` | `manifest.json` 解析与校验 |
| `ServiceConfigSchema` | `stdiolink_service/config/` | `config.schema.json` 解析 |

`ServiceScanner` 不重复实现文件解析逻辑，仅负责：
1. 遍历 `services/` 下的子目录
2. 对每个子目录调用底层类加载 manifest 和 schema
3. 汇总为 `ServiceInfo` 集合，记录加载成功/失败状态

### 2.4 库依赖拆分

`ServiceScanner` 需要引用 `stdiolink_service` 中的配置类（`ServiceDirectory`、`ServiceManifest`、`ServiceConfigSchema`、`ServiceConfigValidator`）。这些类当前编译在 `stdiolink_service` 可执行文件中，不是独立库。

**方案**：将 `stdiolink_service/config/` 下的配置类抽取为静态库 `stdiolink_service_config`，供 `stdiolink_service` 和 `stdiolink_server` 共同链接。

```
stdiolink_service_config (STATIC)
  ├── service_config_schema.cpp
  ├── service_config_validator.cpp
  ├── service_config_help.cpp
  ├── service_manifest.cpp
  └── service_directory.cpp

stdiolink_service (EXECUTABLE)
  └── links: stdiolink_service_config + stdiolink + Qt6 + qjs

stdiolink_server (EXECUTABLE)
  └── links: stdiolink_service_config + stdiolink + Qt6
```

---

## 3. 实现步骤

### 3.1 ServerArgs（命令行参数解析）

```cpp
// src/stdiolink_server/config/server_args.h
#pragma once

#include <QString>
#include <QStringList>

namespace stdiolink_server {

struct ServerArgs {
    QString dataRoot;
    int port = 8080;
    QString host = "127.0.0.1";
    QString logLevel = "info";
    bool help = false;
    bool version = false;
    QString error;

    static ServerArgs parse(const QStringList& args);
};

} // namespace stdiolink_server
```

```cpp
// src/stdiolink_server/config/server_args.cpp
#include "server_args.h"

namespace stdiolink_server {

ServerArgs ServerArgs::parse(const QStringList& args) {
    ServerArgs result;
    // args[0] 是程序名，从 args[1] 开始解析
    for (int i = 1; i < args.size(); ++i) {
        const QString& arg = args[i];
        if (arg == "-h" || arg == "--help") {
            result.help = true;
        } else if (arg == "-v" || arg == "--version") {
            result.version = true;
        } else if (arg.startsWith("--data-root=")) {
            result.dataRoot = arg.mid(12);
        } else if (arg.startsWith("--port=")) {
            bool ok = false;
            result.port = arg.mid(7).toInt(&ok);
            if (!ok || result.port < 1 || result.port > 65535) {
                result.error = "invalid port: " + arg.mid(7);
                return result;
            }
        } else if (arg.startsWith("--host=")) {
            result.host = arg.mid(7);
        } else if (arg.startsWith("--log-level=")) {
            result.logLevel = arg.mid(12);
            if (result.logLevel != "debug" && result.logLevel != "info"
                && result.logLevel != "warn" && result.logLevel != "error") {
                result.error = "invalid log level: " + result.logLevel;
                return result;
            }
        } else {
            result.error = "unknown option: " + arg;
            return result;
        }
    }
    if (result.dataRoot.isEmpty()) {
        result.dataRoot = ".";
    }
    return result;
}

} // namespace stdiolink_server
```

### 3.2 ServerConfig（配置文件加载）

```cpp
// src/stdiolink_server/config/server_config.h
#pragma once

#include <QString>
#include <QJsonObject>
#include "server_args.h"

namespace stdiolink_server {

struct ServerConfig {
    int port = 8080;
    QString host = "127.0.0.1";
    QString logLevel = "info";
    QString serviceProgram;

    static ServerConfig loadFromFile(const QString& filePath, QString& error);
    void applyArgs(const ServerArgs& args);
};

} // namespace stdiolink_server
```

### 3.3 ServiceScanner

```cpp
// src/stdiolink_server/scanner/service_scanner.h
#pragma once

#include <QMap>
#include <QJsonObject>
#include <QString>
#include "config/service_config_schema.h"
#include "config/service_manifest.h"

namespace stdiolink_server {

struct ServiceInfo {
    QString id;
    QString name;
    QString version;
    QString serviceDir;       // 绝对路径
    stdiolink_service::ServiceManifest manifest;
    stdiolink_service::ServiceConfigSchema configSchema;
    QJsonObject rawConfigSchema;     // config.schema.json 源格式（供 HTTP API 返回）
    bool hasSchema = true;           // v1 中 schema 为必需文件
    bool valid = true;
    QString error;
};

class ServiceScanner {
public:
    struct ScanStats {
        int scannedDirs = 0;
        int loadedServices = 0;
        int failedServices = 0;
    };

    /// 扫描 services/ 目录，返回 id → ServiceInfo 映射
    QMap<QString, ServiceInfo> scan(const QString& servicesDir,
                                    ScanStats* stats = nullptr) const;

private:
    /// 加载单个 Service 目录
    ServiceInfo loadService(const QString& serviceDir) const;
};

} // namespace stdiolink_server
```

```cpp
// src/stdiolink_server/scanner/service_scanner.cpp
#include "service_scanner.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include "config/service_directory.h"

using namespace stdiolink_service;

namespace stdiolink_server {

QMap<QString, ServiceInfo> ServiceScanner::scan(
    const QString& servicesDir, ScanStats* stats) const
{
    QMap<QString, ServiceInfo> result;
    QDir dir(servicesDir);
    if (!dir.exists()) return result;

    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        if (stats) stats->scannedDirs++;
        const QString subDir = dir.absoluteFilePath(entry);
        ServiceInfo info = loadService(subDir);
        if (info.valid) {
            if (stats) stats->loadedServices++;
            result.insert(info.id, info);
        } else {
            if (stats) stats->failedServices++;
            qWarning("ServiceScanner: skip %s: %s",
                     qUtf8Printable(entry),
                     qUtf8Printable(info.error));
        }
    }
    return result;
}

ServiceInfo ServiceScanner::loadService(const QString& serviceDir) const {
    ServiceInfo info;
    info.serviceDir = serviceDir;

    // 1. 校验目录结构
    ServiceDirectory svcDir(serviceDir);
    QString dirErr;
    if (!svcDir.validate(dirErr)) {
        info.valid = false;
        info.error = dirErr;
        return info;
    }

    // 2. 加载 manifest
    QString mErr;
    info.manifest = ServiceManifest::loadFromFile(
        svcDir.manifestPath(), mErr);
    if (!mErr.isEmpty()) {
        info.valid = false;
        info.error = mErr;
        return info;
    }
    info.id = info.manifest.id;
    info.name = info.manifest.name;
    info.version = info.manifest.version;

    // 3. 加载 config schema（必需）
    QString sErr;
    info.configSchema = ServiceConfigSchema::fromJsonFile(
        svcDir.configSchemaPath(), sErr);
    if (!sErr.isEmpty()) {
        info.valid = false;
        info.error = sErr;
        return info;
    }

    // 保存源 schema JSON，供后续 API 按源格式返回
    QFile schemaFile(svcDir.configSchemaPath());
    if (schemaFile.open(QIODevice::ReadOnly)) {
        QJsonParseError parseErr;
        QJsonDocument rawDoc = QJsonDocument::fromJson(
            schemaFile.readAll(), &parseErr);
        if (parseErr.error == QJsonParseError::NoError
            && rawDoc.isObject()) {
            info.rawConfigSchema = rawDoc.object();
        } else {
            info.valid = false;
            info.error = "config.schema.json parse error: "
                + parseErr.errorString();
            return info;
        }
    } else {
        info.valid = false;
        info.error = "cannot open config schema file: "
            + svcDir.configSchemaPath();
        return info;
    }

    return info;
}

} // namespace stdiolink_server
```

### 3.4 main.cpp 启动入口

```cpp
// src/stdiolink_server/main.cpp
#include <QCoreApplication>
#include <QDir>
#include <QTextStream>
#include <cstdio>

#include "config/server_args.h"
#include "config/server_config.h"
#include "scanner/service_scanner.h"
#include "stdiolink/platform/platform_utils.h"

using namespace stdiolink_server;

namespace {

void printHelp() {
    QTextStream err(stderr);
    err << "Usage: stdiolink_server [options]\n"
        << "Options:\n"
        << "  --data-root=<path>       Data root directory (default: .)\n"
        << "  --port=<port>            HTTP port (default: 8080)\n"
        << "  --host=<addr>            Listen address (default: 127.0.0.1)\n"
        << "  --log-level=<level>      debug|info|warn|error (default: info)\n"
        << "  -h, --help               Show this help\n"
        << "  -v, --version            Show version\n";
    err.flush();
}

bool ensureDirectories(const QString& dataRoot) {
    for (const QString& sub : {"services", "projects", "workspaces", "logs"}) {
        if (!QDir(dataRoot + "/" + sub).mkpath(".")) {
            qCritical("Failed to create: %s/%s",
                       qUtf8Printable(dataRoot), qUtf8Printable(sub));
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    stdiolink::PlatformUtils::initConsoleEncoding();
    QCoreApplication app(argc, argv);

    auto args = ServerArgs::parse(app.arguments());
    if (args.help) { printHelp(); return 0; }
    if (args.version) {
        fprintf(stderr, "stdiolink_server 0.1.0\n");
        return 0;
    }
    if (!args.error.isEmpty()) {
        fprintf(stderr, "Error: %s\n", qUtf8Printable(args.error));
        return 2;
    }

    QString dataRoot = QDir(args.dataRoot).absolutePath();

    // 加载 config.json（可选）
    QString cfgErr;
    auto config = ServerConfig::loadFromFile(
        dataRoot + "/config.json", cfgErr);
    if (!cfgErr.isEmpty()) {
        fprintf(stderr, "Error: %s\n", qUtf8Printable(cfgErr));
        return 2;
    }
    config.applyArgs(args);

    if (!ensureDirectories(dataRoot)) return 1;

    // 扫描 Service
    ServiceScanner serviceScanner;
    ServiceScanner::ScanStats svcStats;
    auto services = serviceScanner.scan(
        dataRoot + "/services", &svcStats);
    qInfo("Services: %d loaded, %d failed",
          svcStats.loadedServices, svcStats.failedServices);

    // 后续里程碑在此处继续集成：
    // - M35: DriverManagerScanner
    // - M36: ProjectManager
    // - M37: InstanceManager + ScheduleEngine
    // - M38: HttpServer

    return app.exec();
}
```

### 3.5 CMake 构建配置

```cmake
# src/stdiolink_service/CMakeLists.txt — 修改部分
# 抽取 config/ 为静态库

add_library(stdiolink_service_config STATIC
    config/service_config_schema.cpp
    config/service_config_validator.cpp
    config/service_config_help.cpp
    config/service_manifest.cpp
    config/service_directory.cpp
)

target_include_directories(stdiolink_service_config PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(stdiolink_service_config PUBLIC
    stdiolink
    Qt6::Core
)
```

```cmake
# src/stdiolink_server/CMakeLists.txt — 新增

set(STDIOLINK_SERVER_SOURCES
    main.cpp
    config/server_args.cpp
    config/server_config.cpp
    scanner/service_scanner.cpp
)

set(STDIOLINK_SERVER_HEADERS
    config/server_args.h
    config/server_config.h
    scanner/service_scanner.h
)

add_executable(stdiolink_server
    ${STDIOLINK_SERVER_SOURCES}
    ${STDIOLINK_SERVER_HEADERS}
)

target_include_directories(stdiolink_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(stdiolink_server PRIVATE
    stdiolink_service_config
    stdiolink
    Qt6::Core
)

set_target_properties(stdiolink_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
```

---

## 4. 文件清单

| 操作 | 文件 | 说明 |
|------|------|------|
| 新增 | `src/stdiolink_server/main.cpp` | Server 启动入口 |
| 新增 | `src/stdiolink_server/config/server_args.h` | 命令行参数解析头文件 |
| 新增 | `src/stdiolink_server/config/server_args.cpp` | 命令行参数解析实现 |
| 新增 | `src/stdiolink_server/config/server_config.h` | 配置文件加载头文件 |
| 新增 | `src/stdiolink_server/config/server_config.cpp` | 配置文件加载实现 |
| 新增 | `src/stdiolink_server/scanner/service_scanner.h` | ServiceScanner 头文件 |
| 新增 | `src/stdiolink_server/scanner/service_scanner.cpp` | ServiceScanner 实现 |
| 新增 | `src/stdiolink_server/CMakeLists.txt` | Server 构建配置 |
| 修改 | `src/stdiolink_service/CMakeLists.txt` | 抽取 config/ 为静态库 |
| 修改 | `src/CMakeLists.txt` | 新增 `add_subdirectory(stdiolink_server)` |

---

## 5. 验收标准

1. `stdiolink_server` 可执行文件能正常编译和链接
2. `--help` 输出帮助信息并退出
3. `--version` 输出版本号并退出
4. `--data-root=<path>` 正确设置数据根目录
5. `--port=<port>` 正确设置端口，非法值报错退出
6. `--log-level=<level>` 仅接受 `debug|info|warn|error`，非法值报错退出
7. 启动时自动创建 `services/`、`projects/`、`workspaces/`、`logs/` 目录
8. `config.json` 不存在时使用默认值，不报错
9. `config.json` 存在但格式非法时报错退出
10. CLI 参数覆盖 `config.json` 中的同名配置
11. `ServiceScanner` 正确扫描 `services/` 下的子目录
12. 有效 Service（含 `manifest.json` + `index.js` + `config.schema.json`）被正确加载
13. 无效 Service（缺少 `manifest.json` 或格式错误）被标记为失败，不影响其他 Service
14. `config.schema.json` 为必需文件，缺失时该 Service 记为失败
15. `stdiolink_service_config` 静态库被 `stdiolink_service` 和 `stdiolink_server` 共同链接，现有 `stdiolink_service` 功能不受影响

---

## 6. 单元测试用例

### 6.1 ServerArgs 测试

```cpp
#include <gtest/gtest.h>
#include "config/server_args.h"

using namespace stdiolink_server;

TEST(ServerArgsTest, DefaultValues) {
    auto args = ServerArgs::parse({"stdiolink_server"});
    EXPECT_EQ(args.dataRoot, ".");
    EXPECT_EQ(args.port, 8080);
    EXPECT_EQ(args.host, "127.0.0.1");
    EXPECT_EQ(args.logLevel, "info");
    EXPECT_TRUE(args.error.isEmpty());
}

TEST(ServerArgsTest, AllOptions) {
    auto args = ServerArgs::parse({
        "stdiolink_server",
        "--data-root=/tmp/data",
        "--port=9090",
        "--host=0.0.0.0",
        "--log-level=debug"
    });
    EXPECT_EQ(args.dataRoot, "/tmp/data");
    EXPECT_EQ(args.port, 9090);
    EXPECT_EQ(args.host, "0.0.0.0");
    EXPECT_EQ(args.logLevel, "debug");
    EXPECT_TRUE(args.error.isEmpty());
}

TEST(ServerArgsTest, InvalidPort) {
    auto args = ServerArgs::parse({
        "stdiolink_server", "--port=99999"
    });
    EXPECT_FALSE(args.error.isEmpty());
}

TEST(ServerArgsTest, InvalidLogLevel) {
    auto args = ServerArgs::parse({
        "stdiolink_server", "--log-level=verbose"
    });
    EXPECT_FALSE(args.error.isEmpty());
}

TEST(ServerArgsTest, HelpFlag) {
    auto args = ServerArgs::parse({"stdiolink_server", "--help"});
    EXPECT_TRUE(args.help);
}
```

### 6.2 ServiceScanner 测试

```cpp
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include "scanner/service_scanner.h"

using namespace stdiolink_server;

class ServiceScannerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmpDir.isValid());
        servicesDir = tmpDir.path() + "/services";
        QDir().mkpath(servicesDir);
    }

    void createService(const QString& id,
                       const QString& manifest,
                       bool withEntry = true,
                       bool withSchema = true) {
        QString dir = servicesDir + "/" + id;
        QDir().mkpath(dir);
        writeFile(dir + "/manifest.json", manifest);
        if (withEntry) {
            writeFile(dir + "/index.js",
                      "console.log('hello');");
        }
        if (withSchema) {
            writeFile(dir + "/config.schema.json", "{}");
        }
    }

    void writeFile(const QString& path,
                   const QString& content) {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        QTextStream(&f) << content;
    }

    QTemporaryDir tmpDir;
    QString servicesDir;
};

TEST_F(ServiceScannerTest, EmptyDirectory) {
    ServiceScanner scanner;
    ServiceScanner::ScanStats stats;
    auto result = scanner.scan(servicesDir, &stats);
    EXPECT_TRUE(result.isEmpty());
    EXPECT_EQ(stats.scannedDirs, 0);
}

TEST_F(ServiceScannerTest, ValidService) {
    createService("demo",
        R"({"manifestVersion":"1","id":"demo","name":"Demo","version":"1.0.0"})");
    ServiceScanner scanner;
    ServiceScanner::ScanStats stats;
    auto result = scanner.scan(servicesDir, &stats);
    EXPECT_EQ(result.size(), 1);
    EXPECT_TRUE(result.contains("demo"));
    EXPECT_EQ(result["demo"].name, "Demo");
    EXPECT_EQ(stats.loadedServices, 1);
    EXPECT_EQ(stats.failedServices, 0);
}

TEST_F(ServiceScannerTest, InvalidManifest) {
    createService("bad", "not json");
    ServiceScanner scanner;
    ServiceScanner::ScanStats stats;
    auto result = scanner.scan(servicesDir, &stats);
    EXPECT_TRUE(result.isEmpty());
    EXPECT_EQ(stats.failedServices, 1);
}

TEST_F(ServiceScannerTest, MixedServices) {
    createService("good",
        R"({"manifestVersion":"1","id":"good","name":"Good","version":"1.0.0"})");
    createService("bad", "{}");  // 缺少必要字段
    ServiceScanner scanner;
    ServiceScanner::ScanStats stats;
    auto result = scanner.scan(servicesDir, &stats);
    EXPECT_EQ(stats.loadedServices, 1);
    EXPECT_GE(stats.failedServices, 1);
}
```

---

## 7. 依赖关系

### 7.1 前置依赖

| 依赖项 | 说明 |
|--------|------|
| 里程碑 28（Service 配置 Schema） | `ServiceConfigSchema`、`ServiceConfigValidator`、`ServiceManifest`、`ServiceDirectory` 等底层类 |
| stdiolink 核心库 | `PlatformUtils`、`DriverScanner`（后续里程碑使用） |

### 7.2 后置影响

| 后续里程碑 | 依赖内容 |
|-----------|---------|
| 里程碑 35（DriverManagerScanner） | `main.cpp` 启动流程、`ServerConfig` |
| 里程碑 36（ProjectManager） | `ServiceScanner` 提供的 `ServiceInfo` 集合 |
| 里程碑 37（InstanceManager） | `ServerConfig.serviceProgram` 路径查找 |
| 里程碑 38（HTTP API） | 所有基础设施 |
