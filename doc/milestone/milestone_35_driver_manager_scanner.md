# 里程碑 35：DriverManagerScanner

> **前置条件**: 里程碑 34（Server 脚手架与 ServiceScanner）已完成
> **目标**: 实现 Manager 层的 Driver 扫描器，在核心库 `DriverScanner` 基础上补充 `.failed` 隔离策略、meta 自动导出、手动重扫能力

---

## 1. 目标

- 实现 `DriverManagerScanner`，封装核心库 `stdiolink::DriverScanner` 并补充 Manager 层策略
- 支持 `.failed` 后缀目录的自动跳过
- 支持缺失 `driver.meta.json` 时自动执行 Driver 导出（`--export-meta`）
- 导出失败时将目录重命名为 `<dir>.failed` 并记录日志
- 支持手动触发重扫（刷新已有 meta、重新导出缺失 meta）
- 在 `main.cpp` 中集成 Driver 扫描步骤

---

## 2. 技术要点

### 2.1 与核心库 DriverScanner 的关系

| 层级 | 类 | 职责 |
|------|-----|------|
| 核心库 | `stdiolink::DriverScanner` | 扫描目录、构建 `DriverConfig`（public: `scanDirectory`） |
| 核心库 | `stdiolink::DriverCatalog` | 持有 Driver 配置快照，提供查询和健康检查 |
| Manager 层 | `DriverManagerScanner` | 编排层：`.failed` 跳过、meta 导出、失败隔离、重扫 |

说明：当前代码中 `DriverScanner::loadMetaFromFile()` 与 `findExecutableInDirectory()` 为 private，
Manager 层不能直接调用；`DriverManagerScanner` 需实现本地 helper 完成可执行文件发现和 meta 合法性检查。

### 2.2 扫描流程

```
扫描 drivers/ 目录:
  for each 子目录:
    if 目录名以 .failed 结尾:
      跳过，skippedFailed++
      continue

    if 存在 driver.meta.json:
      if refreshMeta:
        尝试重新导出覆盖
        导出失败 → 保留旧 meta（不标记 .failed）
      调用 DriverScanner 加载 meta
    else:
      尝试执行 Driver 导出 meta
      if 导出成功:
        调用 DriverScanner 加载 meta
      else:
        重命名目录为 <dir>.failed
        newlyFailed++
```

### 2.3 Meta 导出机制

Driver 可执行文件支持 `--export-meta=<path>` 参数，将自身元数据导出为 `driver.meta.json`。

**导出命令**：
```bash
<driver_executable> --export-meta=<driver_dir>/driver.meta.json
```

**成功判定**：
- 进程退出码为 0
- 输出文件存在且为合法 JSON
- 超时阈值：10 秒（`QProcess::waitForFinished`）

**失败处理**：
- 进程启动失败、超时、退出码非 0、输出文件缺失或非法 JSON
- 对于首次导出失败：重命名目录为 `<dir>.failed`
- 对于 refreshMeta 模式下的重新导出失败：保留旧 meta，仅记录警告日志

### 2.4 .failed 目录恢复

`.failed` 目录需要人工介入恢复：
1. 修复 Driver 可执行文件或配置
2. 手动将目录名中的 `.failed` 后缀去掉
3. 触发手动重扫（`POST /api/drivers/scan`，里程碑 38 实现）

---

## 3. 实现步骤

### 3.1 DriverManagerScanner 头文件

```cpp
// src/stdiolink_server/scanner/driver_manager_scanner.h
#pragma once

#include <QString>
#include <QHash>
#include "stdiolink/host/driver_catalog.h"

namespace stdiolink_server {

class DriverManagerScanner {
public:
    struct ScanStats {
        int scanned = 0;
        int updated = 0;
        int newlyFailed = 0;
        int skippedFailed = 0;
    };

    /// 扫描 drivers/ 目录
    /// @param driversDir drivers 目录绝对路径
    /// @param refreshMeta 是否对已有 meta 的目录重新导出
    /// @return Driver 配置集合
    QHash<QString, stdiolink::DriverConfig> scan(
        const QString& driversDir,
        bool refreshMeta = true,
        ScanStats* stats = nullptr) const;

private:
    static constexpr int kExportTimeoutMs = 10000;

    bool tryExportMeta(const QString& executable,
                       const QString& metaPath) const;
    static QString findDriverExecutable(const QString& dirPath);
    static bool loadMetaFile(const QString& metaPath,
                             stdiolink::DriverConfig& config);
    static bool isFailedDir(const QString& dirName);
    static bool markFailed(const QString& dirPath);
};

} // namespace stdiolink_server
```

### 3.2 DriverManagerScanner 实现

```cpp
// src/stdiolink_server/scanner/driver_manager_scanner.cpp
#include "driver_manager_scanner.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QJsonDocument>
#include <memory>
#include "stdiolink/protocol/meta_types.h"
#include "stdiolink/platform/platform_utils.h"

namespace stdiolink_server {

QString DriverManagerScanner::findDriverExecutable(const QString& dirPath) {
    QDir dir(dirPath);
    QStringList filters;
    filters << stdiolink::PlatformUtils::executableFilter();
    const auto files = dir.entryList(filters, QDir::Files | QDir::Executable);
    return files.isEmpty() ? QString() : dir.absoluteFilePath(files.first());
}

bool DriverManagerScanner::loadMetaFile(
    const QString& metaPath, stdiolink::DriverConfig& config)
{
    QFile f(metaPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray data = f.readAll();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    auto meta = std::make_shared<stdiolink::meta::DriverMeta>(
        stdiolink::meta::DriverMeta::fromJson(doc.object()));
    if (meta->info.id.isEmpty()) {
        return false;
    }

    config.id = meta->info.id;
    config.meta = meta;
    config.program = findDriverExecutable(QFileInfo(metaPath).absolutePath());
    return true;
}

bool DriverManagerScanner::isFailedDir(const QString& dirName) {
    return dirName.endsWith(".failed");
}

bool DriverManagerScanner::markFailed(const QString& dirPath) {
    QDir parent(QFileInfo(dirPath).absolutePath());
    QString oldName = QFileInfo(dirPath).fileName();
    QString newName = oldName + ".failed";
    return parent.rename(oldName, newName);
}

bool DriverManagerScanner::tryExportMeta(
    const QString& executable, const QString& metaPath) const
{
    QProcess proc;
    proc.setProgram(executable);
    proc.setArguments({"--export-meta=" + metaPath});
    proc.start();
    if (!proc.waitForFinished(kExportTimeoutMs)) {
        proc.kill();
        return false;
    }
    if (proc.exitCode() != 0) return false;

    // 验证输出文件
    QFile f(metaPath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err;
    QJsonDocument::fromJson(f.readAll(), &err);
    return err.error == QJsonParseError::NoError;
}
```

**`scan()` 核心逻辑**：

```cpp
QHash<QString, stdiolink::DriverConfig> DriverManagerScanner::scan(
    const QString& driversDir, bool refreshMeta, ScanStats* stats) const
{
    QHash<QString, stdiolink::DriverConfig> result;
    QDir dir(driversDir);
    if (!dir.exists()) return result;

    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        const QString subDir = dir.absoluteFilePath(entry);

        // 跳过 .failed 目录
        if (isFailedDir(entry)) {
            if (stats) stats->skippedFailed++;
            continue;
        }
        if (stats) stats->scanned++;

        const QString metaPath = subDir + "/driver.meta.json";
        const bool hasMeta = QFileInfo::exists(metaPath);
        const QString exe = findDriverExecutable(subDir);

        if (!hasMeta) {
            // 无 meta：尝试导出
            if (exe.isEmpty() || !tryExportMeta(exe, metaPath)) {
                qWarning("Driver export failed: %s",
                         qUtf8Printable(entry));
                markFailed(subDir);
                if (stats) stats->newlyFailed++;
                continue;
            }
        } else if (refreshMeta && !exe.isEmpty()) {
            // 有 meta + refreshMeta：尝试重新导出（失败保留旧 meta）
            if (!tryExportMeta(exe, metaPath)) {
                qWarning("Driver re-export failed, keeping old meta: %s",
                         qUtf8Printable(entry));
            }
        }

        // 加载 meta（Manager 本地 helper，避免依赖 DriverScanner private 方法）
        stdiolink::DriverConfig config;
        if (loadMetaFile(metaPath, config)) {
            if (stats) stats->updated++;
            result.insert(config.id, config);
        }
    }
    return result;
}

} // namespace stdiolink_server
```

### 3.3 main.cpp 集成

在里程碑 34 的 `main.cpp` 中，Service 扫描之后插入 Driver 扫描：

```cpp
// main.cpp — 新增部分
#include "scanner/driver_manager_scanner.h"
#include "stdiolink/host/driver_catalog.h"

// ... Service 扫描之后 ...

// 扫描 Driver
DriverManagerScanner driverScanner;
DriverManagerScanner::ScanStats drvStats;
QString driversDir = dataRoot + "/drivers";
if (QDir(driversDir).exists()) {
    auto drivers = driverScanner.scan(driversDir, true, &drvStats);
    stdiolink::DriverCatalog driverCatalog;
    driverCatalog.replaceAll(drivers);
    qInfo("Drivers: %d updated, %d failed, %d skipped",
          drvStats.updated, drvStats.newlyFailed,
          drvStats.skippedFailed);
}
```

---

## 4. 文件清单

| 操作 | 文件 | 说明 |
|------|------|------|
| 新增 | `src/stdiolink_server/scanner/driver_manager_scanner.h` | DriverManagerScanner 头文件 |
| 新增 | `src/stdiolink_server/scanner/driver_manager_scanner.cpp` | DriverManagerScanner 实现 |
| 修改 | `src/stdiolink_server/main.cpp` | 集成 Driver 扫描步骤 |
| 修改 | `src/stdiolink_server/CMakeLists.txt` | 新增源文件 |

---

## 5. 验收标准

1. `drivers/` 目录不存在时不报错，跳过扫描
2. 有 `driver.meta.json` 的目录被正确加载
3. 无 `driver.meta.json` 时自动执行 `--export-meta` 导出
4. 导出成功后 `driver.meta.json` 被正确加载
5. 导出失败时目录被重命名为 `<dir>.failed`
6. `.failed` 后缀目录在后续扫描中被跳过
7. `refreshMeta=true` 时对已有 meta 的目录重新导出
8. 重新导出失败时保留旧 meta，不标记 `.failed`
9. `ScanStats` 各计数器准确反映扫描结果
10. 扫描结果可正确注入 `DriverCatalog`

---

## 6. 单元测试用例

### 6.1 基础扫描测试

```cpp
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "scanner/driver_manager_scanner.h"

using namespace stdiolink_server;

class DriverManagerScannerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmpDir.isValid());
        driversDir = tmpDir.path() + "/drivers";
        QDir().mkpath(driversDir);
    }

    QTemporaryDir tmpDir;
    QString driversDir;
};

TEST_F(DriverManagerScannerTest, EmptyDirectory) {
    DriverManagerScanner scanner;
    DriverManagerScanner::ScanStats stats;
    auto result = scanner.scan(driversDir, false, &stats);
    EXPECT_TRUE(result.isEmpty());
    EXPECT_EQ(stats.scanned, 0);
}

TEST_F(DriverManagerScannerTest, SkipFailedDirs) {
    QDir().mkpath(driversDir + "/broken.failed");
    DriverManagerScanner scanner;
    DriverManagerScanner::ScanStats stats;
    scanner.scan(driversDir, false, &stats);
    EXPECT_EQ(stats.skippedFailed, 1);
    EXPECT_EQ(stats.scanned, 0);
}

TEST_F(DriverManagerScannerTest, NonExistentDirectory) {
    DriverManagerScanner scanner;
    auto result = scanner.scan("/nonexistent/path", false);
    EXPECT_TRUE(result.isEmpty());
}
```

---

## 7. 依赖关系

### 7.1 前置依赖

| 依赖项 | 说明 |
|--------|------|
| 里程碑 34（Server 脚手架） | `main.cpp` 启动流程、`ServerConfig` |
| 核心库 `DriverCatalog` / 元数据类型 | `DriverConfig`、`DriverMeta`、`DriverCatalog::replaceAll()` |

### 7.2 后置影响

| 后续里程碑 | 依赖内容 |
|-----------|---------|
| 里程碑 38（HTTP API） | `POST /api/drivers/scan` 调用 `DriverManagerScanner::scan()` |
