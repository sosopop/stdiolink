# Console 模式使用指南

Console 模式允许直接在命令行运行 Driver，用于调试和独立使用。

## 模式检测

DriverCore 自动检测运行模式：

- **Stdio 模式**：stdin 是管道时
- **Console 模式**：stdin 是终端时

## 命令行参数

### 框架参数

| 参数 | 说明 |
|------|------|
| `--help` | 显示帮助 |
| `--version` | 显示版本 |
| `--mode=console` | 强制 Console 模式 |
| `--mode=stdio` | 强制 Stdio 模式 |
| `--export-meta` | 导出元数据 |
| `--export-doc=FORMAT` | 导出文档 |

### 命令参数

```bash
driver.exe <command> [--param=value ...]
```

## 使用示例

### 查看帮助

```bash
./meta_driver.exe --help
```

### 执行命令

```bash
./meta_driver.exe scan --fps=30 --duration=1.5
```

### 导出元数据

```bash
./meta_driver.exe --export-meta > meta.json
```

### 导出文档

```bash
./meta_driver.exe --export-doc=markdown > doc.md
```
