# Task 句柄

`Task` 类是请求的句柄，用于获取响应消息。

## 类定义

```cpp
namespace stdiolink {

class Task {
public:
    Task() = default;
    Task(Driver* owner, std::shared_ptr<TaskState> state);

    bool isValid() const;
    bool isDone() const;
    int exitCode() const;
    QString errorText() const;
    QJsonValue finalPayload() const;

    bool tryNext(Message& out);
    bool waitNext(Message& out, int timeoutMs = -1);
    bool hasQueued() const;

    Driver* owner() const;
};

}
```

## 方法说明

### isValid

检查 Task 是否有效：

```cpp
bool isValid() const;
```

### isDone

检查请求是否已完成：

```cpp
bool isDone() const;
```

### waitNext

阻塞等待下一条消息：

```cpp
bool waitNext(Message& out, int timeoutMs = -1);
```

| 参数 | 类型 | 说明 |
|------|------|------|
| out | Message& | 输出消息 |
| timeoutMs | int | 超时毫秒，-1 表示无限等待 |

**返回值：** 成功获取消息返回 `true`。

### tryNext

非阻塞尝试获取消息：

```cpp
bool tryNext(Message& out);
```

## 使用示例

```cpp
Task t = driver.request("scan", params);

Message msg;
while (t.waitNext(msg, 5000)) {
    if (msg.status == "event") {
        qDebug() << "Progress:" << msg.payload;
    } else if (msg.status == "done") {
        qDebug() << "Result:" << msg.payload;
        break;
    }
}
```
