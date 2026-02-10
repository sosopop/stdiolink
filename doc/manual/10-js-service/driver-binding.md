# Driver/Task 绑定

底层 API，提供对 Driver 进程和 Task 请求的直接控制。

> 大多数场景推荐使用 [openDriver() Proxy](proxy-and-scheduler.md) 代替直接操作 Driver/Task。
> 如需同时监听多个 Task（并保留中间 event），参见 [waitAny()](wait-any-binding.md)。

## Driver 类

### 构造与启动

```js
import { Driver } from 'stdiolink';

const d = new Driver();
const ok = d.start('./my_driver');  // 返回 boolean
```

### API 参考

| 方法/属性 | 类型 | 说明 |
|-----------|------|------|
| `start(program, args?)` | `boolean` | 启动 Driver 进程 |
| `request(cmd, data?)` | `Task` | 发送请求，返回 Task 句柄 |
| `queryMeta(timeoutMs?)` | `object \| null` | 查询元数据（默认 5000ms） |
| `terminate()` | `void` | 终止 Driver 进程 |
| `running` | `boolean` | 只读，进程是否运行中 |
| `hasMeta` | `boolean` | 只读，是否已获取元数据 |

### 生命周期

Driver 对象被 GC 回收时会自动调用 `terminate()`，但推荐显式终止：

```js
const d = new Driver();
d.start('./driver');
// ... 使用 ...
d.terminate();
```

## Task 类

Task 由 `Driver.request()` 创建，不支持直接构造。

### API 参考

| 方法/属性 | 类型 | 说明 |
|-----------|------|------|
| `tryNext()` | `object \| null` | 非阻塞尝试获取下一条消息 |
| `waitNext(timeoutMs?)` | `object \| null` | 阻塞等待下一条消息（默认无超时） |
| `done` | `boolean` | 只读，请求是否已完成 |
| `exitCode` | `number` | 只读，完成时的退出码 |
| `errorText` | `string` | 只读，错误信息 |
| `finalPayload` | `any` | 只读，最终响应数据 |

### 消息格式

`waitNext()` / `tryNext()` 返回的消息对象：

```js
{
    status: "done" | "event" | "error",
    code: 0,
    data: { /* 响应数据 */ }
}
```

### 使用示例

```js
const d = new Driver();
d.start('./calculator_driver');

const task = d.request('add', { a: 10, b: 20 });
const msg = task.waitNext(5000);

if (msg && msg.status === 'done') {
    console.log('result:', msg.data.result);
}

d.terminate();
```
