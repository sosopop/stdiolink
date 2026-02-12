# 模块系统

stdiolink_service 使用 ES Module 规范，脚本以模块模式执行，支持 `import`/`export` 语法。

## 模块解析规则

| import 路径 | 解析方式 |
|-------------|---------|
| `"stdiolink"` | 内置模块，C++ 注册 |
| `"./foo.js"` / `"../lib/bar.js"` | 相对路径，基于当前文件目录解析 |
| `"/abs/path.js"` | 绝对路径，直接加载 |

## 内置模块

内置模块名称会被拦截，不经过文件系统查找。

### stdiolink（主模块）

核心 Driver 编排 API：

```js
import { Driver, openDriver, waitAny, exec, getConfig } from 'stdiolink';
```

### stdiolink/constants

系统信息与路径常量：

```js
import { SYSTEM, APP_PATHS } from 'stdiolink/constants';
```

### stdiolink/path

纯函数式路径操作：

```js
import { join, resolve, dirname, basename, extname, normalize, isAbsolute } from 'stdiolink/path';
```

### stdiolink/fs

同步文件系统操作：

```js
import { exists, readText, writeText, readJson, writeJson, mkdir, listDir, stat } from 'stdiolink/fs';
```

### stdiolink/time

时间获取与非阻塞 sleep：

```js
import { nowMs, monotonicMs, sleep } from 'stdiolink/time';
```

### stdiolink/http

异步 HTTP 客户端：

```js
import { request, get, post } from 'stdiolink/http';
```

### stdiolink/log

结构化日志：

```js
import { createLogger } from 'stdiolink/log';
```

### stdiolink/process

异步进程执行：

```js
import { execAsync, spawn } from 'stdiolink/process';
```

## 文件模块

支持从本地文件系统加载 `.js` 模块：

```js
// 相对路径（基于当前文件所在目录）
import { helper } from './lib/utils.js';

// 父目录
import { shared } from '../common/shared.js';
```
