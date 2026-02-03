# 里程碑 3：Host 端 Driver 类

## 1. 目标

实现 Host 端的 Driver 类，通过 QProcess 启动 Driver 进程并完成请求/响应通信。

## 2. 技术要点

### 2.1 进程管理

- 使用 QProcess 启动 Driver 进程
- 支持 oneshot 和 keepalive 两种启动模式
- 正确处理进程生命周期（启动、运行、终止）

### 2.2 通信机制

- 通过 stdin 写入请求
- 通过 stdout 读取响应
- stderr 用于日志（可选转发）

### 2.3 核心约束

- **同一个 Driver 同一时刻只允许 1 个 in-flight 请求**
- 协议不需要 id，天然"一来一回"
- 想并行需要启动多个 Driver 进程

## 3. 实现步骤

### 3.1 定义 Driver 类

```cpp
class Driver {
public:
    Driver() = default;
    ~Driver();

    // 启动进程
    bool start(const QString& program, const QStringList& args = {});

    // 终止进程
    void terminate();

    // 发起请求，返回 Task 句柄
    Task request(const QString& cmd, const QJsonObject& data = {});

    // 解析 stdout，推入当前 Task 队列
    void pumpStdout();

    // 获取 QProcess 指针（用于信号连接）
    QProcess* process();

    // 状态查询
    bool isRunning() const;
    bool hasQueued() const;
    bool isCurrentTerminal() const;

private:
    QProcess proc;
    QByteArray buf;

    bool waitingHeader = true;
    FrameHeader hdr;
    std::shared_ptr<TaskState> cur;

    bool tryReadLine(QByteArray& outLine);
};
```

### 3.2 实现进程启动

```cpp
bool Driver::start(const QString& program, const QStringList& args) {
    proc.setProgram(program);
    proc.setArguments(args);
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start();
    return proc.waitForStarted(3000);
}
```

### 3.3 实现请求发送

```cpp
Task Driver::request(const QString& cmd, const QJsonObject& data) {
    // 创建新的 TaskState
    cur = std::make_shared<TaskState>();

    // 构造请求 JSON
    QJsonObject req;
    req["cmd"] = cmd;
    if (!data.isEmpty()) req["data"] = data;

    // 写入 stdin
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    line.append('\n');
    proc.write(line);
    proc.flush();

    // 重置解析状态
    waitingHeader = true;
    buf.clear();

    return Task(this, cur);
}
```

### 3.4 实现 stdout 解析

```cpp
void Driver::pumpStdout() {
    if (!cur) return;

    buf.append(proc.readAllStandardOutput());

    QByteArray line;
    while (tryReadLine(line)) {
        if (waitingHeader) {
            // 解析 header
            if (!parseHeader(line, hdr)) {
                pushError(1000, "invalid header");
                return;
            }
            waitingHeader = false;
        } else {
            // 解析 payload
            QJsonValue payload = parsePayload(line);
            Message msg{hdr.status, hdr.code, payload};
            cur->queue.push_back(msg);

            // 检查是否终态
            if (hdr.status == "done" || hdr.status == "error") {
                cur->terminal = true;
                cur->exitCode = hdr.code;
                cur->finalPayload = payload;
            }

            waitingHeader = true;
        }
    }
}
```

## 4. 验收标准

1. 能正确启动 Driver 进程
2. 能通过 stdin 发送请求
3. 能正确解析 stdout 响应（header + payload 配对）
4. 能处理流式输入（半行、多行）
5. 能正确识别终态（done/error）
6. 能正确终止进程
7. 进程异常退出时能正确处理

## 5. 单元测试用例

### 5.1 进程启动测试

```cpp
TEST(Driver, StartSuccess) {
    Driver d;
    // 使用一个简单的 echo 程序或测试 Driver
    bool ok = d.start("test_driver", {"--mode=stdio", "--profile=keepalive"});
    EXPECT_TRUE(ok);
    EXPECT_TRUE(d.isRunning());
    d.terminate();
}

TEST(Driver, StartFailure) {
    Driver d;
    bool ok = d.start("nonexistent_program");
    EXPECT_FALSE(ok);
}
```

### 5.2 请求发送测试

```cpp
TEST(Driver, RequestSimple) {
    Driver d;
    d.start("test_driver", {"--mode=stdio"});

    Task t = d.request("echo", QJsonObject{{"msg", "hello"}});
    EXPECT_TRUE(t.isValid());

    d.terminate();
}
```

### 5.3 响应解析测试

```cpp
TEST(Driver, ParseSingleResponse) {
    Driver d;
    d.start("test_driver", {"--mode=stdio"});

    Task t = d.request("echo", QJsonObject{{"msg", "hello"}});

    // 等待响应
    Message msg;
    bool got = t.waitNext(msg, 5000);

    EXPECT_TRUE(got);
    EXPECT_EQ(msg.status, "done");

    d.terminate();
}

TEST(Driver, ParseMultipleEvents) {
    Driver d;
    d.start("test_driver", {"--mode=stdio"});

    // 请求一个会产生多个 event 的命令
    Task t = d.request("progress_test");

    int eventCount = 0;
    Message msg;
    while (t.waitNext(msg, 5000)) {
        if (msg.status == "event") eventCount++;
        if (msg.status == "done") break;
    }

    EXPECT_GT(eventCount, 0);

    d.terminate();
}
```

### 5.4 流式解析测试

```cpp
TEST(Driver, PartialResponse) {
    // 模拟 Driver 分多次输出一个完整响应
    // 验证 pumpStdout 能正确处理
}

TEST(Driver, MultipleResponsesInOneRead) {
    // 模拟 Driver 一次输出多个响应
    // 验证能正确解析每一个
}
```

### 5.5 错误处理测试

```cpp
TEST(Driver, InvalidHeader) {
    // 模拟 Driver 输出非法 header
    // 验证能正确生成 error 消息
}

TEST(Driver, ProcessCrash) {
    Driver d;
    d.start("test_driver", {"--mode=stdio"});

    Task t = d.request("crash_test");

    // 验证进程崩溃后 Task 能正确处理
    EXPECT_FALSE(d.isRunning());
}

TEST(Driver, ProcessExit) {
    Driver d;
    d.start("test_driver", {"--mode=stdio", "--profile=oneshot"});

    Task t = d.request("echo", QJsonObject{});

    Message msg;
    t.waitNext(msg, 5000);

    // oneshot 模式下，done 后进程应退出
    d.process()->waitForFinished(1000);
    EXPECT_FALSE(d.isRunning());
}
```

### 5.6 生命周期测试

```cpp
TEST(Driver, Terminate) {
    Driver d;
    d.start("test_driver", {"--mode=stdio", "--profile=keepalive"});

    EXPECT_TRUE(d.isRunning());
    d.terminate();
    EXPECT_FALSE(d.isRunning());
}

TEST(Driver, DestructorTerminates) {
    {
        Driver d;
        d.start("test_driver", {"--mode=stdio", "--profile=keepalive"});
    }
    // 验证析构时进程被终止
}
```

## 6. 依赖关系

- **前置依赖**：
  - 里程碑 1（JSONL 协议基础）：使用协议解析函数
  - 里程碑 2（Driver 端核心）：需要有可用的 Driver 进程进行测试
- **后续依赖**：
  - 里程碑 4（Task 类）：Task 类与 Driver 紧密配合
  - 里程碑 5（多 Driver 并行）：基于 Driver 类实现并行
