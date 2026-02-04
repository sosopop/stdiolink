# stdiolink 项目指南

## 项目概述

stdiolink 是一个基于 Qt 的跨平台 IPC 框架，使用 JSONL 协议通过 stdin/stdout 进行进程间通信。

## 构建命令

```bash
# 构建
ninja -C build_ninja

# 运行测试
./build_ninja/src/tests/stdiolink_tests.exe

# 运行单个测试
./build_ninja/src/tests/stdiolink_tests.exe --gtest_filter=TestName.*

# clang-tidy 检查（排除自动生成文件）
python ./tools/run-clang-tidy.py -p build_ninja -j 8 -quiet -config-file .clang-tidy
```

## 代码规范

### 必须使用 Qt 类

- 文件 I/O：使用 `QFile`，不用 `fopen/fread/fwrite`
- 文本流：使用 `QTextStream`，不用 `fgets/fprintf`
- 字符串：使用 `QString`/`QByteArray`
- JSON：使用 `QJsonDocument`/`QJsonObject`/`QJsonArray`

### 命名规范

- 命名空间：`lower_case`（如 `stdiolink`）
- 类名：`CamelCase`（如 `DriverCore`）
- 函数/方法：`camelBack`（如 `parseRequest`）
- 成员变量：`m_` 前缀 + `camelBack`（如 `m_driverPath`）
- 局部变量：`camelBack`（如 `driverPath`）
- 全局常量：`CamelCase`（如 `TestDriver`）
- 枚举常量：`CamelCase`（如 `StatusDone`）

### clang-tidy 配置

项目使用 `.clang-tidy` 配置文件，已禁用以下不适合本项目的检查：

| 检查项 | 禁用原因 |
|--------|----------|
| `misc-include-cleaner` | Qt 头文件依赖复杂，误报多 |
| `modernize-use-nodiscard` | 可选属性，不强制要求 |
| `cppcoreguidelines-special-member-functions` | 接口类不需要定义所有特殊成员函数 |
| `readability-function-size` | 部分函数确实需要较长实现 |
| `cppcoreguidelines-pro-type-const-cast` | 测试代码需要 const_cast |

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
├── stdiolink/         # 基础库
│   ├── protocol/      # JSONL 协议层
│   ├── driver/        # Driver 端（被调用方）
│   ├── host/          # Host 端（调用方）
│   └── console/       # Console 模式
├── tests/             # 单元测试 (78个测试用例)
└── demo/              # 示例程序
    ├── host_demo/     # Host 端示例
    ├── echo_driver/   # Echo Driver
    └── progress_driver/  # Progress Driver
tools/
└── run-clang-tidy.py  # clang-tidy 批量检查工具
```

## 里程碑进度

- [x] 里程碑 1：JSONL 协议基础
- [x] 里程碑 2：Driver 端核心实现
- [x] 里程碑 3：Host 端 Driver 类
- [x] 里程碑 4：Task 类（Future/Promise）
- [x] 里程碑 5：多 Driver 并行
- [x] 里程碑 6：Console 模式

## 核心 API

### Host 端使用示例

```cpp
#include "stdiolink/host/driver.h"
#include "stdiolink/host/wait_any.h"

// 启动 Driver 进程
Driver d;
d.start("path/to/driver.exe");

// 发送请求
Task t = d.request("echo", QJsonObject{{"msg", "hello"}});

// 等待响应
Message msg;
while (t.waitNext(msg, 5000)) {
    if (msg.status == "done") break;
}

d.terminate();
```

### 多 Driver 并发

```cpp
Driver d1, d2;
d1.start("driver.exe");
d2.start("driver.exe");

QVector<Task> tasks;
tasks << d1.request("cmd1", data1);
tasks << d2.request("cmd2", data2);

AnyItem item;
while (waitAnyNext(tasks, item, 5000)) {
    // item.taskIndex 表示来源
    // item.msg 是消息内容
}
```
