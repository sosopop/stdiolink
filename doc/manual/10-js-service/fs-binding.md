# 文件系统 (stdiolink/fs)

`stdiolink/fs` 提供同步文件系统操作 API。IO 错误抛出 `InternalError`（含路径信息），类型错误抛出 `TypeError`。

## 导入

```js
import { exists, readText, writeText, readJson, writeJson, mkdir, listDir, stat } from 'stdiolink/fs';
```

## API 参考

### exists(path)

检查文件或目录是否存在。

```js
if (exists('/tmp/data.json')) {
    // ...
}
```

### readText(path)

读取文件内容为 UTF-8 字符串。文件不存在或非 UTF-8 编码时抛出 `InternalError`。

```js
const content = readText('./config.txt');
```

### writeText(path, text, options?)

写入字符串到文件。

| options 字段 | 类型 | 默认值 | 说明 |
|-------------|------|--------|------|
| `append` | `boolean` | `false` | 追加模式 |
| `ensureParent` | `boolean` | `false` | 自动创建父目录 |

```js
writeText('/tmp/output.txt', 'hello world');
writeText('/tmp/log.txt', 'line\n', { append: true });
writeText('/tmp/deep/dir/file.txt', 'data', { ensureParent: true });
```

### readJson(path)

读取 JSON 文件并解析为 JS 对象/数组。JSON 格式错误时抛出 `InternalError`。

```js
const config = readJson('./settings.json');
```

### writeJson(path, value, options?)

将 JS 对象/数组序列化为 JSON 并写入文件（Compact 格式）。

| options 字段 | 类型 | 默认值 | 说明 |
|-------------|------|--------|------|
| `ensureParent` | `boolean` | `false` | 自动创建父目录 |

```js
writeJson('/tmp/result.json', { status: 'ok', count: 42 });
```

### mkdir(path, options?)

创建目录。

| options 字段 | 类型 | 默认值 | 说明 |
|-------------|------|--------|------|
| `recursive` | `boolean` | `true` | 递归创建父目录 |

```js
mkdir('/tmp/a/b/c');                    // 递归创建
mkdir('/tmp/single', { recursive: false }); // 仅创建最后一级
```

### listDir(path, options?)

列出目录内容，返回 `string[]`（按名称排序）。

| options 字段 | 类型 | 默认值 | 说明 |
|-------------|------|--------|------|
| `recursive` | `boolean` | `false` | 递归列出子目录（返回相对路径） |
| `filesOnly` | `boolean` | `false` | 仅列出文件 |
| `dirsOnly` | `boolean` | `false` | 仅列出目录 |

`filesOnly` 和 `dirsOnly` 互斥，同时为 `true` 时抛出 `TypeError`。

```js
const files = listDir('./data', { filesOnly: true });
const all = listDir('./project', { recursive: true });
```

### stat(path)

获取文件/目录的元信息。路径不存在时抛出 `InternalError`。

返回对象：

| 属性 | 类型 | 说明 |
|------|------|------|
| `isFile` | `boolean` | 是否为文件 |
| `isDir` | `boolean` | 是否为目录 |
| `size` | `number` | 文件大小（字节） |
| `mtimeMs` | `number` | 最后修改时间（毫秒时间戳） |

```js
const info = stat('./data.json');
console.log('Size:', info.size, 'Modified:', new Date(info.mtimeMs));
```
