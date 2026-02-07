# 模块系统

stdiolink_service 使用 ES Module 规范，脚本以模块模式执行，支持 `import`/`export` 语法。

## 模块解析规则

| import 路径 | 解析方式 |
|-------------|---------|
| `"stdiolink"` | 内置模块，C++ 注册 |
| `"./foo.js"` / `"../lib/bar.js"` | 相对路径，基于当前文件目录解析 |
| `"/abs/path.js"` | 绝对路径，直接加载 |

## 内置模块

`stdiolink` 是唯一的内置模块，提供所有框架 API：

```js
import { Driver, openDriver, exec, getConfig } from 'stdiolink';
```

内置模块名称会被拦截，不经过文件系统查找。

## 文件模块

支持从本地文件系统加载 `.js` 模块：

```js
// 相对路径（基于当前文件所在目录）
import { helper } from './lib/utils.js';

// 父目录
import { shared } from '../common/shared.js';
```
