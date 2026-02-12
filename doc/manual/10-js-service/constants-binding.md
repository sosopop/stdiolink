# 常量模块 (stdiolink/constants)

`stdiolink/constants` 提供两个只读常量对象：系统信息和应用路径。所有属性均为 deep freeze，不可修改。

## 导入

```js
import { SYSTEM, APP_PATHS } from 'stdiolink/constants';
```

## SYSTEM

当前运行环境的系统信息。

| 属性 | 类型 | 说明 |
|------|------|------|
| `os` | `string` | 操作系统标识：`"windows"` / `"macos"` / `"linux"` / `"unknown"` |
| `isWindows` | `boolean` | 是否为 Windows |
| `isMac` | `boolean` | 是否为 macOS |
| `isLinux` | `boolean` | 是否为 Linux |
| `arch` | `string` | CPU 架构（如 `"x86_64"`、`"arm64"`） |

```js
import { SYSTEM } from 'stdiolink/constants';

if (SYSTEM.isWindows) {
    console.log('Running on Windows', SYSTEM.arch);
}
```

## APP_PATHS

应用和服务相关的路径信息。路径统一使用 `/` 分隔符。

| 属性 | 类型 | 说明 |
|------|------|------|
| `appPath` | `string` | `stdiolink_service` 可执行文件的完整路径 |
| `appDir` | `string` | 可执行文件所在目录 |
| `cwd` | `string` | 启动时的工作目录 |
| `serviceDir` | `string` | 当前服务目录的绝对路径 |
| `serviceEntryPath` | `string` | 入口脚本（`index.js`）的完整路径 |
| `serviceEntryDir` | `string` | 入口脚本所在目录 |
| `tempDir` | `string` | 系统临时目录 |
| `homeDir` | `string` | 用户主目录 |

```js
import { APP_PATHS } from 'stdiolink/constants';

console.log('Service dir:', APP_PATHS.serviceDir);
console.log('Temp dir:', APP_PATHS.tempDir);
```

## 注意事项

- 两个对象均为 deep freeze，赋值操作静默无效
- 路径在 Windows 上也使用 `/` 分隔符
