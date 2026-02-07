# 进程调用

`exec()` 函数提供在 JS 中执行外部进程的能力，基于 `QProcess` 实现同步调用。

## 基本用法

```js
import { exec } from 'stdiolink';

const r = exec('git', ['status', '--short']);
console.log('exit code:', r.exitCode);
console.log(r.stdout);
```

## 函数签名

```js
exec(program, args?, options?) → { exitCode, stdout, stderr }
```

### 参数

| 参数 | 类型 | 说明 |
|------|------|------|
| `program` | `string` | 可执行文件路径（必填） |
| `args` | `string[]` | 命令行参数（可选） |
| `options` | `object` | 执行选项（可选） |

### options 选项

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `cwd` | `string` | 当前目录 | 工作目录 |
| `env` | `object` | - | 额外环境变量（与系统环境合并） |
| `timeout` | `number` | 30000 | 超时时间（毫秒） |
| `input` | `string` | - | 写入 stdin 的数据 |

### 返回值

| 字段 | 类型 | 说明 |
|------|------|------|
| `exitCode` | `number` | 进程退出码 |
| `stdout` | `string` | 标准输出（UTF-8） |
| `stderr` | `string` | 标准错误（UTF-8） |

## 错误处理

| 场景 | 行为 |
|------|------|
| 程序不存在 | 抛出 `Error` |
| 进程超时 | kill 进程并抛出 `Error("Process timed out")` |
| 非零退出码 | 不抛异常，通过 `exitCode` 返回 |

```js
import { exec } from 'stdiolink';

try {
    const r = exec('__nonexistent__');
} catch (e) {
    console.error('启动失败:', e.message);
}
```

## 使用示例

### 设置工作目录

```js
const r = exec('ls', ['-la'], { cwd: '/tmp' });
```

### 传入 stdin 数据

```js
const r = exec('wc', ['-l'], { input: 'line1\nline2\nline3\n' });
console.log(r.stdout);  // "3"
```

### 设置超时

```js
const r = exec('sleep', ['10'], { timeout: 2000 });
// 2 秒后超时，抛出异常
```
