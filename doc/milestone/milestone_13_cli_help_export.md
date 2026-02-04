# 里程碑 13：CLI Help 与元数据导出

## 1. 目标

实现细粒度 CLI 帮助生成器和标准化元数据导出链路。

## 2. 对应需求

- **需求2**: 标准化元数据导出链路 (--export-meta)
- **需求8**: 细粒度 CLI 帮助生成器 (HelpGenerator)
- **边界说明**: M13 仅负责 CLI 参数解析与调用入口，文档生成逻辑由 M14 实现

## 3. 期望效果

```bash
# 版本信息
./driver.exe --version
# Meta Driver Demo v1.0.0

# 全局帮助
./driver.exe --help
# 显示所有命令列表

# 命令详情帮助
./driver.exe --cmd=scan --help
# 显示 scan 命令的参数、约束、示例

# 元数据导出
./driver.exe --export-meta
# 输出完整 JSON 元数据到 stdout

./driver.exe --export-meta=driver.meta.json
# 导出到指定文件

# 文档导出（与 M14 联动）
./driver.exe --export-doc=markdown
./driver.exe --export-doc=openapi=api.json
```

## 4. 技术设计

### 4.1 HelpGenerator 类

```cpp
class HelpGenerator {
public:
    static QString generateVersion(const meta::DriverMeta& meta);
    static QString generateHelp(const meta::DriverMeta& meta);
    static QString generateCommandHelp(const meta::CommandMeta& cmd);
    static QString formatParam(const meta::FieldMeta& field);
private:
    static QString formatConstraints(const meta::Constraints& c);
};
```

### 4.2 MetaExporter 类

```cpp
class MetaExporter {
public:
    static QByteArray exportJson(const meta::DriverMeta& meta, bool pretty = true);
    static bool exportToFile(const meta::DriverMeta& meta, const QString& path);
};
```

### 4.3 ConsoleArgs 扩展（新增参数）

```cpp
class ConsoleArgs {
public:
    // ...
    bool exportMeta = false;
    QString exportMetaPath;   // 可选
    QString exportDocFormat;  // markdown|openapi|html
    QString exportDocPath;    // 可选
};
```

解析规则：
- `--export-meta` 输出到 stdout
- `--export-meta=<path>` 输出到指定文件
- `--export-doc=<format>` 输出到 stdout
- `--export-doc=<format>=<path>` 输出到指定文件

短参数支持（系统参数）：
- `-h` 等价 `--help`
- `-v` 等价 `--version`
- `-m` 等价 `--mode`
- `-c` 等价 `--cmd`
- `-E` 等价 `--export-meta`
- `-D` 等价 `--export-doc`

默认行为：
- 不传参数且 stdin 为交互终端时，等价执行 `--help` 并退出。
- 若进入 stdio 模式，CLI 参数仍会被解析并参与数据合并（详见 M12）。

### 4.4 DriverCore 集成（Console 分支）

1. 若 `exportMeta` 为 true，调用 `MetaExporter::exportJson` 或 `exportToFile`，然后退出。
2. 若 `exportDocFormat` 非空，调用 M14 的 `DocGenerator`，然后退出。
3. `--help/--version` 与 `--export-*` 可共存时以 `--help/--version` 优先。

### 4.5 driver.meta.json 规范

`--export-meta` 输出即标准 `driver.meta.json` 格式，两者**完全一致**，用于离线分发与 Host 发现。

## 5. 验收标准

1. `--version` 显示驱动名称、版本、vendor
2. `--help` 显示完整帮助（Usage、Options、Commands）
3. `--cmd=xxx --help` 显示命令参数详情和约束
4. `--export-meta` 输出标准 JSON 元数据
5. `--export-meta=file.json` 导出到文件
6. `--export-doc` 仅验证参数解析与调用入口（可使用占位实现）
7. 导出失败时返回非 0 退出码并写入 stderr

## 6. 单元测试用例

### 6.1 测试文件：tests/test_help_generator.cpp

```cpp
#include <gtest/gtest.h>
#include "stdiolink/driver/help_generator.h"
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class HelpGeneratorTest : public ::testing::Test {
protected:
    DriverMeta createTestMeta() {
        DriverMeta meta;
        meta.info.id = "test.driver";
        meta.info.name = "Test Driver";
        meta.info.version = "1.0.0";
        meta.info.vendor = "stdiolink";

        CommandMeta cmd;
        cmd.name = "scan";
        cmd.description = "Scan operation";

        FieldMeta field;
        field.name = "fps";
        field.type = FieldType::Int;
        field.description = "Frame rate";
        field.constraints.min = 1;
        field.constraints.max = 60;
        cmd.params.append(field);

        meta.commands.append(cmd);
        return meta;
    }
};

// 测试版本信息生成
TEST_F(HelpGeneratorTest, GenerateVersion) {
    auto meta = createTestMeta();
    QString version = HelpGenerator::generateVersion(meta);
    EXPECT_TRUE(version.contains("Test Driver"));
    EXPECT_TRUE(version.contains("1.0.0"));
}

// 测试全局帮助生成
TEST_F(HelpGeneratorTest, GenerateHelp) {
    auto meta = createTestMeta();
    QString help = HelpGenerator::generateHelp(meta);
    EXPECT_TRUE(help.contains("Usage:"));
    EXPECT_TRUE(help.contains("Commands:"));
    EXPECT_TRUE(help.contains("scan"));
}

// 测试命令详情帮助
TEST_F(HelpGeneratorTest, GenerateCommandHelp) {
    auto meta = createTestMeta();
    QString help = HelpGenerator::generateCommandHelp(meta.commands[0]);
    EXPECT_TRUE(help.contains("fps"));
}

// 测试约束格式化
TEST_F(HelpGeneratorTest, FormatConstraints) {
    FieldMeta field;
    field.constraints.min = 0;
    field.constraints.max = 100;
    QString formatted = HelpGenerator::formatParam(field);
    EXPECT_TRUE(formatted.contains("0") && formatted.contains("100"));
}
```

### 6.2 测试文件：tests/test_meta_exporter.cpp

```cpp
#include <gtest/gtest.h>
#include <QDir>
#include <QFile>
#include "stdiolink/driver/meta_exporter.h"

using namespace stdiolink;

class MetaExporterTest : public ::testing::Test {};

// 测试 JSON 导出
TEST_F(MetaExporterTest, ExportJson) {
    meta::DriverMeta meta;
    meta.info.id = "test";
    meta.info.version = "1.0.0";

    QByteArray json = MetaExporter::exportJson(meta);
    EXPECT_FALSE(json.isEmpty());
    EXPECT_TRUE(json.contains("test"));
}

// 测试 Pretty 格式
TEST_F(MetaExporterTest, ExportJsonPretty) {
    meta::DriverMeta meta;
    meta.info.id = "test";

    QByteArray pretty = MetaExporter::exportJson(meta, true);
    QByteArray compact = MetaExporter::exportJson(meta, false);
    EXPECT_GT(pretty.size(), compact.size());
}

// 测试文件导出
TEST_F(MetaExporterTest, ExportToFile) {
    meta::DriverMeta meta;
    meta.info.id = "test";

    QString path = QDir::temp().filePath("test_meta.json");
    EXPECT_TRUE(MetaExporter::exportToFile(meta, path));
    EXPECT_TRUE(QFile::exists(path));
    QFile::remove(path);
}
```

### 6.3 ConsoleArgs 扩展测试

```cpp
// 测试 --export-meta 解析
TEST(ConsoleArgs, ParseExportMeta) {
    ConsoleArgs args;
    char* argv[] = {const_cast<char*>("prog"), const_cast<char*>("--export-meta")};
    EXPECT_TRUE(args.parse(2, argv));
    EXPECT_TRUE(args.exportMeta);
}

// 测试 --export-meta=path 解析
TEST(ConsoleArgs, ParseExportMetaWithPath) {
    ConsoleArgs args;
    char* argv[] = {const_cast<char*>("prog"), const_cast<char*>("--export-meta=out.json")};
    EXPECT_TRUE(args.parse(2, argv));
    EXPECT_TRUE(args.exportMeta);
    EXPECT_EQ(args.exportMetaPath, "out.json");
}

// 测试 --export-doc 解析
TEST(ConsoleArgs, ParseExportDoc) {
    ConsoleArgs args;
    char* argv[] = {const_cast<char*>("prog"), const_cast<char*>("--export-doc=markdown")};
    EXPECT_TRUE(args.parse(2, argv));
    EXPECT_EQ(args.exportDocFormat, "markdown");
}
```

## 7. 依赖关系

- **前置**: M12 (双模式集成)
- **后续**: M14 (完整文档生成器)
