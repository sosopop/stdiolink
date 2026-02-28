# 快速入门

本章将帮助你快速上手 stdiolink，创建第一个 Host-Driver 应用。

## 环境要求

- Qt 6.6 或更高版本
- CMake 3.21 或更高版本
- C++17 兼容的编译器
- vcpkg（依赖管理）

## 构建项目

```bash
# Windows (使用 Ninja)
build.bat

# 或使用 CMake
cmake -B build -G Ninja
cmake --build build --parallel 8
```

## 创建第一个 Driver

Driver 是执行具体任务的工作进程。下面创建一个简单的 Echo Driver。

### 1. 实现命令处理器

```cpp
// echo_handler.h
#include "stdiolink/driver/icommand_handler.h"

class EchoHandler : public stdiolink::ICommandHandler {
public:
    void handle(const QString& cmd,
                const QJsonValue& data,
                stdiolink::IResponder& resp) override
    {
        if (cmd == "echo") {
            QString msg = data.toObject()["msg"].toString();
            resp.done(0, QJsonObject{{"echo", msg}});
        } else {
            resp.error(404, QJsonObject{{"message", "unknown command"}});
        }
    }
};
```

### 2. 创建 main 函数

```cpp
// main.cpp
#include <QCoreApplication>
#include "stdiolink/driver/driver_core.h"
#include "echo_handler.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    EchoHandler handler;
    stdiolink::DriverCore core;
    core.setHandler(&handler);

    return core.run(argc, argv);
}
```

### 3. CMakeLists.txt

```cmake
add_executable(echo_driver main.cpp)
target_link_libraries(echo_driver PRIVATE stdiolink)
```

> 提示：如果该 Driver 需要被 `stdiolink_server` 自动扫描，建议将可执行文件名设为 `stdio.drv.` 前缀（例如 `stdio.drv.echo`，可通过 CMake `OUTPUT_NAME` 设置）。

## 创建 Host 程序

Host 是主控进程，负责启动 Driver 并与之通信。

```cpp
#include <QCoreApplication>
#include <QDebug>
#include "stdiolink/host/driver.h"

using namespace stdiolink;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    // 启动 Driver 进程
    Driver d;
    if (!d.start("./echo_driver.exe")) {
        qDebug() << "Failed to start driver";
        return 1;
    }

    // 发送请求
    Task t = d.request("echo", QJsonObject{{"msg", "Hello!"}});

    // 等待响应
    Message msg;
    if (t.waitNext(msg, 5000)) {
        qDebug() << "Response:" << msg.payload;
    }

    // 关闭 Driver
    d.terminate();
    return 0;
}
```

## 测试运行

### 命令行测试 Driver

Driver 支持 Console 模式，可以直接在命令行测试：

```bash
# 查看帮助
./echo_driver.exe --help

# 执行命令
./echo_driver.exe echo --msg="Hello"
```

### 运行 Host 程序

```bash
./host_demo.exe
# 输出: Response: {"echo":"Hello!"}
```

## 下一步

- [架构概述](03-architecture.md) - 了解系统架构
- [Driver 端开发](05-driver/README.md) - 深入学习 Driver 开发
- [Host 端开发](06-host/README.md) - 深入学习 Host 开发
