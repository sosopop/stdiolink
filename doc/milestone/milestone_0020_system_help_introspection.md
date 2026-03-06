# 里程碑 20：系统命令说明补齐与自解释

## 1. 目标

补齐所有系统命令（框架参数）在 `--help` 中的说明，并保证 `--cmd=<command> --help` 能对命令参数进行自解释（类型/约束/默认值/示例）。形成“解析逻辑与帮助说明”一致的单一来源，避免遗漏与文档漂移。

## 2. 对应需求

- **需求9**：系统命令说明补齐与自解释
- 依赖里程碑：M12、M13

## 3. 期望效果

```bash
# 全局帮助必须包含所有系统参数
./driver.exe --help

# 命令详情帮助（参数类型/约束/默认值/示例）
./driver.exe --cmd=scan --help

# 未知命令时的错误提示
./driver.exe --cmd=unknown --help
```

帮助输出至少包含：
- `--help/-h`、`--version/-v`
- `--mode/-m`（stdio|console）
- `--profile`（oneshot|keepalive）
- `--cmd/-c`
- `--export-meta/-E`（可选路径）
- `--export-doc/-D`（markdown|openapi|html，可选路径）

## 4. 技术设计

### 4.1 系统命令元数据（单一来源）

新增系统命令元数据结构，供参数解析与帮助生成共享：

```cpp
struct SystemOptionMeta {
    QString longName;      // e.g. "export-doc"
    QString shortName;     // e.g. "D"
    QString valueHint;     // e.g. "<fmt>", "[=path]"
    QString description;   // 人类可读说明
    QStringList choices;   // 可选值（如 format）
    QString defaultValue;  // 默认值（若有）
};

class SystemOptionRegistry {
public:
    static QVector<SystemOptionMeta> list();
    static const SystemOptionMeta* findLong(const QString& name);
    static const SystemOptionMeta* findShort(const QString& name);
};
```

要求：
- `ConsoleArgs::isFrameworkArg/parseShortArg` 必须通过 `SystemOptionRegistry` 识别系统参数。
- `HelpGenerator` 不再硬编码系统参数列表，改为从 registry 生成 `Options`。

### 4.2 HelpGenerator 输出增强

- `generateHelp()` 输出系统参数说明与命令列表。
- `generateCommandHelp()` 输出参数的：
  - 类型、必填
  - 约束（范围、长度、枚举、pattern）
  - 默认值
  - 示例（若命令参数或命令级别 `examples` 存在）

### 4.3 DriverCore 统一帮助入口

`DriverCore::printHelp()` 必须使用 `HelpGenerator::generateHelp()` 输出，以确保系统参数说明完整覆盖且与解析一致。

## 5. 验收标准

1. `--help` 输出包含所有系统参数及短参数说明。
2. `--help` 输出包含 `--export-meta/--export-doc` 的格式与路径约定。
3. `--cmd=<command> --help` 输出完整参数自解释（类型/约束/默认值/示例）。
4. `--cmd=<unknown> --help` 返回非 0 并提示命令不存在。
5. `ConsoleArgs` 与 `HelpGenerator` 使用同一套系统参数来源，避免遗漏。

## 6. 单元测试用例

### 6.1 测试文件：tests/test_help_generator.cpp

- `generateHelp` 必须包含所有系统参数（含 `--export-*`、`--profile`）。
- `generateCommandHelp` 必须包含参数类型、约束与默认值。

### 6.2 测试文件：tests/test_console.cpp

- `--help` 输出包含系统参数说明。
- `--cmd=unknown --help` 返回非 0 且输出错误。
