# 里程碑 13：CLI Help 与元数据导出

## 1. 目标

实现细粒度 CLI 帮助生成器和标准化元数据导出链路。

## 2. 对应需求

- **需求2**: 标准化元数据导出链路 (--export-meta)
- **需求8**: 细粒度 CLI 帮助生成器 (HelpGenerator)

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

## 5. 验收标准

1. `--version` 显示驱动名称、版本、vendor
2. `--help` 显示完整帮助（Usage、Options、Commands）
3. `--cmd=xxx --help` 显示命令参数详情和约束
4. `--export-meta` 输出标准 JSON 元数据
5. `--export-meta=file.json` 导出到文件

## 6. 依赖关系

- **前置**: M12 (双模式集成)
- **后续**: M14 (完整文档生成器)
