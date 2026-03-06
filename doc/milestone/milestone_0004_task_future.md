# 里程碑 4：Task 类（Future/Promise）

## 1. 目标

实现 Task 类，支持消息队列和阻塞等待，提供 Future/Promise 风格的异步调用接口。

## 2. 技术要点

### 2.1 Task 语义

- Task 是 `request()` 的返回值（句柄）
- 承载调用的完成/失败状态
- 存储 0..N 条 event 消息和最终 done/error 消息
- 支持连续取消息直到完成

### 2.2 核心方法

- `tryNext()`：非阻塞，立即返回
- `waitNext()`：阻塞等待新消息
- `isDone()`：是否已完成且队列取空
- `exitCode()`：最终状态码

### 2.3 状态管理

```cpp
struct TaskState {
    bool terminal = false;        // 是否已收到 done/error
    int exitCode = 0;             // 终态 code
    QString errorText;            // 错误文本
    QJsonValue finalPayload;      // 终态 payload
    std::deque<Message> queue;    // 待取消息队列
};
```

## 3. 实现步骤

### 3.1 定义 Task 类

```cpp
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

private:
    Driver* drv = nullptr;
    std::shared_ptr<TaskState> st;
};
```

### 3.2 实现 tryNext

```cpp
bool Task::tryNext(Message& out) {
    if (!st || st->queue.empty()) return false;
    out = std::move(st->queue.front());
    st->queue.pop_front();
    return true;
}
```

### 3.3 实现 waitNext

```cpp
bool Task::waitNext(Message& out, int timeoutMs) {
    // 1. 先尝试非阻塞获取
    if (tryNext(out)) return true;

    // 2. 检查是否已完成
    if (!st || !drv || isDone()) return false;

    // 3. 使用 QEventLoop 等待
    QEventLoop loop;
    QTimer timer;

    auto quitIfReady = [&] {
        if (drv) drv->pumpStdout();
        if (hasQueued() || isDone()) loop.quit();
    };

    // 连接信号
    QObject::connect(drv->process(), &QProcess::readyReadStandardOutput,
                     &loop, quitIfReady);
    QObject::connect(drv->process(),
                     qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                     &loop, quitIfReady);

    // 设置超时
    if (timeoutMs >= 0) {
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeoutMs);
    }

    loop.exec();
    return tryNext(out);
}
```

## 4. 验收标准

1. `tryNext()` 能非阻塞获取队列中的消息
2. `tryNext()` 队列为空时立即返回 false
3. `waitNext()` 能阻塞等待新消息
4. `waitNext()` 超时后返回 false
5. `waitNext()` 任务完成后返回 false
6. `isDone()` 正确反映任务完成状态
7. 能连续获取多个 event 直到 done/error

## 5. 单元测试用例

### 5.1 tryNext 测试

```cpp
TEST(Task, TryNext_Empty) {
    auto state = std::make_shared<TaskState>();
    Task t(nullptr, state);

    Message msg;
    EXPECT_FALSE(t.tryNext(msg));
}

TEST(Task, TryNext_HasMessage) {
    auto state = std::make_shared<TaskState>();
    state->queue.push_back(Message{"event", 0, QJsonObject{}});
    Task t(nullptr, state);

    Message msg;
    EXPECT_TRUE(t.tryNext(msg));
    EXPECT_EQ(msg.status, "event");
}

TEST(Task, TryNext_MultipleMessages) {
    auto state = std::make_shared<TaskState>();
    state->queue.push_back(Message{"event", 0, QJsonObject{{"n", 1}}});
    state->queue.push_back(Message{"event", 0, QJsonObject{{"n", 2}}});
    state->queue.push_back(Message{"done", 0, QJsonObject{}});
    Task t(nullptr, state);

    Message msg;
    EXPECT_TRUE(t.tryNext(msg));
    EXPECT_EQ(msg.payload.toObject()["n"].toInt(), 1);

    EXPECT_TRUE(t.tryNext(msg));
    EXPECT_EQ(msg.payload.toObject()["n"].toInt(), 2);

    EXPECT_TRUE(t.tryNext(msg));
    EXPECT_EQ(msg.status, "done");

    EXPECT_FALSE(t.tryNext(msg));
}
```

### 5.2 isDone 测试

```cpp
TEST(Task, IsDone_NotTerminal) {
    auto state = std::make_shared<TaskState>();
    state->terminal = false;
    Task t(nullptr, state);

    EXPECT_FALSE(t.isDone());
}

TEST(Task, IsDone_TerminalWithQueue) {
    auto state = std::make_shared<TaskState>();
    state->terminal = true;
    state->queue.push_back(Message{"done", 0, QJsonObject{}});
    Task t(nullptr, state);

    // terminal 但队列未取空
    EXPECT_FALSE(t.isDone());
}

TEST(Task, IsDone_TerminalEmptyQueue) {
    auto state = std::make_shared<TaskState>();
    state->terminal = true;
    Task t(nullptr, state);

    EXPECT_TRUE(t.isDone());
}
```

### 5.3 waitNext 测试

```cpp
TEST(Task, WaitNext_ImmediateReturn) {
    // 队列中已有消息，应立即返回
    auto state = std::make_shared<TaskState>();
    state->queue.push_back(Message{"event", 0, QJsonObject{}});

    Driver d;
    d.start("test_driver", {"--mode=stdio"});
    Task t(&d, state);

    Message msg;
    EXPECT_TRUE(t.waitNext(msg, 1000));
    EXPECT_EQ(msg.status, "event");

    d.terminate();
}

TEST(Task, WaitNext_Timeout) {
    auto state = std::make_shared<TaskState>();

    Driver d;
    d.start("test_driver", {"--mode=stdio", "--profile=keepalive"});
    Task t(&d, state);

    Message msg;
    // 没有发送请求，不会有响应，应超时
    EXPECT_FALSE(t.waitNext(msg, 100));

    d.terminate();
}

TEST(Task, WaitNext_AlreadyDone) {
    auto state = std::make_shared<TaskState>();
    state->terminal = true;
    Task t(nullptr, state);

    Message msg;
    EXPECT_FALSE(t.waitNext(msg, 1000));
}
```

### 5.4 集成测试

```cpp
TEST(Task, FullWorkflow) {
    Driver d;
    d.start("test_driver", {"--mode=stdio"});

    Task t = d.request("echo", QJsonObject{{"msg", "hello"}});

    Message msg;
    bool got = t.waitNext(msg, 5000);

    EXPECT_TRUE(got);
    EXPECT_EQ(msg.status, "done");
    EXPECT_TRUE(t.isDone());

    d.terminate();
}

TEST(Task, MultipleEvents) {
    Driver d;
    d.start("test_driver", {"--mode=stdio"});

    Task t = d.request("progress", QJsonObject{{"steps", 3}});

    std::vector<Message> messages;
    Message msg;
    while (t.waitNext(msg, 5000)) {
        messages.push_back(msg);
        if (msg.status == "done" || msg.status == "error") break;
    }

    // 应该有 3 个 event + 1 个 done
    EXPECT_EQ(messages.size(), 4);
    EXPECT_EQ(messages.back().status, "done");

    d.terminate();
}
```

### 5.5 exitCode 和 errorText 测试

```cpp
TEST(Task, ExitCode_Success) {
    auto state = std::make_shared<TaskState>();
    state->terminal = true;
    state->exitCode = 0;
    Task t(nullptr, state);

    EXPECT_EQ(t.exitCode(), 0);
}

TEST(Task, ExitCode_Error) {
    auto state = std::make_shared<TaskState>();
    state->terminal = true;
    state->exitCode = 1007;
    state->errorText = "invalid input";
    Task t(nullptr, state);

    EXPECT_EQ(t.exitCode(), 1007);
    EXPECT_EQ(t.errorText(), "invalid input");
}
```

## 6. 依赖关系

- **前置依赖**：
  - 里程碑 1（JSONL 协议基础）：Message 结构定义
  - 里程碑 3（Host 端 Driver 类）：Task 与 Driver 配合工作
- **后续依赖**：
  - 里程碑 5（多 Driver 并行）：waitAnyNext 基于 Task 实现
