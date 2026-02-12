# 时间模块 (stdiolink/time)

`stdiolink/time` 提供时间获取与非阻塞 sleep。`sleep` 通过 Qt `QTimer::singleShot` 桥接到 QuickJS Promise，不阻塞 JS 事件循环。

## 导入

```js
import { nowMs, monotonicMs, sleep } from 'stdiolink/time';
```

## API 参考

### nowMs()

返回当前 UTC 时间的毫秒时间戳（`number`），等价于 `Date.now()`。

```js
const ts = nowMs();
console.log('Current time:', new Date(ts).toISOString());
```

### monotonicMs()

返回单调递增的毫秒计数（`number`），从首次调用时开始计时。适合测量时间间隔，不受系统时钟调整影响。

```js
const start = monotonicMs();
// ... 执行操作 ...
const elapsed = monotonicMs() - start;
console.log('Elapsed:', elapsed, 'ms');
```

### sleep(ms)

非阻塞延迟，返回 `Promise<void>`。

- `ms` 必须为 `>= 0` 的有限数值，否则抛出 `TypeError` 或 `RangeError`
- 不阻塞 JS 事件循环，其他 Promise（如 Driver 调用、HTTP 请求）可在 sleep 期间继续执行

```js
console.log('start');
await sleep(1000);
console.log('1 second later');
```

### 组合示例

```js
import { openDriver } from 'stdiolink';
import { sleep, monotonicMs } from 'stdiolink/time';

const drv = await openDriver('./sensor_driver');

for (let i = 0; i < 10; i++) {
    const t0 = monotonicMs();
    const data = await drv.read({});
    console.log(`Read #${i}: ${monotonicMs() - t0}ms`, data);
    await sleep(1000);
}

drv.$close();
```
