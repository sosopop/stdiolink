# HTTP 客户端 (stdiolink/http)

`stdiolink/http` 提供异步 HTTP 客户端 API，底层使用 Qt `QNetworkAccessManager`。所有请求返回 `Promise`。

## 导入

```js
import { request, get, post } from 'stdiolink/http';
```

## API 参考

### request(options)

通用 HTTP 请求，返回 `Promise<Response>`。

**options 字段：**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `url` | `string` | 是 | 请求 URL（必须含 scheme） |
| `method` | `string` | 否 | HTTP 方法，默认 `"GET"` |
| `headers` | `object` | 否 | 请求头 `{ key: value }` |
| `query` | `object` | 否 | URL 查询参数 `{ key: value }` |
| `body` | `string \| object` | 否 | 请求体。object 自动序列化为 JSON 并设置 Content-Type |
| `timeoutMs` | `number` | 否 | 超时毫秒数 |
| `parseJson` | `boolean` | 否 | 强制解析响应体为 JSON |

**Response 结构：**

| 属性 | 类型 | 说明 |
|------|------|------|
| `status` | `number` | HTTP 状态码 |
| `headers` | `object` | 响应头（key 小写，同名 header 用逗号合并） |
| `bodyText` | `string` | 响应体文本 |
| `bodyJson` | `any` | 解析后的 JSON（仅当 Content-Type 含 `application/json` 或 `parseJson: true` 时存在） |

```js
const resp = await request({
    url: 'https://api.example.com/data',
    method: 'POST',
    headers: { 'Authorization': 'Bearer token123' },
    body: { key: 'value' },
    timeoutMs: 5000
});
console.log(resp.status, resp.bodyJson);
```

### get(url, options?)

GET 请求的快捷方式。

```js
const resp = await get('https://api.example.com/items');
console.log(resp.bodyJson);
```

`options` 支持 `request()` 的所有字段（`method` 和 `url` 除外）：

```js
const resp = await get('https://api.example.com/items', {
    headers: { 'Accept': 'application/json' },
    timeoutMs: 3000
});
```

### post(url, body?, options?)

POST 请求的快捷方式。

```js
const resp = await post('https://api.example.com/items', { name: 'test' });
console.log(resp.status);
```

`options` 支持 `request()` 的所有字段（`method`、`url`、`body` 除外）。

## 错误处理

| 场景 | 行为 |
|------|------|
| 传输层错误（DNS 失败、连接拒绝等） | Promise reject，错误信息为 `errorString()` |
| HTTP 错误状态码（4xx/5xx） | 正常 resolve，通过 `resp.status` 判断 |
| 超时 | Promise reject |
| `parseJson: true` 但响应非合法 JSON | Promise reject |

```js
try {
    const resp = await get('https://api.example.com/data');
    if (resp.status !== 200) {
        console.error('HTTP error:', resp.status);
    }
} catch (e) {
    console.error('Network error:', e);
}
```
