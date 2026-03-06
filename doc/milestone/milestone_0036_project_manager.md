# 里程碑 36：ProjectManager 与配置验证

> **前置条件**: 里程碑 34（ServiceScanner）、里程碑 35（DriverManagerScanner）已完成
> **目标**: 实现 Project 配置文件的加载、保存、Schema 验证，以及 Schedule 调度参数解析

---

## 1. 目标

- 实现 `ProjectManager`，管理 `projects/` 目录下的 Project 配置文件
- 支持 Project 的加载、创建、更新、删除操作
- 复用 `ServiceConfigValidator` 对 Project 的 `config` 进行 Schema 验证
- 实现 `Schedule` 调度参数解析（`manual`、`fixed_rate`、`daemon`）
- 验证失败的 Project 标记为 `invalid`，不参与调度
- 在 `main.cpp` 中集成 Project 加载和验证步骤

---

## 2. 技术要点

### 2.1 Project 配置文件格式

每个 Project 对应 `projects/` 下的一个 JSON 文件：

```json
{
  "name": "料仓A数据采集",
  "serviceId": "data-collector",
  "enabled": true,
  "schedule": {
    "type": "fixed_rate",
    "intervalMs": 5000,
    "maxConcurrent": 1
  },
  "config": {
    "device": { "host": "192.168.1.100", "port": 502 },
    "polling": { "intervalMs": 1000, "registers": [0, 1, 2] }
  }
}
```

### 2.2 Project ID 约束

- Project ID 取自文件名（去掉 `.json` 后缀）
- 必须匹配正则 `^[A-Za-z0-9_-]+$`
- 禁止包含 `/`、`\`、`..`、空白字符
- 文件路径固定为 `projects/{id}.json`

### 2.3 验证流程

```
加载 Project 配置时:
  1. 读取 projects/{id}.json
  2. 解析 JSON，提取 serviceId
  3. 查找对应的 ServiceInfo（来自 ServiceScanner）
  4. 若 Service 不存在 → invalid
  5. 使用 ServiceConfigValidator 验证 config（Service 的 schema 在 M34 已保证存在）
  6. 解析 schedule 参数
  7. 验证通过 → valid，验证失败 → invalid + 记录 error
```

### 2.4 Schedule 类型

| 类型 | 参数 | 说明 |
|------|------|------|
| `manual` | 无 | 不自动启动，仅手动触发 |
| `fixed_rate` | `intervalMs`, `maxConcurrent`(默认1) | 固定频率定时触发 |
| `daemon` | `restartDelayMs`(默认3000), `maxConsecutiveFailures`(默认5) | 守护进程，异常退出自动重启 |

---

## 3. 实现步骤

### 3.1 Schedule 数据结构

```cpp
// src/stdiolink_server/model/schedule.h
#pragma once

#include <QJsonObject>
#include <QString>

namespace stdiolink_server {

enum class ScheduleType { Manual, FixedRate, Daemon };

struct Schedule {
    ScheduleType type = ScheduleType::Manual;
    int intervalMs = 5000;          // fixed_rate
    int maxConcurrent = 1;          // fixed_rate
    int restartDelayMs = 3000;      // daemon
    int maxConsecutiveFailures = 5; // daemon

    static Schedule fromJson(const QJsonObject& obj, QString& error);
    QJsonObject toJson() const;
};

} // namespace stdiolink_server
```

### 3.2 Schedule 实现

```cpp
// src/stdiolink_server/model/schedule.cpp
#include "schedule.h"

namespace stdiolink_server {

Schedule Schedule::fromJson(const QJsonObject& obj, QString& error) {
    Schedule s;
    const QString typeStr = obj["type"].toString("manual");

    if (typeStr == "manual") {
        s.type = ScheduleType::Manual;
    } else if (typeStr == "fixed_rate") {
        s.type = ScheduleType::FixedRate;
        s.intervalMs = obj["intervalMs"].toInt(5000);
        s.maxConcurrent = obj["maxConcurrent"].toInt(1);
        if (s.intervalMs < 100) {
            error = "schedule.intervalMs must be >= 100";
            return s;
        }
        if (s.maxConcurrent < 1) {
            error = "schedule.maxConcurrent must be >= 1";
            return s;
        }
    } else if (typeStr == "daemon") {
        s.type = ScheduleType::Daemon;
        s.restartDelayMs = obj["restartDelayMs"].toInt(3000);
        s.maxConsecutiveFailures = obj["maxConsecutiveFailures"].toInt(5);
        if (s.restartDelayMs < 0) {
            error = "schedule.restartDelayMs must be >= 0";
            return s;
        }
        if (s.maxConsecutiveFailures < 1) {
            error = "schedule.maxConsecutiveFailures must be >= 1";
            return s;
        }
    } else {
        error = "unknown schedule type: " + typeStr;
        return s;
    }
    return s;
}

QJsonObject Schedule::toJson() const {
    QJsonObject obj;
    switch (type) {
    case ScheduleType::Manual:
        obj["type"] = "manual";
        break;
    case ScheduleType::FixedRate:
        obj["type"] = "fixed_rate";
        obj["intervalMs"] = intervalMs;
        obj["maxConcurrent"] = maxConcurrent;
        break;
    case ScheduleType::Daemon:
        obj["type"] = "daemon";
        obj["restartDelayMs"] = restartDelayMs;
        obj["maxConsecutiveFailures"] = maxConsecutiveFailures;
        break;
    }
    return obj;
}

} // namespace stdiolink_server
```

### 3.3 Project 数据结构

```cpp
// src/stdiolink_server/model/project.h
#pragma once

#include <QJsonObject>
#include <QString>
#include "schedule.h"

namespace stdiolink_server {

struct Project {
    QString id;
    QString name;
    QString serviceId;
    bool enabled = true;
    Schedule schedule;
    QJsonObject config;

    bool valid = true;
    QString error;

    static Project fromJson(const QString& id,
                            const QJsonObject& obj,
                            QString& parseError);
    QJsonObject toJson() const;
};

} // namespace stdiolink_server
```

### 3.4 ProjectManager 头文件

```cpp
// src/stdiolink_server/manager/project_manager.h
#pragma once

#include <QMap>
#include <QString>
#include "model/project.h"
#include "scanner/service_scanner.h"

namespace stdiolink_server {

class ProjectManager {
public:
    struct LoadStats {
        int loaded = 0;
        int invalid = 0;
    };

    /// 加载 projects/ 目录下所有 Project 并验证
    QMap<QString, Project> loadAll(
        const QString& projectsDir,
        const QMap<QString, ServiceInfo>& services,
        LoadStats* stats = nullptr);

    /// 验证单个 Project 的 config
    static bool validateProject(
        Project& project,
        const QMap<QString, ServiceInfo>& services);

    /// 保存 Project 到文件
    static bool saveProject(const QString& projectsDir,
                            const Project& project,
                            QString& error);

    /// 删除 Project 文件
    static bool removeProject(const QString& projectsDir,
                              const QString& id,
                              QString& error);

    /// 校验 Project ID 格式
    static bool isValidProjectId(const QString& id);

private:
    static Project loadOne(const QString& filePath,
                           const QString& id);
};

} // namespace stdiolink_server
```

### 3.5 ProjectManager 实现

```cpp
// src/stdiolink_server/manager/project_manager.cpp
#include "project_manager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include "config/service_config_validator.h"

using namespace stdiolink_service;

namespace stdiolink_server {

bool ProjectManager::isValidProjectId(const QString& id) {
    static const QRegularExpression re("^[A-Za-z0-9_-]+$");
    return !id.isEmpty() && re.match(id).hasMatch();
}
```

**`loadOne()` 和 `loadAll()` 核心逻辑**：

```cpp
Project ProjectManager::loadOne(const QString& filePath,
                                const QString& id) {
    Project p;
    p.id = id;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        p.valid = false;
        p.error = "cannot open file: " + filePath;
        return p;
    }

    QJsonParseError jsonErr;
    auto doc = QJsonDocument::fromJson(f.readAll(), &jsonErr);
    if (jsonErr.error != QJsonParseError::NoError) {
        p.valid = false;
        p.error = "JSON parse error: " + jsonErr.errorString();
        return p;
    }

    QString parseErr;
    p = Project::fromJson(id, doc.object(), parseErr);
    if (!parseErr.isEmpty()) {
        p.valid = false;
        p.error = parseErr;
    }
    return p;
}

QMap<QString, Project> ProjectManager::loadAll(
    const QString& projectsDir,
    const QMap<QString, ServiceInfo>& services,
    LoadStats* stats)
{
    QMap<QString, Project> result;
    QDir dir(projectsDir);
    if (!dir.exists()) return result;

    const auto entries = dir.entryList({"*.json"}, QDir::Files);
    for (const QString& entry : entries) {
        const QString id = entry.chopped(5); // 去掉 ".json"
        if (!isValidProjectId(id)) {
            qWarning("ProjectManager: skip invalid id: %s",
                     qUtf8Printable(entry));
            continue;
        }

        Project p = loadOne(dir.absoluteFilePath(entry), id);
        if (p.valid) {
            validateProject(p, services);
        }

        if (p.valid) {
            if (stats) stats->loaded++;
        } else {
            if (stats) stats->invalid++;
            qWarning("ProjectManager: %s invalid: %s",
                     qUtf8Printable(id),
                     qUtf8Printable(p.error));
        }
        result.insert(id, p);
    }
    return result;
}
```

**`validateProject()` 验证逻辑**：

```cpp
bool ProjectManager::validateProject(
    Project& project,
    const QMap<QString, ServiceInfo>& services)
{
    // 1. 检查 serviceId 对应的 Service 是否存在
    if (!services.contains(project.serviceId)) {
        project.valid = false;
        project.error = "service not found: " + project.serviceId;
        return false;
    }

    const ServiceInfo& svc = services[project.serviceId];

    // 2. 验证 config（schema 在 ServiceScanner 阶段已保证存在）
    QJsonObject merged;
    auto result = ServiceConfigValidator::mergeAndValidate(
        svc.configSchema,
        QJsonObject{},       // fileConfig: Project 场景为空
        project.config,      // 直接视作 typed config
        UnknownFieldPolicy::Reject,
        merged);
    if (!result.valid) {
        project.valid = false;
        project.error = result.toString();
        return false;
    }

    project.valid = true;
    project.error.clear();
    return true;
}
```

**`saveProject()` 和 `removeProject()`**：

```cpp
bool ProjectManager::saveProject(const QString& projectsDir,
                                 const Project& project,
                                 QString& error)
{
    if (!isValidProjectId(project.id)) {
        error = "invalid project id: " + project.id;
        return false;
    }

    const QString path = projectsDir + "/" + project.id + ".json";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = "cannot write file: " + path;
        return false;
    }

    QJsonDocument doc(project.toJson());
    f.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool ProjectManager::removeProject(const QString& projectsDir,
                                   const QString& id,
                                   QString& error)
{
    const QString path = projectsDir + "/" + id + ".json";
    if (!QFile::exists(path)) {
        error = "project not found: " + id;
        return false;
    }
    if (!QFile::remove(path)) {
        error = "cannot remove file: " + path;
        return false;
    }
    return true;
}

} // namespace stdiolink_server
```

### 3.6 main.cpp 集成

在里程碑 35 的 `main.cpp` 中，Driver 扫描之后插入 Project 加载：

```cpp
// main.cpp — 新增部分
#include "manager/project_manager.h"

// ... Driver 扫描之后 ...

// 加载并验证 Project
ProjectManager projectManager;
ProjectManager::LoadStats projStats;
auto projects = projectManager.loadAll(
    dataRoot + "/projects", services, &projStats);
qInfo("Projects: %d loaded, %d invalid",
      projStats.loaded, projStats.invalid);
```

---

## 4. 文件清单

| 操作 | 文件 | 说明 |
|------|------|------|
| 新增 | `src/stdiolink_server/model/schedule.h` | Schedule 数据结构头文件 |
| 新增 | `src/stdiolink_server/model/schedule.cpp` | Schedule 实现 |
| 新增 | `src/stdiolink_server/model/project.h` | Project 数据结构头文件 |
| 新增 | `src/stdiolink_server/manager/project_manager.h` | ProjectManager 头文件 |
| 新增 | `src/stdiolink_server/manager/project_manager.cpp` | ProjectManager 实现 |
| 修改 | `src/stdiolink_server/main.cpp` | 集成 Project 加载步骤 |
| 修改 | `src/stdiolink_server/CMakeLists.txt` | 新增源文件 |

---

## 5. 验收标准

1. `projects/` 目录不存在时不报错，返回空集合
2. 合法 JSON 文件被正确解析为 `Project` 对象
3. 非法 JSON 文件（语法错误）标记为 `invalid`
4. `serviceId` 对应的 Service 不存在时标记为 `invalid`
5. Service 有 Schema 时，`config` 通过 `ServiceConfigValidator` 验证
6. 验证失败的 Project 记录详细错误信息
7. Service schema 缺失视为前置数据错误（应在 ServiceScanner 阶段拦截）
8. `Schedule` 三种类型（`manual`、`fixed_rate`、`daemon`）正确解析
9. `Schedule` 参数越界时报错（如 `intervalMs < 100`）
10. 未知 `schedule.type` 报错
11. Project ID 格式校验：仅允许 `[A-Za-z0-9_-]`
12. `saveProject()` 正确写入 JSON 文件
13. `removeProject()` 正确删除文件，不存在时返回错误
14. `LoadStats` 各计数器准确反映加载结果

---

## 6. 单元测试用例

### 6.1 Schedule 解析测试

```cpp
#include <gtest/gtest.h>
#include "model/schedule.h"

using namespace stdiolink_server;

TEST(ScheduleTest, ManualDefault) {
    QJsonObject obj{{"type", "manual"}};
    QString err;
    auto s = Schedule::fromJson(obj, err);
    EXPECT_TRUE(err.isEmpty());
    EXPECT_EQ(s.type, ScheduleType::Manual);
}

TEST(ScheduleTest, FixedRate) {
    QJsonObject obj{
        {"type", "fixed_rate"},
        {"intervalMs", 3000},
        {"maxConcurrent", 2}
    };
    QString err;
    auto s = Schedule::fromJson(obj, err);
    EXPECT_TRUE(err.isEmpty());
    EXPECT_EQ(s.type, ScheduleType::FixedRate);
    EXPECT_EQ(s.intervalMs, 3000);
    EXPECT_EQ(s.maxConcurrent, 2);
}

TEST(ScheduleTest, FixedRateInvalidInterval) {
    QJsonObject obj{{"type", "fixed_rate"}, {"intervalMs", 50}};
    QString err;
    Schedule::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}

TEST(ScheduleTest, Daemon) {
    QJsonObject obj{
        {"type", "daemon"},
        {"restartDelayMs", 5000},
        {"maxConsecutiveFailures", 3}
    };
    QString err;
    auto s = Schedule::fromJson(obj, err);
    EXPECT_TRUE(err.isEmpty());
    EXPECT_EQ(s.type, ScheduleType::Daemon);
    EXPECT_EQ(s.restartDelayMs, 5000);
    EXPECT_EQ(s.maxConsecutiveFailures, 3);
}

TEST(ScheduleTest, UnknownType) {
    QJsonObject obj{{"type", "cron"}};
    QString err;
    Schedule::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}
```

### 6.2 ProjectManager 测试

```cpp
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>
#include "manager/project_manager.h"

using namespace stdiolink_server;

class ProjectManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmpDir.isValid());
        projectsDir = tmpDir.path() + "/projects";
        QDir().mkpath(projectsDir);
    }

    void writeProject(const QString& id, const QJsonObject& obj) {
        QFile f(projectsDir + "/" + id + ".json");
        f.open(QIODevice::WriteOnly);
        f.write(QJsonDocument(obj).toJson());
    }

    QMap<QString, ServiceInfo> makeServices() {
        ServiceInfo svc;
        svc.id = "demo";
        svc.name = "Demo";
        svc.valid = true;
        svc.hasSchema = true;
        return {{"demo", svc}};
    }

    QTemporaryDir tmpDir;
    QString projectsDir;
};
```

**测试用例**：

```cpp
TEST_F(ProjectManagerTest, EmptyDirectory) {
    ProjectManager mgr;
    ProjectManager::LoadStats stats;
    auto result = mgr.loadAll(projectsDir, makeServices(), &stats);
    EXPECT_TRUE(result.isEmpty());
    EXPECT_EQ(stats.loaded, 0);
}

TEST_F(ProjectManagerTest, ValidProject) {
    writeProject("test-1", QJsonObject{
        {"name", "Test"},
        {"serviceId", "demo"},
        {"enabled", true},
        {"schedule", QJsonObject{{"type", "manual"}}},
        {"config", QJsonObject{}}
    });
    ProjectManager mgr;
    ProjectManager::LoadStats stats;
    auto result = mgr.loadAll(projectsDir, makeServices(), &stats);
    EXPECT_EQ(result.size(), 1);
    EXPECT_TRUE(result["test-1"].valid);
    EXPECT_EQ(stats.loaded, 1);
}
```

```cpp
TEST_F(ProjectManagerTest, InvalidJson) {
    QFile f(projectsDir + "/bad.json");
    f.open(QIODevice::WriteOnly);
    f.write("not json");
    f.close();

    ProjectManager mgr;
    ProjectManager::LoadStats stats;
    auto result = mgr.loadAll(projectsDir, makeServices(), &stats);
    EXPECT_EQ(stats.invalid, 1);
    EXPECT_FALSE(result["bad"].valid);
}

TEST_F(ProjectManagerTest, UnknownService) {
    writeProject("orphan", QJsonObject{
        {"name", "Orphan"},
        {"serviceId", "nonexistent"},
        {"enabled", true},
        {"schedule", QJsonObject{{"type", "manual"}}},
        {"config", QJsonObject{}}
    });
    ProjectManager mgr;
    ProjectManager::LoadStats stats;
    auto result = mgr.loadAll(projectsDir, makeServices(), &stats);
    EXPECT_FALSE(result["orphan"].valid);
    EXPECT_EQ(stats.invalid, 1);
}

TEST_F(ProjectManagerTest, InvalidProjectId) {
    EXPECT_FALSE(ProjectManager::isValidProjectId(""));
    EXPECT_FALSE(ProjectManager::isValidProjectId("a/b"));
    EXPECT_FALSE(ProjectManager::isValidProjectId("a b"));
    EXPECT_TRUE(ProjectManager::isValidProjectId("silo-a"));
    EXPECT_TRUE(ProjectManager::isValidProjectId("test_123"));
}
```

---

## 7. 依赖关系

### 7.1 前置依赖

| 依赖项 | 说明 |
|--------|------|
| 里程碑 34（Server 脚手架） | `main.cpp` 启动流程、`ServiceScanner`、`ServiceInfo` |
| 里程碑 35（DriverManagerScanner） | `main.cpp` 中 Driver 扫描步骤 |
| `ServiceConfigValidator` | `mergeAndValidate()` 验证 Project config |
| `ServiceConfigSchema` | Schema 数据结构 |

### 7.2 后置影响

| 后续里程碑 | 依赖内容 |
|-----------|---------|
| 里程碑 37（InstanceManager） | `Project` 数据结构、`Schedule` 调度参数 |
| 里程碑 38（HTTP API） | `ProjectManager` 的 CRUD 操作、`validateProject()` |
