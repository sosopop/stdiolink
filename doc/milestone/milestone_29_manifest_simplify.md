# 里程碑 29：Manifest 精简与固定文件名约定

## 1. 目标

精简 `manifest.json` 结构，去除所有路径配置字段（如 `entry`），仅保留服务描述信息。同时确立服务目录的固定文件名约定：

- 入口脚本固定为 `index.js`
- 配置 Schema 固定为 `config.schema.json`
- `manifest.json` 仅承载 id / name / version 等描述信息

## 2. 设计概述

### 2.1 服务目录规范

```
my_service/
  manifest.json
  index.js
  config.schema.json
```

### 2.2 manifest.json 最终最小结构

```json
{
  "manifestVersion": "1",
  "id": "com.example.demo",
  "name": "Demo Service",
  "version": "0.1.0",
  "description": "optional",
  "author": "optional"
}
```

### 2.3 固定文件名约定（强制）

| 约定 | 说明 |
|------|------|
| 入口脚本 | 固定命名为 `index.js`，不可配置 |
| 配置 Schema | 固定命名为 `config.schema.json`，不可配置 |
| 完整性要求 | 服务目录必须同时包含上述两个文件及 `manifest.json` |
| 只读文件集 | `manifest.json` 与 `config.schema.json` 属于只读描述文件，可在不执行脚本的场景下使用 |

### 2.4 与现有 M28 的差异

本里程碑**仅定义目录约定和 manifest 结构，不改动 schema 加载链路**。Schema 加载实现的切换在 M31 中完成。

| 对比项 | M28（当前） | M29（本里程碑） | 后续（M31） |
|--------|-----------|---------------|------------|
| Schema 来源 | JS 脚本内 `defineConfig()` 声明 | 不变（仅约定文件名） | 切换为读取 `config.schema.json` |
| 入口配置 | 命令行直接传脚本路径 | 不变（仅约定文件名） | 由 M30 切换为固定入口 |
| manifest 角色 | 不存在 | **新增**：仅描述信息，无路径配置 | 不变 |

## 3. 技术要点

### 3.1 ServiceManifest 结构

```cpp
// src/stdiolink_service/config/service_manifest.h
#pragma once

#include <QJsonObject>
#include <QString>

namespace stdiolink_service {

struct ServiceManifest {
    QString manifestVersion;  // 固定 "1"
    QString id;               // 服务唯一标识，如 "com.example.demo"
    QString name;             // 服务显示名称
    QString version;          // 语义化版本号
    QString description;      // 可选描述
    QString author;           // 可选作者

    /// 从 JSON 对象解析 manifest
    static ServiceManifest fromJson(const QJsonObject& obj, QString& error);

    /// 从文件加载 manifest
    static ServiceManifest loadFromFile(const QString& filePath, QString& error);

    /// 校验必填字段
    bool isValid(QString& error) const;
};

} // namespace stdiolink_service
```

### 3.2 解析与校验规则

- `manifestVersion` 必须为 `"1"`，其他值报错
- `id` 必填，格式建议 reverse-domain（仅警告，不强制）
- `name` 必填，非空字符串
- `version` 必填，非空字符串
- `description` 和 `author` 可选，缺失时为空字符串
- 出现未知字段时报错拒绝（严格模式，项目尚未发布，无需向前兼容）

## 4. 实现步骤

### 4.1 新增 service_manifest.h / .cpp

实现 `ServiceManifest` 结构体及其 `fromJson()`、`loadFromFile()`、`isValid()` 方法。

### 4.2 新增 service_directory.h / .cpp

封装服务目录的路径拼接逻辑：

```cpp
// src/stdiolink_service/config/service_directory.h
#pragma once

#include <QString>

namespace stdiolink_service {

class ServiceDirectory {
public:
    explicit ServiceDirectory(const QString& dirPath);

    QString manifestPath() const;       // <dir>/manifest.json
    QString entryPath() const;          // <dir>/index.js
    QString configSchemaPath() const;   // <dir>/config.schema.json

    /// 校验目录结构完整性（三个文件均存在且可读）
    bool validate(QString& error) const;

private:
    QString m_dirPath;
};

} // namespace stdiolink_service
```

### 4.3 修改 CMakeLists.txt

将新增源文件加入 `stdiolink_service` 构建目标。

## 5. 文件清单

| 操作 | 文件路径 | 说明 |
|------|---------|------|
| 新增 | `src/stdiolink_service/config/service_manifest.h` | Manifest 结构定义 |
| 新增 | `src/stdiolink_service/config/service_manifest.cpp` | Manifest 解析与校验实现 |
| 新增 | `src/stdiolink_service/config/service_directory.h` | 服务目录路径封装 |
| 新增 | `src/stdiolink_service/config/service_directory.cpp` | 目录校验实现 |
| 修改 | `src/stdiolink_service/CMakeLists.txt` | 添加新增源文件 |
| 新增 | `src/tests/test_service_manifest.cpp` | Manifest 单元测试 |
| 新增 | `src/tests/test_service_directory.cpp` | 目录校验单元测试 |
| 修改 | `src/tests/CMakeLists.txt` | 添加新增测试文件 |

## 6. 验收标准

1. `ServiceManifest::fromJson()` 正确解析包含 id/name/version 的 JSON
2. `manifestVersion` 非 `"1"` 时返回错误
3. 缺少 id / name / version 任一必填字段时返回错误
4. description / author 缺失时默认为空字符串，不报错
5. JSON 中出现未知字段（如 `entry`）时返回错误
6. `ServiceDirectory::validate()` 在三个文件均存在时返回 true
7. 缺少 `manifest.json` 时返回明确错误信息
8. 缺少 `index.js` 时返回明确错误信息
9. 缺少 `config.schema.json` 时返回明确错误信息
10. 路径拼接使用 `QDir::filePath()` 确保跨平台正确

## 7. 单元测试用例

### 7.1 test_service_manifest.cpp

```cpp
#include <gtest/gtest.h>
#include "config/service_manifest.h"

using namespace stdiolink_service;

// --- 正常解析 ---

TEST(ServiceManifest, ParseMinimalValid) {
    QJsonObject obj{
        {"manifestVersion", "1"},
        {"id", "com.example.test"},
        {"name", "Test Service"},
        {"version", "1.0.0"}
    };
    QString err;
    auto m = ServiceManifest::fromJson(obj, err);
    EXPECT_TRUE(err.isEmpty());
    EXPECT_EQ(m.id, "com.example.test");
    EXPECT_EQ(m.name, "Test Service");
    EXPECT_EQ(m.version, "1.0.0");
    EXPECT_TRUE(m.description.isEmpty());
    EXPECT_TRUE(m.author.isEmpty());
}

TEST(ServiceManifest, ParseWithOptionalFields) {
    QJsonObject obj{
        {"manifestVersion", "1"},
        {"id", "com.example.full"},
        {"name", "Full Service"},
        {"version", "2.0.0"},
        {"description", "A full example"},
        {"author", "Test Author"}
    };
    QString err;
    auto m = ServiceManifest::fromJson(obj, err);
    EXPECT_TRUE(err.isEmpty());
    EXPECT_EQ(m.description, "A full example");
    EXPECT_EQ(m.author, "Test Author");
}

// --- 必填字段缺失 ---

TEST(ServiceManifest, MissingManifestVersion) {
    QJsonObject obj{{"id", "x"}, {"name", "x"}, {"version", "1.0"}};
    QString err;
    ServiceManifest::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}

TEST(ServiceManifest, MissingId) {
    QJsonObject obj{{"manifestVersion", "1"}, {"name", "x"}, {"version", "1.0"}};
    QString err;
    ServiceManifest::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}

TEST(ServiceManifest, MissingName) {
    QJsonObject obj{{"manifestVersion", "1"}, {"id", "x"}, {"version", "1.0"}};
    QString err;
    ServiceManifest::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}

TEST(ServiceManifest, MissingVersion) {
    QJsonObject obj{{"manifestVersion", "1"}, {"id", "x"}, {"name", "x"}};
    QString err;
    ServiceManifest::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}

// --- 版本号校验 ---

TEST(ServiceManifest, InvalidManifestVersion) {
    QJsonObject obj{
        {"manifestVersion", "2"},
        {"id", "x"}, {"name", "x"}, {"version", "1.0"}
    };
    QString err;
    ServiceManifest::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}

// --- 未知字段拒绝 ---

TEST(ServiceManifest, RejectUnknownField) {
    QJsonObject obj{
        {"manifestVersion", "1"},
        {"id", "x"}, {"name", "x"}, {"version", "1.0"},
        {"entry", "custom.js"}
    };
    QString err;
    ServiceManifest::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
    // 未知字段 "entry" 应被拒绝
}

TEST(ServiceManifest, RejectArbitraryUnknownField) {
    QJsonObject obj{
        {"manifestVersion", "1"},
        {"id", "x"}, {"name", "x"}, {"version", "1.0"},
        {"foo", "bar"}
    };
    QString err;
    ServiceManifest::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}

// --- 文件加载 ---

TEST(ServiceManifest, LoadFromValidFile) {
    // 创建临时 manifest.json 文件并加载
    // 验证解析结果正确
}

TEST(ServiceManifest, LoadFromNonexistentFile) {
    QString err;
    ServiceManifest::loadFromFile("nonexistent.json", err);
    EXPECT_FALSE(err.isEmpty());
}

TEST(ServiceManifest, LoadFromMalformedJson) {
    // 创建包含非法 JSON 的临时文件
    // 验证 error 非空
}
```

### 7.2 test_service_directory.cpp

```cpp
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QFile>
#include "config/service_directory.h"

using namespace stdiolink_service;

class ServiceDirectoryTest : public ::testing::Test {
protected:
    void createFile(const QString& path, const QByteArray& content = "{}") {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(content);
        f.close();
    }
};

TEST_F(ServiceDirectoryTest, ValidDirectoryWithAllFiles) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/manifest.json");
    createFile(tmp.path() + "/index.js", "// entry");
    createFile(tmp.path() + "/config.schema.json");

    ServiceDirectory dir(tmp.path());
    QString err;
    EXPECT_TRUE(dir.validate(err));
}

TEST_F(ServiceDirectoryTest, MissingManifest) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/index.js");
    createFile(tmp.path() + "/config.schema.json");

    ServiceDirectory dir(tmp.path());
    QString err;
    EXPECT_FALSE(dir.validate(err));
    EXPECT_TRUE(err.contains("manifest.json"));
}

TEST_F(ServiceDirectoryTest, MissingIndexJs) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/manifest.json");
    createFile(tmp.path() + "/config.schema.json");

    ServiceDirectory dir(tmp.path());
    QString err;
    EXPECT_FALSE(dir.validate(err));
    EXPECT_TRUE(err.contains("index.js"));
}

TEST_F(ServiceDirectoryTest, MissingConfigSchema) {
    QTemporaryDir tmp;
    createFile(tmp.path() + "/manifest.json");
    createFile(tmp.path() + "/index.js");

    ServiceDirectory dir(tmp.path());
    QString err;
    EXPECT_FALSE(dir.validate(err));
    EXPECT_TRUE(err.contains("config.schema.json"));
}

TEST_F(ServiceDirectoryTest, PathConcatenation) {
    ServiceDirectory dir("/some/path/my_service");
    EXPECT_TRUE(dir.manifestPath().endsWith("manifest.json"));
    EXPECT_TRUE(dir.entryPath().endsWith("index.js"));
    EXPECT_TRUE(dir.configSchemaPath().endsWith("config.schema.json"));
}

TEST_F(ServiceDirectoryTest, NonexistentDirectory) {
    ServiceDirectory dir("/nonexistent/path");
    QString err;
    EXPECT_FALSE(dir.validate(err));
}
```

## 8. 依赖关系

- **前置依赖**：
  - 里程碑 28（Service 配置 Schema 与注入）：提供 `ServiceConfigSchema`、`ServiceArgs` 等基础设施
- **后续依赖**：
  - 里程碑 30（固定入口 index.js 加载机制）：依赖本里程碑的 `ServiceDirectory` 和 `ServiceManifest`
  - 里程碑 31（config.schema.json 外部 Schema 加载）：依赖本里程碑的目录约定
