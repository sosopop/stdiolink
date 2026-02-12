# 异步进程 (stdiolink/process)

`stdiolink/process` 提供异步进程执行 API，包括 `execAsync`（等待完成）和 `spawn`（流式交互）。底层使用 `QProcess`。

> 同步进程执行参见 [exec()](process-binding.md)（来自 `stdiolink` 主模块）。

## 导入

```js
import { execAsync, spawn } from 'stdiolink/process';
```

## execAsync(program, args?, options?)

异步执行外部进程，等待完成后返回结果。返回 `Promise<ExecResult>`。

**参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `program` | `string` | 可执行文件路径（必填） |
| `args` | `string[]` | 命令行参数 |
| `options` | `object` | 执行选项 |

**options 字段：**

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `cwd` | `string` | 当前目录 | 工作目录 |
| `env` | `object` | — | 额外环境变量（与系统环境合并） |
| `input` | `string` | — | 写入 stdin 的数据（写入后自动关闭 stdin） |
| `timeoutMs` | `number` | `30000` | 超时毫秒数 |

**ExecResult 结构：**

| 属性 | 类型 | 说明 |
|------|------|------|
| `exitCode` | `number` | 进程退出码 |
| `stdout` | `string` | 标准输出 |
| `stderr` | `string` | 标准错误 |

```js
const result = await execAsync('git', ['status', '--short'], { cwd: '/my/repo' });
console.log('exit:', result.exitCode);
console.log(result.stdout);
```

**错误处理：**

| 场景 | 行为 |
|------|------|
| 程序不存在 | Promise reject |
| 超时 | kill 进程后 reject（`"execAsync: process timed out"`） |
| 非零退出码 | 正常 resolve，通过 `exitCode` 判断 |

### exec vs execAsync

| | `exec`（stdiolink 主模块） | `execAsync`（stdiolink/process） |
|---|---|---|
| 调用方式 | 同步阻塞 | 异步 Promise |
| 并发 | 阻塞 JS 线程 | 可与其他异步操作并发 |
| 适用场景 | 简单快速的命令 | 耗时操作、需要并发 |

---

## spawn(program, args?, options?)

启动长期运行的子进程，返回 `ProcessHandle` 对象，通过回调接收输出。

**参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `program` | `string` | 可执行文件路径（必填） |
| `args` | `string[]` | 命令行参数 |
| `options` | `object` | 执行选项 |

**options 字段：**

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `cwd` | `string` | 当前目录 | 工作目录 |
| `env` | `object` | — | 额外环境变量（与系统环境合并） |

### ProcessHandle API

| 方法/属性 | 类型 | 说明 |
|-----------|------|------|
| `onStdout(callback)` | 链式 | 注册 stdout 数据回调 `(data: string) => void` |
| `onStderr(callback)` | 链式 | 注册 stderr 数据回调 `(data: string) => void` |
| `onExit(callback)` | 链式 | 注册退出回调 `(result: {exitCode, exitStatus}) => void` |
| `write(data)` | `boolean` | 向 stdin 写入字符串 |
| `closeStdin()` | `void` | 关闭 stdin 写入通道 |
| `kill(signal?)` | `void` | 终止进程。`"SIGKILL"` 强制杀死，其他值（默认）发送 terminate |
| `pid` | `number` | 只读，进程 PID |
| `running` | `boolean` | 只读，进程是否运行中 |

`onExit` 回调的 `exitStatus` 为 `"normal"` 或 `"crash"`。如果进程已退出后再注册 `onExit`，回调会立即触发。

`onStdout`、`onStderr`、`onExit` 均返回 `this`，支持链式调用。

### 使用示例

```js
import { spawn } from 'stdiolink/process';
import { sleep } from 'stdiolink/time';

const handle = spawn('./my_tool', ['--watch'])
    .onStdout((data) => {
        console.log('stdout:', data);
    })
    .onStderr((data) => {
        console.error('stderr:', data);
    })
    .onExit((result) => {
        console.log('exited:', result.exitCode, result.exitStatus);
    });

// 向子进程写入数据
handle.write('start\n');

// 等待一段时间后终止
await sleep(5000);
handle.kill();
```

### 交互式进程

```js
const proc = spawn('python3', ['-u', '-c', 'import sys; print(input("? "))']);

proc.onStdout((data) => {
    console.log('Python says:', data.trim());
});

proc.write('hello from JS\n');
proc.closeStdin();
```
