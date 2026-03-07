# Driver 路径解析

`stdiolink/driver` 模块提供 Driver 可执行文件路径解析功能。

## resolveDriver

根据 Driver 名称自动查找可执行文件路径。

### 语法

```js
import { resolveDriver } from 'stdiolink/driver';

const path = resolveDriver(driverName);
```

### 参数

| 参数 | 类型 | 说明 |
|------|------|------|
| `driverName` | `string` | Driver 名称（如 `"stdio.drv.calculator"`） |

### 返回值

返回 Driver 可执行文件的绝对路径（字符串）。

### 查找规则

按以下顺序查找：

1. `<data_root>/drivers/<driverName>/<driverName>.exe`（Windows）
2. `<data_root>/drivers/<driverName>/<driverName>`（Unix）
3. `<app_dir>/<driverName>.exe`（Windows）
4. `<app_dir>/<driverName>`（Unix）

其中：
- `data_root` 通过 `--data-root` 参数指定，默认为当前目录
- `app_dir` 为 `stdiolink_service` 可执行文件所在目录

### 使用示例

```js
import { openDriver } from 'stdiolink';
import { resolveDriver } from 'stdiolink/driver';

// 推荐写法：使用 resolveDriver 自动查找
const calc = await openDriver(resolveDriver('stdio.drv.calculator'));
const result = await calc.add({ a: 10, b: 20 });
calc.$close();
```

### 错误处理

当 Driver 未找到时抛出异常，包含搜索路径列表：

```
Error: driver not found: "stdio.drv.xxx"
  searched:
    - /path/to/data_root/drivers/stdio.drv.xxx/stdio.drv.xxx
    - /path/to/app_dir/stdio.drv.xxx
```

### 约束条件

- `driverName` 不能包含路径分隔符（`/` 或 `\`）
- `driverName` 不能以 `.exe` 结尾
- `driverName` 不能为空字符串

### 最佳实践

- 在 Service 中使用 `resolveDriver()` 而非硬编码路径
- 临时 runtime 场景显式传 `--data-root` 参数
- Driver 名称使用 `stdio.drv.` 前缀以便 Server 扫描

### 相关文档

- [Proxy 代理与并发调度](proxy-and-scheduler.md) - openDriver() 使用示例
- [常量模块](constants-binding.md) - APP_PATHS.dataRoot 说明
