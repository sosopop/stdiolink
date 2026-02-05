# 响应器接口

响应器接口用于 Driver 向 Host 输出响应消息。

## IResponder 接口

```cpp
namespace stdiolink {

class IResponder {
public:
    virtual ~IResponder() = default;

    // 输出中间事件
    virtual void event(int code, const QJsonValue& payload) = 0;

    // 输出带事件名的中间事件
    virtual void event(const QString& eventName,
                       int code,
                       const QJsonValue& data);

    // 输出成功完成
    virtual void done(int code, const QJsonValue& payload) = 0;

    // 输出错误
    virtual void error(int code, const QJsonValue& payload) = 0;
};

}
```

## 方法说明

### event

输出中间事件，可调用多次：

```cpp
void event(int code, const QJsonValue& payload);
void event(const QString& eventName, int code, const QJsonValue& data);
```

### done

输出成功完成，只能调用一次：

```cpp
void done(int code, const QJsonValue& payload);
```

### error

输出错误，只能调用一次：

```cpp
void error(int code, const QJsonValue& payload);
```

## 使用示例

### 简单响应

```cpp
void handle(const QString& cmd,
            const QJsonValue& data,
            IResponder& resp)
{
    QString msg = data.toObject()["msg"].toString();
    resp.done(0, QJsonObject{{"echo", msg}});
}
```

### 带进度的响应

```cpp
void handle(const QString& cmd,
            const QJsonValue& data,
            IResponder& resp)
{
    int steps = data.toObject()["steps"].toInt(3);
    for (int i = 1; i <= steps; ++i) {
        resp.event(0, QJsonObject{
            {"step", i},
            {"total", steps}
        });
    }
    resp.done(0, QJsonObject{});
}
```

### 错误响应

```cpp
void handle(const QString& cmd,
            const QJsonValue& data,
            IResponder& resp)
{
    if (cmd != "echo") {
        resp.error(404, QJsonObject{
            {"message", "unknown command"}
        });
        return;
    }
    // ...
}
```
