# 命令处理器接口

命令处理器是 Driver 的业务逻辑入口，负责处理具体的命令请求。

## ICommandHandler

基础命令处理器接口：

```cpp
namespace stdiolink {

class ICommandHandler {
public:
    virtual ~ICommandHandler() = default;

    virtual void handle(const QString& cmd,
                        const QJsonValue& data,
                        IResponder& responder) = 0;
};

}
```

### handle 方法

| 参数 | 类型 | 说明 |
|------|------|------|
| cmd | QString | 命令名 |
| data | QJsonValue | 命令参数 |
| responder | IResponder& | 响应输出接口 |

### 示例

```cpp
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
            resp.error(404, QJsonObject{{"message", "unknown"}});
        }
    }
};
```

## IMetaCommandHandler

支持元数据的命令处理器接口，继承自 `ICommandHandler`：

```cpp
namespace stdiolink {

class IMetaCommandHandler : public ICommandHandler {
public:
    virtual const meta::DriverMeta& driverMeta() const = 0;
    virtual bool autoValidateParams() const { return true; }
};

}
```

### 方法说明

| 方法 | 说明 |
|------|------|
| `driverMeta()` | 返回 Driver 的元数据描述 |
| `autoValidateParams()` | 是否自动验证参数（默认 true） |

### 示例

```cpp
class MyHandler : public stdiolink::IMetaCommandHandler {
public:
    const meta::DriverMeta& driverMeta() const override {
        return m_meta;
    }

    void handle(const QString& cmd,
                const QJsonValue& data,
                stdiolink::IResponder& resp) override
    {
        // 参数已自动验证
        if (cmd == "scan") {
            handleScan(data.toObject(), resp);
        }
    }

private:
    meta::DriverMeta m_meta;
};
```
