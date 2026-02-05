# Driver 类

`Driver` 类用于管理一个 Driver 进程实例。

## 类定义

```cpp
namespace stdiolink {

class Driver {
public:
    Driver() = default;
    ~Driver();

    // 禁止拷贝和移动
    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;
    Driver(Driver&&) = delete;
    Driver& operator=(Driver&&) = delete;

    bool start(const QString& program,
               const QStringList& args = {});
    void terminate();

    Task request(const QString& cmd,
                 const QJsonObject& data = {});
    void pumpStdout();

    QProcess* process();
    bool isRunning() const;
    bool hasQueued() const;
    bool isCurrentTerminal() const;

    // 元数据查询
    const meta::DriverMeta* queryMeta(int timeoutMs = 5000);
    bool hasMeta() const;
    void refreshMeta();
};

}
```

## 方法说明

### start

启动 Driver 进程：

```cpp
bool start(const QString& program, const QStringList& args = {});
```

| 参数 | 类型 | 说明 |
|------|------|------|
| program | QString | 可执行文件路径 |
| args | QStringList | 命令行参数 |

**返回值：** 启动成功返回 `true`。

### terminate

终止 Driver 进程：

```cpp
void terminate();
```

### request

发送请求：

```cpp
Task request(const QString& cmd, const QJsonObject& data = {});
```

**返回值：** 返回 `Task` 句柄用于获取响应。

### queryMeta

查询 Driver 元数据：

```cpp
const meta::DriverMeta* queryMeta(int timeoutMs = 5000);
```

**返回值：** 成功返回元数据指针，失败返回 `nullptr`。

## 使用示例

```cpp
Driver d;
if (!d.start("./echo_driver.exe")) {
    return 1;
}

Task t = d.request("echo", QJsonObject{{"msg", "hello"}});

Message msg;
if (t.waitNext(msg, 5000)) {
    qDebug() << msg.payload;
}

d.terminate();
```
