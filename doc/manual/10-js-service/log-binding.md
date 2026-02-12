# 结构化日志 (stdiolink/log)

`stdiolink/log` 提供结构化日志 API。日志输出为 JSON line 格式，映射到 Qt 日志通道（stderr）。

## 导入

```js
import { createLogger } from 'stdiolink/log';
```

## createLogger(baseFields?)

创建 Logger 实例。`baseFields` 为可选的基础字段对象，会附加到每条日志中。

```js
const log = createLogger({ service: 'data-collector', version: '1.0' });
```

## Logger API

| 方法 | 说明 | Qt 日志级别 |
|------|------|------------|
| `debug(msg, fields?)` | 调试日志 | `qDebug` |
| `info(msg, fields?)` | 信息日志 | `qInfo` |
| `warn(msg, fields?)` | 警告日志 | `qWarning` |
| `error(msg, fields?)` | 错误日志 | `qCritical` |
| `child(extraFields)` | 创建子 Logger，继承并扩展 baseFields | — |

- `msg`：日志消息字符串
- `fields`：可选的附加字段对象，与 baseFields 合并（同名 key 覆盖）

## 输出格式

每条日志输出一行 JSON：

```json
{"ts":"2025-01-15T08:30:00.123Z","level":"info","msg":"started","fields":{"service":"data-collector","version":"1.0"}}
```

| 字段 | 说明 |
|------|------|
| `ts` | UTC 时间戳（ISO 8601 毫秒精度） |
| `level` | 日志级别：`debug` / `info` / `warn` / `error` |
| `msg` | 日志消息 |
| `fields` | 合并后的字段对象（无字段时省略） |

## 使用示例

```js
import { createLogger } from 'stdiolink/log';

const log = createLogger({ module: 'scanner' });

log.info('scan started', { dir: '/opt/services' });
log.debug('found service', { id: 'data-collector' });
log.warn('schema missing', { id: 'legacy-service' });
log.error('scan failed', { reason: 'permission denied' });
```

### 子 Logger

`child()` 创建继承父 Logger baseFields 的子实例，适合在不同上下文中添加额外标识：

```js
const log = createLogger({ service: 'collector' });
const taskLog = log.child({ taskId: 'task-001' });

taskLog.info('processing');
// {"ts":"...","level":"info","msg":"processing","fields":{"service":"collector","taskId":"task-001"}}
```
