# JSONL 协议格式

stdiolink 使用 JSONL（JSON Lines）格式进行通信，每条消息占一行。

## 请求格式

### 结构定义

```cpp
struct Request {
    QString cmd;      // 命令名
    QJsonValue data;  // 命令参数
};
```

### JSON 格式

```json
{"cmd":"命令名","data":{参数对象}}
```

### 示例

```json
{"cmd":"echo","data":{"msg":"hello"}}
{"cmd":"scan","data":{"fps":30,"duration":1.5}}
{"cmd":"process","data":{"tags":["a","b"],"options":{"verbose":true}}}
```

## 响应格式

响应由两行组成：帧头和载荷。

### 帧头结构

```cpp
struct FrameHeader {
    QString status;  // "event" | "done" | "error"
    int code = 0;    // 状态码
};
```

### 消息结构

```cpp
struct Message {
    QString status;      // 状态
    int code = 0;        // 状态码
    QJsonValue payload;  // 载荷数据
};
```

### JSON 格式

```
第一行: {"status":"状态","code":状态码}
第二行: {载荷数据}
```

### 状态类型

| 状态 | 说明 | 是否终止 |
|------|------|----------|
| `event` | 中间事件 | 否 |
| `done` | 成功完成 | 是 |
| `error` | 执行失败 | 是 |

### 响应示例

**成功响应：**
```json
{"status":"done","code":0}
{"echo":"hello"}
```

**带进度的响应：**
```json
{"status":"event","code":0}
{"step":1,"total":3}
{"status":"event","code":0}
{"step":2,"total":3}
{"status":"event","code":0}
{"step":3,"total":3}
{"status":"done","code":0}
{}
```

**错误响应：**
```json
{"status":"error","code":404}
{"message":"unknown command"}
```

## 特殊命令

### meta.describe

查询 Driver 元数据：

```json
{"cmd":"meta.describe","data":{}}
```

响应包含完整的 DriverMeta 结构。

### meta.validate

验证命令参数（不执行）：

```json
{"cmd":"meta.validate","data":{"command":"scan","params":{"fps":30}}}
```

## 编码规范

- 使用 UTF-8 编码
- 每条消息必须在单独一行
- 不允许消息内换行
- JSON 必须是紧凑格式（无缩进）
