# 多任务并行等待

`waitAnyNext` 函数用于同时等待多个 Task 的响应。

## 函数定义

```cpp
namespace stdiolink {

struct AnyItem {
    int taskIndex = -1;  // 来源 Task 索引
    Message msg;         // 消息内容
};

bool waitAnyNext(QVector<Task>& tasks,
                 AnyItem& out,
                 int timeoutMs = -1,
                 std::function<bool()> breakFlag = {});

}
```

## 参数说明

| 参数 | 类型 | 说明 |
|------|------|------|
| tasks | QVector<Task>& | Task 列表 |
| out | AnyItem& | 输出结果 |
| timeoutMs | int | 超时毫秒 |
| breakFlag | function | 中断条件 |

## 使用示例

```cpp
Driver d1, d2;
d1.start("driver1.exe");
d2.start("driver2.exe");

QVector<Task> tasks;
tasks << d1.request("cmd1", data1);
tasks << d2.request("cmd2", data2);

AnyItem item;
while (waitAnyNext(tasks, item, 5000)) {
    qDebug() << "Task" << item.taskIndex
             << ":" << item.msg.status;
}

d1.terminate();
d2.terminate();
```
