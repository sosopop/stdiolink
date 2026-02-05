# stdiolink 项目指南

基于 Qt 的跨平台 IPC 框架，使用 JSONL 协议通过 stdin/stdout 通信。

## 构建

```bash
build_ninja.bat                              # 首次构建
cmake --build build_ninja --parallel 8       # 增量构建
./build_ninja/bin/stdiolink_tests.exe        # 运行测试
```

## 代码规范

### Qt 类优先

- 文件 I/O：`QFile`（非 fopen/fread）
- 字符串：`QString`/`QByteArray`
- JSON：`QJsonDocument`/`QJsonObject`

### 命名

- 类：`CamelCase`，函数：`camelBack`，成员：`m_camelBack`

### 输出规范

| 内容 | 输出目标 |
|------|---------|
| JSONL 响应、导出数据 | stdout |
| 帮助、版本、错误信息 | stderr |
| Qt 日志 (qDebug等) | stderr 或 `--log=<file>` |

### Windows 注意

`fread()` 与 QProcess 管道不兼容，必须用 `QTextStream::readLine()` 逐行读取。

## 项目结构

```
src/stdiolink/
├── protocol/   # JSONL 协议层
├── driver/     # Driver 端
├── host/       # Host 端
└── console/    # Console 模式
```
