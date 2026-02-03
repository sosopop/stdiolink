# stdiolink 项目指南

## 项目概述

stdiolink 是一个基于 Qt 的跨平台 IPC 框架，使用 JSONL 协议通过 stdin/stdout 进行进程间通信。

## 构建命令

```bash
# 构建
ninja -C build_ninja

# 运行测试
./build_ninja/stdiolink_tests.exe

# 运行单个测试
./build_ninja/stdiolink_tests.exe --gtest_filter=TestName.*
```

## 代码规范

### 必须使用 Qt 类

- 文件 I/O：使用 `QFile`，不用 `fopen/fread/fwrite`
- 文本流：使用 `QTextStream`，不用 `fgets/fprintf`
- 字符串：使用 `QString`/`QByteArray`
- JSON：使用 `QJsonDocument`/`QJsonObject`/`QJsonArray`

### stdin/stdout 读写示例

```cpp
// 读取 stdin
QFile input;
input.open(stdin, QIODevice::ReadOnly);
QTextStream in(&input);
QString line = in.readLine();

// 写入 stdout
QFile output;
output.open(stdout, QIODevice::WriteOnly);
output.write(data);
output.flush();
```

## 已知问题

### Windows 管道与 fread 不兼容

在 Windows 上，`fread()` 与 QProcess 管道配合时会阻塞等待填满缓冲区。必须使用 `QTextStream::readLine()` 或 `fgets()` 逐行读取。

## 项目结构

```
src/
├── protocol/          # JSONL 协议层
│   ├── jsonl_types.h
│   ├── jsonl_serializer.h/cpp
│   ├── jsonl_parser.h/cpp
│   └── jsonl_stream_parser.cpp
├── driver/            # Driver 端（被调用方）
│   ├── iresponder.h
│   ├── icommand_handler.h
│   ├── stdio_responder.h/cpp
│   └── driver_core.h/cpp
├── host/              # Host 端（调用方）
│   ├── task_state.h
│   ├── task.h/cpp
│   └── driver.h/cpp
└── test_driver_main.cpp

tests/                 # 单元测试
```

## 里程碑进度

- [x] 里程碑 1：JSONL 协议基础
- [x] 里程碑 2：Driver 端核心实现
- [x] 里程碑 3：Host 端 Driver 类
- [ ] 里程碑 4：Task 类（Future/Promise）
- [ ] 里程碑 5：多 Driver 并行
- [ ] 里程碑 6：Console 模式
