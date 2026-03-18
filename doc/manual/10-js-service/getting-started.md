# 快速入门

本节介绍如何编写和运行第一个 JS Service 脚本。

## 前置条件

- 已构建 `stdiolink_service` 可执行文件
- 已构建至少一个 Driver（如 `stdio.drv.calculator`）

## 最小示例

创建服务目录 `hello/`，包含三个文件：

`manifest.json`：

```json
{
    "manifestVersion": "1",
    "id": "hello",
    "name": "Hello",
    "version": "1.0"
}
```

`config.schema.json`：

```json
{}
```

`index.js`：

```js
import { openDriver } from 'stdiolink';
import { resolveDriver } from 'stdiolink/driver';

const calc = await openDriver(resolveDriver('stdio.drv.calculator'));
const result = await calc.add({ a: 10, b: 20 });
console.log('10 + 20 =', result.result);
calc.$close();
```

运行：

```bash
stdiolink_service ./hello --data-root=./build/runtime_release/data_root
```

## 使用配置参数

创建服务目录 `my_calc/`，包含三个文件：

`config.schema.json`：

```json
{
    "driverPath": {
        "type": "string",
        "required": true,
        "description": "Driver 标识，例如 stdio.drv.calculator"
    },
    "a": { "type": "int", "required": true, "description": "第一个操作数" },
    "b": { "type": "int", "required": true, "description": "第二个操作数" }
}
```

`index.js`：

```js
import { getConfig, openDriver } from 'stdiolink';
import { resolveDriver } from 'stdiolink/driver';

const config = getConfig();
const calc = await openDriver(resolveDriver(config.driverPath));
const result = await calc.add({ a: config.a, b: config.b });
console.log(`${config.a} + ${config.b} =`, result.result);
calc.$close();
```

运行：

```bash
stdiolink_service ./my_calc \
    --data-root=./build/runtime_release/data_root \
    --config.driverPath=stdio.drv.calculator \
    --config.a=5 --config.b=3
```

说明：
- 推荐在 Service 中使用 `resolveDriver("stdio.drv.xxx")`，而不是硬编码相对可执行文件路径。
- 若 Driver 位于临时 `data_root` 或发布包外目录，启动 `stdiolink_service` 时请显式传入 `--data-root=<path>`。

查看配置帮助：

```bash
stdiolink_service ./my_calc --help
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
