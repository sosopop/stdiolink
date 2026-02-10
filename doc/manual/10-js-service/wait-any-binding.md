# 多路事件监听 (waitAny)

`waitAny` 是 JS Service 中用于多 Task 异步监听的 API，能够保留中间 `event` 消息。

## 适用场景

- 同时等待多个 Task 的下一条消息
- 监听长任务进度事件流
- 在不阻塞 JS 线程的情况下做多路复用

## API

```js
import { waitAny } from "stdiolink";

const result = await waitAny(tasks, timeoutMs?);
```

- `tasks`: `Task[]`
- `timeoutMs`: 可选，毫秒，未传表示无限等待
- 返回：`Promise<WaitAnyResult | null>`

`WaitAnyResult` 结构：

```js
{
    taskIndex: number,
    msg: {
        status: "event" | "done" | "error",
        code: number,
        data: any
    }
}
```

返回 `null` 的情况：

- `tasks` 为空
- 指定超时且超时
- 所有 Task 已完成且没有可消费消息

## 示例

```js
import { Driver, waitAny } from "stdiolink";

const d = new Driver();
d.start("./calculator_driver");

const task = d.request("batch", {
    operations: [
        { type: "add", a: 1, b: 2 },
        { type: "mul", a: 3, b: 4 }
    ]
});

while (true) {
    const result = await waitAny([task], 5000);
    if (!result) break;

    if (result.msg.status === "event") {
        console.log("progress:", result.msg.data);
        continue;
    }
    if (result.msg.status === "done") {
        console.log("done:", result.msg.data);
        break;
    }
    throw new Error(JSON.stringify(result.msg.data));
}

d.terminate();
```

## 注意事项

- 同一个 `Task` 不应同时被多个并发 `waitAny(...)` 监听。
- 同一个 `Task` 不应同时被 `waitAny(...)` 与 `proxy.command()` 监听；消息是消费型队列。
