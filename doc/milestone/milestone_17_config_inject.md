# 里程碑 17：配置注入闭环

## 1. 目标

实现从"UI 保存"到"驱动生效"的完整链路。

## 2. 对应需求

- **需求5**: 配置注入与生效闭环 (Config Injected Loop)

## 3. 注入策略

| 方式 | 说明 |
|------|------|
| env | 环境变量注入 |
| args | 启动参数注入（等价 startupArgs） |
| file | 配置文件注入 |
| command | 运行时命令注入 |

### 3.1 具体执行规则（补充）

- **env**：Host 在启动 Driver 时追加环境变量（`ENV_PREFIX + key = value`），需要重启生效。
- **args**：Host 在启动参数中追加 `--key=value`，需要重启生效。
- **file**：Host 生成 JSON 配置文件并传入文件路径（如 `--config=<path>`），需要重启生效。
- **command**：Host 调用 `meta.config.set` 运行时生效，不需重启。

### 3.2 失败与回滚策略

- 注入失败必须返回非 0 或 `error` 响应
- 对于 `env/args/file` 方式，失败时不得覆盖原配置文件
- 对于 `command` 方式，失败时应返回错误码并保持原配置

## 4. ConfigApply 结构

```cpp
struct ConfigApply {
    QString method;     // env|args|file|command
    QString envPrefix;  // 环境变量前缀
    QString command;    // 配置命令名
    QString fileName;   // 配置文件名
};
```

## 5. 热重载机制

```cpp
// meta.config.set 命令
// meta.config.reload 命令
```

补充建议：
- `meta.config.reload` 仅用于 `file` 模式的热加载
- `env/args` 模式需要重启 Driver（由 Host 负责）

## 6. 单元测试用例

### 6.1 测试文件：tests/test_config_inject.cpp

```cpp
#include <gtest/gtest.h>
#include "stdiolink/host/config_injector.h"
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class ConfigInjectTest : public ::testing::Test {};

// 测试环境变量注入
TEST_F(ConfigInjectTest, EnvInjection) {
    ConfigApply apply;
    apply.method = "env";
    apply.envPrefix = "DRIVER_";

    QJsonObject config{{"timeout", 5000}, {"debug", true}};

    auto envVars = ConfigInjector::toEnvVars(config, apply);
    EXPECT_EQ(envVars["DRIVER_TIMEOUT"], "5000");
    EXPECT_EQ(envVars["DRIVER_DEBUG"], "true");
}

// 测试启动参数注入
TEST_F(ConfigInjectTest, ArgsInjection) {
    ConfigApply apply;
    apply.method = "args";

    QJsonObject config{{"timeout", 5000}};

    QStringList args = ConfigInjector::toArgs(config, apply);
    EXPECT_TRUE(args.contains("--timeout=5000"));
}

// 测试配置文件注入
TEST_F(ConfigInjectTest, FileInjection) {
    ConfigApply apply;
    apply.method = "file";
    apply.fileName = "config.json";

    QJsonObject config{{"timeout", 5000}};
    QString path = QDir::temp().filePath("test_config.json");

    EXPECT_TRUE(ConfigInjector::toFile(config, path));
    EXPECT_TRUE(QFile::exists(path));

    QFile::remove(path);
}
```

### 6.2 运行时命令注入测试

```cpp
// 测试 meta.config.set 命令
TEST_F(ConfigInjectTest, CommandInjection) {
    // 模拟 Driver 接收 meta.config.set
    MockDriver driver;
    QJsonObject config{{"timeout", 3000}};

    auto result = driver.request("meta.config.set", config);
    EXPECT_TRUE(result.isSuccess());
}

// 测试 meta.config.reload 命令
TEST_F(ConfigInjectTest, ConfigReload) {
    MockDriver driver;
    auto result = driver.request("meta.config.reload", QJsonObject{});
    EXPECT_TRUE(result.isSuccess());
}
```

说明：
- 需新增 `ConfigInjector` 工具类（将 config 转 env/args/file）
- 需提供最小 `MockDriver`（模拟 request 返回结果）

### 6.3 失败与回滚测试

```cpp
// 测试注入失败不覆盖原配置
TEST_F(ConfigInjectTest, FailureNoOverwrite) {
    QString path = QDir::temp().filePath("original.json");
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("{\"original\": true}");
    f.close();

    // 模拟写入失败（只读目录等）
    // 验证原文件未被修改
}

// 测试命令注入失败返回错误
TEST_F(ConfigInjectTest, CommandFailureReturnsError) {
    MockDriver driver;
    QJsonObject invalidConfig{{"unknown_field", "value"}};

    auto result = driver.request("meta.config.set", invalidConfig);
    EXPECT_TRUE(result.isError());
}
```

## 7. 依赖关系

- **前置**: M16 (高级UI生成)
- **后续**: M18 (版本协商)
