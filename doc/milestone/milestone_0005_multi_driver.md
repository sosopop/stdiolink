# 里程碑 5：多 Driver 并行

## 1. 目标

实现 `waitAnyNext()` 函数，支持多个 Driver 进程并发运行，Host 能等待任意一个 Driver 的输出。

## 2. 技术要点

### 2.1 并发模型

- 并发不靠消息 id，而是靠**多进程**
- 每个 Driver 同一时刻只处理 1 个请求
- 想并行 → 启动多个 Driver 进程

### 2.2 waitAnyNext 语义

- 输入：多个 Task（来自不同 Driver）
- 输出：一次只返回**一个**新消息（携带来源 task 索引）
- 退出条件：所有任务都完成且无待取消息

### 2.3 实现机制

- 使用 QEventLoop 等待任意 Driver 的 stdout 信号
- 连接所有未完成 Task 对应 Driver 的 readyReadStandardOutput 信号
- 支持超时和手动中断（breakFlag）

## 3. 实现步骤

### 3.1 定义 AnyItem 结构

```cpp
struct AnyItem {
    int taskIndex = -1;  // 来源 Task 在数组中的索引
    Message msg;         // 消息内容
};
```

### 3.2 实现 waitAnyNext 函数

```cpp
bool waitAnyNext(QVector<Task>& tasks,
                 AnyItem& out,
                 int timeoutMs = -1,
                 std::function<bool()> breakFlag = {});
```

### 3.3 实现步骤详解

```cpp
bool waitAnyNext(QVector<Task>& tasks, AnyItem& out,
                 int timeoutMs, std::function<bool()> breakFlag) {

    // 1. 快速路径：先检查已有队列
    for (int i = 0; i < tasks.size(); ++i) {
        Message m;
        if (tasks[i].tryNext(m)) {
            out.taskIndex = i;
            out.msg = std::move(m);
            return true;
        }
    }

    // 2. 检查是否全部完成
    auto allDone = [&] {
        for (const auto& t : tasks) {
            if (t.isValid() && !t.isDone()) return false;
        }
        return true;
    };
    if (allDone()) return false;

    // 3. 使用 QEventLoop 等待
    QEventLoop loop;
    // ... 连接信号、设置超时、处理 breakFlag

    // 4. loop 退出后再尝试取消息
    for (int i = 0; i < tasks.size(); ++i) {
        Message m;
        if (tasks[i].tryNext(m)) {
            out.taskIndex = i;
            out.msg = std::move(m);
            return true;
        }
    }

    return false;
}
```

## 4. 验收标准

1. 能同时管理多个 Driver 进程
2. `waitAnyNext()` 能正确等待任意 Driver 的输出
3. 每次调用只返回一条消息
4. 正确返回消息来源（taskIndex）
5. 所有任务完成后返回 false
6. 超时机制正常工作
7. breakFlag 能正确中断等待

## 5. 单元测试用例

### 5.1 基础功能测试

```cpp
TEST(WaitAnyNext, SingleTask) {
    Driver d;
    d.start("test_driver", {"--mode=stdio"});

    QVector<Task> tasks;
    tasks << d.request("echo", QJsonObject{{"msg", "hello"}});

    AnyItem item;
    bool got = waitAnyNext(tasks, item, 5000);

    EXPECT_TRUE(got);
    EXPECT_EQ(item.taskIndex, 0);
    EXPECT_EQ(item.msg.status, "done");

    d.terminate();
}
```

### 5.2 多任务测试

```cpp
TEST(WaitAnyNext, MultipleTasks) {
    Driver d1, d2;
    d1.start("test_driver", {"--mode=stdio"});
    d2.start("test_driver", {"--mode=stdio"});

    QVector<Task> tasks;
    tasks << d1.request("echo", QJsonObject{{"id", 1}});
    tasks << d2.request("echo", QJsonObject{{"id", 2}});

    std::set<int> receivedIndices;
    AnyItem item;

    while (waitAnyNext(tasks, item, 5000)) {
        receivedIndices.insert(item.taskIndex);
        if (item.msg.status == "done" || item.msg.status == "error") {
            // 继续等待其他任务
        }
    }

    // 应该收到两个任务的响应
    EXPECT_EQ(receivedIndices.size(), 2);

    d1.terminate();
    d2.terminate();
}
```

### 5.3 全部完成测试

```cpp
TEST(WaitAnyNext, AllDone) {
    Driver d1, d2;
    d1.start("test_driver", {"--mode=stdio"});
    d2.start("test_driver", {"--mode=stdio"});

    QVector<Task> tasks;
    tasks << d1.request("echo", QJsonObject{});
    tasks << d2.request("echo", QJsonObject{});

    // 消费所有消息
    AnyItem item;
    while (waitAnyNext(tasks, item, 5000)) {
        // 处理消息
    }

    // 再次调用应返回 false
    EXPECT_FALSE(waitAnyNext(tasks, item, 100));

    d1.terminate();
    d2.terminate();
}
```

### 5.4 超时测试

```cpp
TEST(WaitAnyNext, Timeout) {
    Driver d;
    d.start("test_driver", {"--mode=stdio", "--profile=keepalive"});

    QVector<Task> tasks;
    // 不发送请求，不会有响应

    AnyItem item;
    auto start = QDateTime::currentMSecsSinceEpoch();
    bool got = waitAnyNext(tasks, item, 200);
    auto elapsed = QDateTime::currentMSecsSinceEpoch() - start;

    EXPECT_FALSE(got);
    EXPECT_GE(elapsed, 180);  // 允许一些误差
    EXPECT_LE(elapsed, 300);

    d.terminate();
}
```

### 5.5 breakFlag 测试

```cpp
TEST(WaitAnyNext, BreakFlag) {
    Driver d;
    d.start("test_driver", {"--mode=stdio", "--profile=keepalive"});

    QVector<Task> tasks;
    // 不发送请求

    std::atomic<bool> shouldBreak{false};

    // 在另一个线程中设置 break
    QTimer::singleShot(100, [&] { shouldBreak = true; });

    AnyItem item;
    bool got = waitAnyNext(tasks, item, -1, [&] { return shouldBreak.load(); });

    EXPECT_FALSE(got);

    d.terminate();
}
```

### 5.6 混合状态测试

```cpp
TEST(WaitAnyNext, MixedStates) {
    Driver d1, d2, d3;
    d1.start("test_driver", {"--mode=stdio"});
    d2.start("test_driver", {"--mode=stdio"});
    d3.start("test_driver", {"--mode=stdio"});

    QVector<Task> tasks;
    tasks << d1.request("fast_cmd", QJsonObject{});    // 快速完成
    tasks << d2.request("slow_cmd", QJsonObject{});    // 慢速完成
    tasks << d3.request("error_cmd", QJsonObject{});   // 返回错误

    int doneCount = 0, errorCount = 0;
    AnyItem item;

    while (waitAnyNext(tasks, item, 10000)) {
        if (item.msg.status == "done") doneCount++;
        if (item.msg.status == "error") errorCount++;
    }

    EXPECT_EQ(doneCount + errorCount, 3);

    d1.terminate();
    d2.terminate();
    d3.terminate();
}
```

### 5.7 事件流测试

```cpp
TEST(WaitAnyNext, EventStream) {
    Driver d1, d2;
    d1.start("test_driver", {"--mode=stdio"});
    d2.start("test_driver", {"--mode=stdio"});

    QVector<Task> tasks;
    tasks << d1.request("progress", QJsonObject{{"steps", 3}});
    tasks << d2.request("progress", QJsonObject{{"steps", 2}});

    std::map<int, int> eventCounts;
    AnyItem item;

    while (waitAnyNext(tasks, item, 5000)) {
        if (item.msg.status == "event") {
            eventCounts[item.taskIndex]++;
        }
    }

    EXPECT_EQ(eventCounts[0], 3);
    EXPECT_EQ(eventCounts[1], 2);

    d1.terminate();
    d2.terminate();
}
```

## 6. 依赖关系

- **前置依赖**：
  - 里程碑 3（Host 端 Driver 类）：Driver 进程管理
  - 里程碑 4（Task 类）：Task 消息队列
- **后续依赖**：
  - 无直接后续依赖
  - 里程碑 6（Console 模式）可独立实现
