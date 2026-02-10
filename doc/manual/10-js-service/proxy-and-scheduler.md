# Proxy 代理与并发调度

`openDriver()` 是推荐的 Driver 调用方式，通过 JS Proxy 将命令映射为异步方法调用。

## openDriver()

### 基本用法

```js
import { openDriver } from 'stdiolink';

const calc = await openDriver('./calculator_driver');
const result = await calc.add({ a: 5, b: 3 });
console.log(result);  // { result: 8 }
calc.$close();
```

### 函数签名

```js
await openDriver(program, args?) → Proxy
```

`openDriver()` 内部执行以下步骤：

1. 创建 `Driver` 实例并调用 `start()`
2. 调用 `queryMeta()` 获取元数据
3. 返回 Proxy 对象，将命令名映射为异步方法

### 保留字段

Proxy 对象提供以下特殊字段：

| 字段 | 类型 | 说明 |
|------|------|------|
| `$driver` | `Driver` | 底层 Driver 实例 |
| `$meta` | `object` | Driver 元数据 |
| `$rawRequest(cmd, data)` | `Task` | 底层请求，返回 Task |
| `$close()` | `void` | 终止 Driver 进程 |

### 错误处理

```js
try {
    const calc = await openDriver('./calculator_driver');
    const result = await calc.add({ a: 5, b: 3 });
    calc.$close();
} catch (e) {
    console.error(e.message);
    // Driver 返回 error 状态时，e.code 和 e.data 包含错误详情
}
```

## 并发调度

### 多 Driver 并行

不同 Driver 实例可通过 `Promise.all` 真正并发执行：

```js
import { openDriver } from 'stdiolink';

const drvA = await openDriver('./driver_a');
const drvB = await openDriver('./driver_b');

const [resultA, resultB] = await Promise.all([
    drvA.scan({ fps: 30 }),
    drvB.scan({ fps: 60 })
]);

drvA.$close();
drvB.$close();
```

内部通过 `JsTaskScheduler` 基于 `waitAnyNext` 实现单线程多任务调度。

## waitAny 多路监听

`waitAny(tasks, timeoutMs?)` 支持在 JS 层异步等待多个 Task，并保留中间 `event` 消息：

```js
import { waitAny } from 'stdiolink';

const result = await waitAny([taskA, taskB], 3000);
if (result) {
    console.log(result.taskIndex, result.msg.status, result.msg.data);
}
```

`waitAny` 适合进度流、多路事件监听等场景，详细说明见 [wait-any-binding.md](wait-any-binding.md)。

### 并发语义

| 场景 | 行为 |
|------|------|
| 不同实例并行调用 | 正常并发，由调度器统一驱动 |
| 同一实例并发调用 | 抛出 `DriverBusyError` |

### 同步 vs 异步 API

| API | 调用方式 | 说明 |
|-----|---------|------|
| `task.waitNext()` | 同步阻塞 | 底层 API，阻塞 JS 线程 |
| `calc.add(params)` | 异步 Promise | Proxy 层，通过调度器非阻塞 |
| `waitAny(tasks, timeoutMs?)` | 异步 Promise | 多路监听，保留中间 event |
| `$rawRequest(cmd, data)` | 返回 Task | 底层 API，用户自行决定同步/异步 |
