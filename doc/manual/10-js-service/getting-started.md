# 快速入门

本节介绍如何编写和运行第一个 JS Service 脚本。

## 前置条件

- 已构建 `stdiolink_service` 可执行文件
- 已构建至少一个 Driver（如 `calculator_driver`）

## 最小示例

创建 `hello.js`：

```js
import { openDriver } from 'stdiolink';

const calc = await openDriver('./calculator_driver');
const result = await calc.add({ a: 10, b: 20 });
console.log('10 + 20 =', result.result);
calc.$close();
```

运行：

```bash
stdiolink_service hello.js
```

## 使用配置参数

创建 `configured.js`：

```js
import { defineConfig, getConfig, openDriver } from 'stdiolink';

defineConfig({
    driverPath: {
        type: 'string',
        required: true,
        description: 'Driver 可执行文件路径'
    },
    a: { type: 'int', required: true, description: '第一个操作数' },
    b: { type: 'int', required: true, description: '第二个操作数' }
});

const config = getConfig();
const calc = await openDriver(config.driverPath);
const result = await calc.add({ a: config.a, b: config.b });
console.log(`${config.a} + ${config.b} =`, result.result);
calc.$close();
```

运行：

```bash
stdiolink_service configured.js \
    --config.driverPath=./calculator_driver \
    --config.a=5 --config.b=3
```

查看配置帮助：

```bash
stdiolink_service configured.js --help
```

## 执行外部进程

```js
import { exec } from 'stdiolink';

const r = exec('git', ['status', '--short']);
console.log('exit code:', r.exitCode);
console.log(r.stdout);
```

## 退出码

| 退出码 | 含义 |
|--------|------|
| 0 | 正常退出 |
| 1 | JS 运行时错误 |
| 2 | 参数或文件错误 |

## 下一步

- [模块系统](module-system.md) - 了解模块加载机制
- [配置系统](config-schema.md) - 深入配置参数管理
