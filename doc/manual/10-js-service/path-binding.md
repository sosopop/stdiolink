# 路径操作 (stdiolink/path)

`stdiolink/path` 提供纯函数式路径操作 API，无状态。底层使用 Qt 的 `QDir`/`QFileInfo`，输出路径统一使用 `/` 分隔符。

## 导入

```js
import { join, resolve, dirname, basename, extname, normalize, isAbsolute } from 'stdiolink/path';
```

## API 参考

### join(...segments)

拼接路径片段并规范化。

```js
join('a', 'b', 'c.txt')       // "a/b/c.txt"
join('a', '../b')              // "b"
join()                         // "."
```

### resolve(...segments)

从当前工作目录开始，逐段解析为绝对路径。遇到绝对路径段时重置基准。

```js
resolve('a', 'b')              // "/current/cwd/a/b"
resolve('/tmp', 'file.txt')    // "/tmp/file.txt"
resolve('a', '/tmp', 'b')      // "/tmp/b"
```

### dirname(path)

返回路径的目录部分。

```js
dirname('/a/b/c.txt')          // "/a/b"
dirname('file.txt')            // "."
```

### basename(path)

返回路径的文件名部分（含扩展名）。末尾分隔符会被忽略。

```js
basename('/a/b/c.txt')         // "c.txt"
basename('/a/b/')              // "b"
```

### extname(path)

返回扩展名（含 `.`），无扩展名返回空字符串。

```js
extname('file.txt')            // ".txt"
extname('file')                // ""
extname('.gitignore')          // "gitignore"
```

### normalize(path)

规范化路径，解析 `.`、`..` 并统一分隔符。

```js
normalize('a/./b/../c')        // "a/c"
normalize('a\\b\\c')           // "a/b/c"
```

### isAbsolute(path)

判断路径是否为绝对路径。

```js
isAbsolute('/tmp/file')        // true
isAbsolute('relative/path')    // false
isAbsolute('C:/Windows')       // true (Windows)
```

## 错误处理

所有函数的参数必须为 `string` 类型，否则抛出 `TypeError`。
