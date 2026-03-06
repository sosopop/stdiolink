# Console 模式使用指南

Console 模式允许直接在命令行运行 Driver，用于调试、脚本集成和第三方调用。

## 模式检测

DriverCore 自动检测运行模式：

- **Stdio 模式**：stdin 是管道或重定向输入
- **Console 模式**：显式传入 `--cmd=...`，或通过 `--mode=console` 强制启用

## 框架参数

| 参数 | 说明 |
|------|------|
| `--help` | 显示帮助 |
| `--version` | 显示版本 |
| `--mode=console` | 强制 Console 模式 |
| `--mode=stdio` | 强制 Stdio 模式 |
| `--profile=oneshot|keepalive` | 控制进程生命周期 |
| `--export-meta` | 导出元数据 |
| `--export-doc=FORMAT` | 导出 Markdown/OpenAPI/HTML/TypeScript 文档 |

## 参数语法

### 路径语法

支持以下路径形式：

- `servers[0].host`
- `units[0].id`
- `tags[]`
- `labels["app.kubernetes.io/name"]`

### 值语义

- `Canonical`：命令行 token 使用 JSON 字面量表达值，例如 `--password="123456"`
- `Friendly`：console 解析默认可消费 Canonical 输出，并兼容常见 `true`、`1`、`3.14` 写法
- `String` 和 `Enum` 字段在 console 模式下会按元数据保留为字符串，不再把 `123456` 或 `1` 误判成 number

## 推荐写法

### 对象数组参数

```bash
stdio.drv.example --cmd=run --password="123456" --mode_code="1" --units[0].id=1 --units[0].size=10000
```

### 特殊键名

```bash
stdio.drv.example --cmd=run --labels["app.kubernetes.io/name"]="demo"
```

### 推荐：显式路径展开

```bash
--units[0].id=1 --units[0].size=10000
```

### 不推荐：依赖 shell 保留整段 JSON

```bash
--units=[{"id":1,"size":10000}]
```

### 迁移对照

| 旧写法 | 问题 | 新写法 |
|--------|------|--------|
| `--units=[{"id":1}]` | shell 转义脆弱 | `--units[0].id=1` |
| `--password=123456` | 旧实现会把纯数字字符串误判为 number | `--password="123456"` |
| `--a={"b":1} --a.c=2` | 容器字面量与子路径冲突 | 统一使用完整 JSON 或统一使用子路径 |

## 示例

### 查看帮助

```bash
stdio.drv.example --help
```

### 执行命令

```bash
stdio.drv.example --cmd=scan --fps=30 --duration=1.5
```

### 导出元数据

```bash
stdio.drv.example --export-meta > meta.json
```

### 导出文档

```bash
stdio.drv.example --export-doc=markdown > doc.md
```
