# 里程碑 2：Driver 端核心实现

## 1. 目标

实现 Driver 端的 stdin/stdout 通信，支持 oneshot 和 keepalive 两种运行模式。

## 2. 技术要点

### 2.1 通信通道

- **stdin**：读取请求（JSONL 格式）
- **stdout**：输出响应（JSONL 格式，header + payload）
- **stderr**：仅用于人类可读日志（不参与协议解析）

### 2.2 运行模式

- **oneshot**：处理 1 次请求后自动退出
- **keepalive**：处理完请求后继续等待下一条请求

### 2.3 响应输出规则

- 一次调用可输出 0..N 个 `event`
- 最终必须输出 1 个 `done` 或 `error`
- 每输出一行后必须 `flush()`，避免缓冲导致 Host 卡住

## 3. 实现步骤

### 3.1 定义命令处理接口

```cpp
// 命令处理器接口
class ICommandHandler {
public:
    virtual ~ICommandHandler() = default;

    // 处理命令，通过 responder 输出响应
    virtual void handle(const QString& cmd,
                       const QJsonValue& data,
                       IResponder& responder) = 0;
};

// 响应输出接口
class IResponder {
public:
    virtual ~IResponder() = default;

    virtual void event(int code, const QJsonValue& payload) = 0;
    virtual void done(int code, const QJsonValue& payload) = 0;
    virtual void error(int code, const QJsonValue& payload) = 0;
};
```

### 3.2 实现 StdIO 响应器

```cpp
class StdioResponder : public IResponder {
public:
    void event(int code, const QJsonValue& payload) override;
    void done(int code, const QJsonValue& payload) override;
    void error(int code, const QJsonValue& payload) override;

private:
    void writeResponse(const QString& status, int code, const QJsonValue& payload);
};
```

### 3.3 实现 Driver 主循环

```cpp
class DriverCore {
public:
    enum class Profile { OneShot, KeepAlive };

    void setProfile(Profile p);
    void setHandler(ICommandHandler* handler);

    // 主循环：读取 stdin，处理请求，输出响应
    int run();

private:
    Profile profile = Profile::OneShot;
    ICommandHandler* handler = nullptr;
    JsonlParser parser;

    bool processOneLine(const QByteArray& line);
};
```

### 3.4 实现 stdin 读取

```cpp
// 阻塞读取 stdin
QByteArray readStdin();

// 非阻塞检查 stdin 是否有数据（可选，用于 keepalive 模式）
bool hasStdinData();
```

## 4. 验收标准

1. Driver 能正确从 stdin 读取 JSONL 请求
2. Driver 能正确解析请求中的 cmd 和 data
3. Driver 能通过 stdout 输出正确格式的响应（header + payload）
4. stderr 日志不影响协议通信
5. oneshot 模式：输出 done/error 后进程退出
6. keepalive 模式：输出 done/error 后继续等待下一请求
7. 每次输出后正确 flush，Host 能及时收到数据

## 5. 单元测试用例

### 5.1 响应输出测试

```cpp
TEST(StdioResponder, Event) {
    // 捕获 stdout 输出
    StdioResponder responder;
    responder.event(0, QJsonObject{{"progress", 0.5}});

    // 验证输出两行：
    // {"status":"event","code":0}
    // {"progress":0.5}
}

TEST(StdioResponder, Done) {
    StdioResponder responder;
    responder.done(0, QJsonObject{{"result", 42}});

    // 验证输出 done 状态
}

TEST(StdioResponder, Error) {
    StdioResponder responder;
    responder.error(1007, QJsonObject{{"message", "invalid input"}});

    // 验证输出 error 状态和错误码
}
```

### 5.2 请求处理测试

```cpp
class MockHandler : public ICommandHandler {
public:
    void handle(const QString& cmd, const QJsonValue& data, IResponder& r) override {
        if (cmd == "echo") {
            r.done(0, data);
        } else {
            r.error(404, QJsonObject{{"message", "unknown command"}});
        }
    }
};

TEST(DriverCore, ProcessValidRequest) {
    DriverCore driver;
    MockHandler handler;
    driver.setHandler(&handler);

    // 模拟输入 {"cmd":"echo","data":{"msg":"hello"}}
    // 验证输出 done + payload
}

TEST(DriverCore, ProcessUnknownCommand) {
    DriverCore driver;
    MockHandler handler;
    driver.setHandler(&handler);

    // 模拟输入 {"cmd":"unknown"}
    // 验证输出 error
}
```

### 5.3 模式测试

```cpp
TEST(DriverCore, OneShotMode) {
    DriverCore driver;
    driver.setProfile(DriverCore::Profile::OneShot);

    // 模拟输入一个请求
    // 验证处理完成后 run() 返回
}

TEST(DriverCore, KeepAliveMode) {
    DriverCore driver;
    driver.setProfile(DriverCore::Profile::KeepAlive);

    // 模拟输入多个请求
    // 验证能连续处理
}
```

### 5.4 流式输入测试

```cpp
TEST(DriverCore, PartialInput) {
    DriverCore driver;

    // 模拟分多次输入一个完整请求
    // 验证能正确拼接并处理
}

TEST(DriverCore, MultipleRequestsInOneRead) {
    DriverCore driver;

    // 模拟一次输入包含多个请求
    // 验证能逐个处理
}
```

### 5.5 错误处理测试

```cpp
TEST(DriverCore, InvalidJson) {
    DriverCore driver;

    // 模拟输入非法 JSON
    // 验证输出 error 响应
}

TEST(DriverCore, MissingCmd) {
    DriverCore driver;

    // 模拟输入缺少 cmd 字段
    // 验证输出 error 响应
}
```

## 6. 依赖关系

- **前置依赖**：里程碑 1（JSONL 协议基础）
- **后续依赖**：
  - 里程碑 3（Host 端 Driver 类）需要与本里程碑的 Driver 进行通信
  - 里程碑 6（Console 模式）复用本里程碑的命令处理逻辑
