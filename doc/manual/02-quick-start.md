# 快速入门

本章将帮助你快速上手 stdiolink，创建第一个 Host-Driver 应用。

## 环境要求

- Qt 6.2 或更高版本
- CMake 3.16 或更高版本
- C++17 兼容的编译器

## 构建项目

```bash
# Windows (使用 Ninja)
build_ninja.bat

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
    core.setProfile(stdiolink::DriverCore::Profile::KeepAlive);

    return core.run();
}
```

### 3. CMakeLists.txt

```cmake
add_executable(echo_driver main.cpp)
target_link_libraries(echo_driver PRIVATE stdiolink)
```
