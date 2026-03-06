# 里程碑 6：Console 模式

## 1. 目标

实现命令行直接调用模式，支持通过 CLI 参数构造请求并执行，便于调试和脚本封装。

## 2. 技术要点

### 2.1 CLI 参数分类

**框架保留参数**（不参与 data 构建）：
- `--help` / `--version`
- `--mode=console|stdio`
- `--profile=oneshot|keepalive`
- `--cmd=<name>`

**data 参数**（构建请求数据）：
- `--key=value` → `data.key = value`
- `--arg-key=value` → `data.key = value`（用于与框架参数冲突时）
- `--a.b.c=value` → `data.a.b.c = value`（嵌套路径）

### 2.2 类型推断规则

- `true` / `false` → bool
- `null` → null
- 整数 / 小数 → number
- 以 `{` 或 `[` 开头 → JSON 解析
- 其它 → string

### 2.3 输出规则

- stdout：只输出 done/error 的 header + payload（两行）
- stderr：中间 event 和日志（可选）

## 3. 实现步骤

### 3.1 定义参数解析器

```cpp
class ConsoleArgs {
public:
    bool parse(int argc, char* argv[]);

    // 框架参数
    bool showHelp = false;
    bool showVersion = false;
    QString mode;      // "console" | "stdio"
    QString profile;   // "oneshot" | "keepalive"
    QString cmd;       // 命令名

    // data 参数
    QJsonObject data;

    // 错误信息
    QString errorMessage;
};
```

### 3.2 实现类型推断

```cpp
QJsonValue inferType(const QString& value) {
    // bool
    if (value == "true") return true;
    if (value == "false") return false;

    // null
    if (value == "null") return QJsonValue::Null;

    // number
    bool ok;
    if (!value.contains('.')) {
        int i = value.toInt(&ok);
        if (ok) return i;
    }
    double d = value.toDouble(&ok);
    if (ok) return d;

    // JSON object/array
    if (value.startsWith('{') || value.startsWith('[')) {
        QJsonDocument doc = QJsonDocument::fromJson(value.toUtf8());
        if (!doc.isNull()) {
            if (doc.isObject()) return doc.object();
            if (doc.isArray()) return doc.array();
        }
    }

    // string
    return value;
}
```

### 3.3 实现嵌套路径设置

```cpp
void setNestedValue(QJsonObject& root,
                    const QString& path,
                    const QJsonValue& value) {
    QStringList parts = path.split('.');
    QJsonObject* current = &root;

    for (int i = 0; i < parts.size() - 1; ++i) {
        const QString& key = parts[i];
        if (!current->contains(key)) {
            (*current)[key] = QJsonObject{};
        }
        // 需要特殊处理以支持嵌套修改
        // ...
    }

    (*current)[parts.last()] = value;
}
```

### 3.4 实现 Console 模式主函数

```cpp
int runConsoleMode(const ConsoleArgs& args, ICommandHandler* handler) {
    // 1. 构造请求
    Request req{args.cmd, args.data};

    // 2. 创建响应器（只输出最终结果）
    ConsoleResponder responder;

    // 3. 执行命令
    handler->handle(req.cmd, req.data, responder);

    // 4. 返回退出码
    return responder.exitCode();
}
```

## 4. 验收标准

1. 能正确解析框架保留参数
2. 能正确解析 data 参数并构建 QJsonObject
3. 类型推断正确（bool/null/number/JSON/string）
4. 嵌套路径正确设置（`--a.b.c=value`）
5. `--arg-` 前缀正确处理冲突参数
6. stdout 只输出最终 done/error
7. 退出码正确反映执行结果

## 5. 单元测试用例

### 5.1 参数解析测试

```cpp
TEST(ConsoleArgs, ParseCmd) {
    const char* argv[] = {"prog", "--cmd=scan"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(2, const_cast<char**>(argv)));
    EXPECT_EQ(args.cmd, "scan");
}

TEST(ConsoleArgs, ParseMode) {
    const char* argv[] = {"prog", "--mode=console", "--profile=oneshot"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(3, const_cast<char**>(argv)));
    EXPECT_EQ(args.mode, "console");
    EXPECT_EQ(args.profile, "oneshot");
}

TEST(ConsoleArgs, ParseHelp) {
    const char* argv[] = {"prog", "--help"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(2, const_cast<char**>(argv)));
    EXPECT_TRUE(args.showHelp);
}
```

### 5.2 类型推断测试

```cpp
TEST(InferType, Bool) {
    EXPECT_EQ(inferType("true"), QJsonValue(true));
    EXPECT_EQ(inferType("false"), QJsonValue(false));
}

TEST(InferType, Null) {
    EXPECT_TRUE(inferType("null").isNull());
}

TEST(InferType, Integer) {
    EXPECT_EQ(inferType("42"), QJsonValue(42));
    EXPECT_EQ(inferType("-10"), QJsonValue(-10));
}

TEST(InferType, Double) {
    EXPECT_EQ(inferType("3.14"), QJsonValue(3.14));
}

TEST(InferType, JsonObject) {
    auto val = inferType("{\"x\":1,\"y\":2}");
    EXPECT_TRUE(val.isObject());
    EXPECT_EQ(val.toObject()["x"].toInt(), 1);
}

TEST(InferType, JsonArray) {
    auto val = inferType("[1,2,3]");
    EXPECT_TRUE(val.isArray());
    EXPECT_EQ(val.toArray().size(), 3);
}

TEST(InferType, String) {
    EXPECT_EQ(inferType("hello"), QJsonValue("hello"));
    EXPECT_EQ(inferType("123abc"), QJsonValue("123abc"));
}
```

### 5.3 Data 参数测试

```cpp
TEST(ConsoleArgs, DataSimple) {
    const char* argv[] = {"prog", "--cmd=scan", "--fps=10"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(3, const_cast<char**>(argv)));
    EXPECT_EQ(args.data["fps"].toInt(), 10);
}

TEST(ConsoleArgs, DataMultiple) {
    const char* argv[] = {"prog", "--cmd=scan",
                          "--fps=10", "--enable=true", "--name=test"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(5, const_cast<char**>(argv)));
    EXPECT_EQ(args.data["fps"].toInt(), 10);
    EXPECT_EQ(args.data["enable"].toBool(), true);
    EXPECT_EQ(args.data["name"].toString(), "test");
}

TEST(ConsoleArgs, DataNested) {
    const char* argv[] = {"prog", "--cmd=scan", "--roi.x=10", "--roi.y=20"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(4, const_cast<char**>(argv)));

    auto roi = args.data["roi"].toObject();
    EXPECT_EQ(roi["x"].toInt(), 10);
    EXPECT_EQ(roi["y"].toInt(), 20);
}

TEST(ConsoleArgs, DataArgPrefix) {
    // --arg-mode 用于避免与 --mode 冲突
    const char* argv[] = {"prog", "--cmd=scan",
                          "--mode=console", "--arg-mode=frame"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(4, const_cast<char**>(argv)));
    EXPECT_EQ(args.mode, "console");
    EXPECT_EQ(args.data["mode"].toString(), "frame");
}
```

### 5.4 集成测试

```cpp
TEST(ConsoleMode, EchoCommand) {
    const char* argv[] = {"prog", "--mode=console", "--profile=oneshot",
                          "--cmd=echo", "--msg=hello"};
    ConsoleArgs args;
    args.parse(5, const_cast<char**>(argv));

    MockHandler handler;
    int exitCode = runConsoleMode(args, &handler);

    EXPECT_EQ(exitCode, 0);
}

TEST(ConsoleMode, ErrorCommand) {
    const char* argv[] = {"prog", "--mode=console", "--cmd=unknown"};
    ConsoleArgs args;
    args.parse(3, const_cast<char**>(argv));

    MockHandler handler;
    int exitCode = runConsoleMode(args, &handler);

    EXPECT_NE(exitCode, 0);
}
```

### 5.5 输出格式测试

```cpp
TEST(ConsoleResponder, DoneOutput) {
    // 捕获 stdout
    ConsoleResponder responder;
    responder.done(0, QJsonObject{{"result", 42}});

    // 验证输出两行：
    // {"status":"done","code":0}
    // {"result":42}
}

TEST(ConsoleResponder, ErrorOutput) {
    ConsoleResponder responder;
    responder.error(1007, QJsonObject{{"message", "invalid"}});

    // 验证输出 error 状态
}

TEST(ConsoleResponder, EventSuppressed) {
    // Console 模式下 event 不输出到 stdout
    ConsoleResponder responder;
    responder.event(0, QJsonObject{{"progress", 0.5}});

    // 验证 stdout 无输出（或输出到 stderr）
}
```

### 5.6 边界情况测试

```cpp
TEST(ConsoleArgs, EmptyData) {
    const char* argv[] = {"prog", "--cmd=info"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(2, const_cast<char**>(argv)));
    EXPECT_TRUE(args.data.isEmpty());
}

TEST(ConsoleArgs, InvalidJson) {
    const char* argv[] = {"prog", "--cmd=test", "--obj={invalid}"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(3, const_cast<char**>(argv)));
    // 无效 JSON 应被当作字符串
    EXPECT_EQ(args.data["obj"].toString(), "{invalid}");
}

TEST(ConsoleArgs, MissingCmd) {
    const char* argv[] = {"prog", "--mode=console"};
    ConsoleArgs args;
    EXPECT_FALSE(args.parse(2, const_cast<char**>(argv)));
    EXPECT_FALSE(args.errorMessage.isEmpty());
}
```

## 6. 依赖关系

- **前置依赖**：
  - 里程碑 1（JSONL 协议基础）：响应格式
  - 里程碑 2（Driver 端核心）：命令处理接口
- **后续依赖**：
  - 无（这是最后一个里程碑）
