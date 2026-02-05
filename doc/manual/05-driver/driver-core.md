# DriverCore 核心类

`DriverCore` 是 Driver 端的核心类，负责处理 stdin/stdout 通信和命令分发。

## 类定义

```cpp
namespace stdiolink {

class DriverCore {
public:
    enum class Profile {
        OneShot,   // 处理一次请求后退出
        KeepAlive  // 持续处理请求
    };

    enum class RunMode {
        Auto,    // 自动检测
        Stdio,   // Stdio 模式
        Console  // Console 模式
    };

    DriverCore() = default;

    void setProfile(Profile p);
    void setHandler(ICommandHandler* h);
    void setMetaHandler(IMetaCommandHandler* h);

    int run(int argc, char* argv[]);
    int run();
};

}
```

## 方法说明

### setProfile

设置运行配置：

```cpp
void setProfile(Profile p);
```

| Profile | 说明 |
|---------|------|
| OneShot | 处理一次请求后退出 |
| KeepAlive | 持续处理请求直到进程终止 |

### setHandler

设置命令处理器：

```cpp
void setHandler(ICommandHandler* h);
```

### setMetaHandler

设置支持元数据的处理器：

```cpp
void setMetaHandler(IMetaCommandHandler* h);
```

设置后将自动响应 `meta.*` 命令。

### run

启动 Driver：

```cpp
int run(int argc, char* argv[]);  // 推荐，支持双模式
int run();                         // 纯 Stdio 模式
```

## 使用示例

```cpp
#include <QCoreApplication>
#include "stdiolink/driver/driver_core.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    MyHandler handler;
    stdiolink::DriverCore core;
    core.setHandler(&handler);
    core.setProfile(stdiolink::DriverCore::Profile::KeepAlive);

    return core.run(argc, argv);
}
```
