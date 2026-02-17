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
    const TaskState* stateId() const;
    void forceTerminal(int code, const QString& error);

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

### stateId

获取内部状态标识（用于 `waitAnyNext` 等底层调度）：

```cpp
const TaskState* stateId() const;
```

> 说明：该接口主要用于框架内部或高级调度场景，普通业务代码通常不需要调用。

### forceTerminal

强制将 Task 标记为终止状态（用于超时或异常处理）：

```cpp
void forceTerminal(int code, const QString& error);
```

| 参数 | 类型 | 说明 |
|------|------|------|
| code | int | 错误码 |
| error | QString | 错误描述 |

> 说明：调用后 Task 会进入终止态；建议仅在明确需要“人工兜底收敛状态”的场景使用。

### owner

获取创建此 Task 的 Driver 实例：

```cpp
Driver* owner() const;
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
