# 里程碑 1：JSONL 协议基础

## 1. 目标

实现 JSONL 协议的解析和序列化，为后续 Driver 端和 Host 端通信奠定基础。

## 2. 技术要点

### 2.1 协议格式

**请求格式（Host → Driver）**：
```json
{"cmd":"<command_name>","data":{...}}
```

**响应格式（Driver → Host）**：
- Header 行：`{"status":"<status>","code":<error_code>}`
- Payload 行：紧跟 Header 的下一行，纯 JSON 数据

### 2.2 分帧规则

- 每条消息以 `\n` 结尾
- 必须按字节流解析：一次 read 可能拿到半行或多行
- 解析策略：
  1. `buffer += readData()`
  2. 循环查找 `\n`
  3. 每切出一行（不含 `\n`）就 parse JSON
  4. 剩余半行留在 buffer，等待下次 read 补齐

### 2.3 状态类型

- `event`：中间事件/进度
- `done`：调用完成（成功终态）
- `error`：调用失败（错误终态）

## 3. 实现步骤

### 3.1 定义数据结构

```cpp
// 请求结构
struct Request {
    QString cmd;
    QJsonValue data;
};

// 响应头结构
struct FrameHeader {
    QString status;  // "event" | "done" | "error"
    int code = 0;
};

// 完整消息结构
struct Message {
    QString status;
    int code = 0;
    QJsonValue payload;
};
```

### 3.2 实现序列化函数

```cpp
// 序列化请求
QByteArray serializeRequest(const QString& cmd, const QJsonValue& data);

// 序列化响应（header + payload）
QByteArray serializeResponse(const QString& status, int code, const QJsonValue& payload);
```

### 3.3 实现解析函数

```cpp
// 解析请求
bool parseRequest(const QByteArray& line, Request& out);

// 解析响应头
bool parseHeader(const QByteArray& line, FrameHeader& out);

// 解析 payload（任意 JSON 值）
QJsonValue parsePayload(const QByteArray& line);
```

### 3.4 实现流式缓冲解析器

```cpp
class JsonlParser {
public:
    void append(const QByteArray& data);
    bool tryReadLine(QByteArray& outLine);

private:
    QByteArray buffer;
};
```

## 4. 验收标准

1. 能正确序列化请求为 JSONL 格式
2. 能正确序列化响应（header + payload）为 JSONL 格式
3. 能正确解析请求 JSONL
4. 能正确解析响应头和 payload
5. 流式解析器能正确处理：
   - 完整的单行输入
   - 多行一次性输入
   - 半行输入（需等待后续数据）
   - 空行处理

## 5. 单元测试用例

### 5.1 请求序列化测试

```cpp
TEST(JsonlProtocol, SerializeRequest_Simple) {
    auto result = serializeRequest("scan", QJsonObject{});
    EXPECT_EQ(result, "{\"cmd\":\"scan\"}\n");
}

TEST(JsonlProtocol, SerializeRequest_WithData) {
    QJsonObject data{{"fps", 10}, {"mode", "frame"}};
    auto result = serializeRequest("scan", data);
    // 验证包含 cmd 和 data 字段，以 \n 结尾
}
```

### 5.2 响应序列化测试

```cpp
TEST(JsonlProtocol, SerializeResponse_Done) {
    QJsonObject payload{{"result", 42}};
    auto result = serializeResponse("done", 0, payload);
    // 验证输出两行：header + payload
}

TEST(JsonlProtocol, SerializeResponse_Error) {
    QJsonObject payload{{"message", "invalid input"}};
    auto result = serializeResponse("error", 1007, payload);
    // 验证 status="error", code=1007
}
```

### 5.3 请求解析测试

```cpp
TEST(JsonlProtocol, ParseRequest_Valid) {
    Request req;
    bool ok = parseRequest("{\"cmd\":\"scan\",\"data\":{\"fps\":10}}", req);
    EXPECT_TRUE(ok);
    EXPECT_EQ(req.cmd, "scan");
}

TEST(JsonlProtocol, ParseRequest_Invalid) {
    Request req;
    bool ok = parseRequest("not json", req);
    EXPECT_FALSE(ok);
}
```

### 5.4 响应头解析测试

```cpp
TEST(JsonlProtocol, ParseHeader_Event) {
    FrameHeader hdr;
    bool ok = parseHeader("{\"status\":\"event\",\"code\":0}", hdr);
    EXPECT_TRUE(ok);
    EXPECT_EQ(hdr.status, "event");
    EXPECT_EQ(hdr.code, 0);
}

TEST(JsonlProtocol, ParseHeader_InvalidStatus) {
    FrameHeader hdr;
    bool ok = parseHeader("{\"status\":\"unknown\",\"code\":0}", hdr);
    EXPECT_FALSE(ok);
}
```

### 5.5 流式解析测试

```cpp
TEST(JsonlParser, SingleLine) {
    JsonlParser parser;
    parser.append("{\"cmd\":\"test\"}\n");

    QByteArray line;
    EXPECT_TRUE(parser.tryReadLine(line));
    EXPECT_EQ(line, "{\"cmd\":\"test\"}");
}

TEST(JsonlParser, MultipleLines) {
    JsonlParser parser;
    parser.append("{\"line\":1}\n{\"line\":2}\n");

    QByteArray line;
    EXPECT_TRUE(parser.tryReadLine(line));
    EXPECT_EQ(line, "{\"line\":1}");
    EXPECT_TRUE(parser.tryReadLine(line));
    EXPECT_EQ(line, "{\"line\":2}");
}

TEST(JsonlParser, PartialLine) {
    JsonlParser parser;
    parser.append("{\"cmd\":");

    QByteArray line;
    EXPECT_FALSE(parser.tryReadLine(line));

    parser.append("\"test\"}\n");
    EXPECT_TRUE(parser.tryReadLine(line));
    EXPECT_EQ(line, "{\"cmd\":\"test\"}");
}
```

## 6. 依赖关系

- **前置依赖**：无（这是第一个里程碑）
- **后续依赖**：
  - 里程碑 2（Driver 端核心）依赖本里程碑的解析功能
  - 里程碑 3（Host 端 Driver 类）依赖本里程碑的序列化功能
