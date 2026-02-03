# IPC 通讯设计文档（Qt / JSONL / StdIO + Console 最小实现）— stdiolink 简化协议版

> 目标：在 Windows / Linux / macOS 上实现**最小工作量**的跨平台 IPC（进程间通信），统一使用 **JSONL** 作为协议载体。  
> 库/工具名称：**stdiolink**（Driver/Host 都以此命名为准）。

本设计只保留两种通信入口：

1. **StdIO（标准输入输出管道）**：Host 通过 `QProcess` 启动 Driver；Host 写 stdin；Driver 从 stdin 读；Driver 在 stdout 输出协议消息（JSONL）；stderr 仅用于日志。
2. **Console（命令行直跑）**：Driver 脱离 Host，通过命令行参数构造请求并执行，便于调试与后期脚本封装调用。

---

## 1. 设计原则

### 1.1 单一协议：JSONL（Line-delimited JSON）
- 每条消息是一行 JSON，以 `\n` 结尾。
- **必须按字节流解析**：一次 read 可能拿到半行，也可能拿到多行。

### 1.2 一次请求 = “写一次 / 读多次 / 结束”
Host 发起一次完整调用的形态是：

> **写（1 行请求） → 读（N 行响应/事件） → 读到 done/error 即结束**

这意味着：
- 协议不需要 `req/resp` 标识；
- 协议不需要 `id`（同一个 Driver 同一时刻只处理 1 个 in-flight 请求，天然“一来一回”）；
- Driver 可在一次调用过程中持续输出进度/事件（Host 反复读取）；
- Host 可以同时对多个 Driver 发起请求，再用 `QEventLoop` 等待“任意一个 Driver 有输出”。

### 1.3 OneShot / Keepalive
- **oneshot**：只处理 1 次请求，输出 `done` 或 `error` 后自动退出。
- **keepalive**：处理完 1 次请求后不退出，继续等待下一条请求。

---

## 2. StdIO 传输方式与边界

### 2.1 约定
- **协议消息只走 stdout**（JSONL）
- **stderr 只打印人类可读日志**（不参与协议解析）
- 每发送一行协议消息后尽量 `flush()`，避免 stdout 缓冲导致 Host 看起来“卡住”。

### 2.2 分帧规则（非常重要）
无论 Host 还是 Driver，都必须采用以下解析策略：

1. `buffer += readAllStandardOutput()`
2. 循环查找 `\n`
3. 每切出一行（不含 `\n`）就 parse JSON
4. 剩余半行留在 buffer，等待下次 read 补齐

---

## 3. JSONL 协议规范（stdiolink）

### 3.1 请求格式（Host → Driver）
一行 JSON，只包含两部分：

```json
{"cmd":"<command_name>","data":{...}}
```

- `cmd`：命令名（字符串，必填）
- `data`：数据（JSON object/array/primitive，选填；无参数可省略或置 `null`）

**示例**
```json
{"cmd":"scan","data":{"mode":"frame","fps":10}}
```

### 3.2 响应格式（Driver → Host）
响应**头**是一行 JSON，只包含两部分：

```json
{"status":"<status>","code":<error_code>}
```

- `status`：状态字符串（必填）
  - `event`：本次调用过程中的中间事件/进度
  - `done`：本次调用完成（最终结果）
  - `error`：本次调用失败（错误终止）
- `code`：错误码（int，必填）
  - 成功类：一般 `0`
  - 失败类：非 0（由业务定义枚举）

> 你要求“返回只需要状态、错误码”，因此响应头严格只保留这两个字段。

### 3.3 payload 行规则（可选，但强烈建议）
因为响应头被压到最简（只有 `status + code`），而业务又经常需要携带数据，所以约定：

- 当 `status` 为 `event` / `done` / `error` 时，**下一行紧跟 1 行 payload JSON**（一行一个 JSON 值）。
- payload 不需要 `id`、不需要 `type`，纯数据即可。

也就是：**“1 行头 + 1 行数据”** 成对出现。

> 如果某条状态不需要数据，也可以省略 payload 行，但为了降低 Host 解析复杂度，建议：  
> **event/done/error 都固定带 payload（哪怕是 `{}`）**。

**示例：一个 event**
```json
{"status":"event","code":0}
{"progress":0.35,"message":"processing chunk 7/20"}
```

**示例：最终 done**
```json
{"status":"done","code":0}
{"result":{"count":128,"duration_ms":5321}}
```

**示例：error**
```json
{"status":"error","code":1007}
{"message":"invalid input","field":"fps","expect":"int >= 1"}
```

---

## 4. Host 抽象（Driver + Task/Poll + 多 Driver 并行）

> 目标：把同步/异步统一成一种 **Future/Promise 风格**的调用方式：`request()` 立即返回 `Task`；随后通过 `tryNext()/waitNext()` **连续取回** event/done/error，直到完成。  
> 并发不靠 `id`，而是靠 **多进程（多个 Driver）**；Host 用 `QEventLoop` 等待“任意一个 Driver 有新输出”。

### 4.1 Driver（一个进程实例）
`Driver` 表示一个被 Host 通过 `QProcess` 拉起的 Driver 进程。

- `start(program, args)`：启动进程（keepalive 优先）
- `request(cmd, kvArgs) -> Task`：写入 1 行请求，然后立即返回 `Task`
- `pumpStdout()`：解析 stdout，把新消息推入当前 Task 的队列（通常在 `readyReadStandardOutput` 里调用）
- `terminate()`：结束进程（析构兜底）

**重要约束（保持协议极简）：**
- **同一个 Driver 同一时刻只允许 1 个 in-flight 请求**（所以协议不需要 `id`）。
- 想并行 → **起多个 Driver 进程**，每个进程同时处理 1 个请求。

### 4.2 Task（Future / Promise 风格，可连续取消息）
`Task` 是一次 `request()` 的返回值（句柄），它承载：
- 这次调用的“完成/失败”状态
- `done/error` 的最终 payload
- 调用过程中产生的 0..N 条 `event`（以及最终 `done/error`）消息队列

#### 4.2.1 next 语义（tryNext / waitNext）
- `task.tryNext(outMsg)`：**非阻塞**，立即返回  
  - 有新消息 → `true`  
  - 无新消息 / 已完成且无残留消息 → `false`

- `task.waitNext(outMsg, int timeoutMs = -1)`：**阻塞等待**  
  - 等到新消息 → `true`  
  - 任务完成且不会再有消息 → `false`  
  - 超时 → `false`

- `task.isDone() const`：是否已完成（并且不会再产生新消息，队列也已取空）
- `task.exitCode() const`（可选）：最终 `done/error` 对应的 `code`
- `task.errorText() const`（可选）：当最终为 `error` 时的错误文本（通常来自 payload 的 `"message"` 字段，或内部解析错误）

> 这实现了你要的：Host 可以用 `while (waitAnyNext(tasks, out)) { ... }` 持续拿到“所有任务的新消息”，直到所有任务都完成且再也拿不到新消息。


#### 4.2.2 Host 侧“消息”结构（仅内存结构，不影响协议）
```cpp
struct Message {
    QString status;   // "event" | "done" | "error"
    int     code;     // 0=OK，非0=错误码（event 也可带阶段码）
    QJsonValue payload; // 下一行 payload JSON
};
```

### 4.3 多 Driver 并行：waitAnyNext(tasks)
`waitAnyNext()` 用于：**多进程并发**时，把“等待任意输出 + 解析 + 每次返回一条新消息”封装成一个统一循环。

#### 4.3.1 约定
- 输入：`tasks`（来自不同 Driver 的多个 Task）
- 输出：一次只返回 **一个** 新消息（并携带来源 task）
- 退出：当 **所有任务都 completed 且无待取消息** 时返回 `false`
- 可选：支持 `breakFlag`（人工跳出）或 `timeoutMs`

#### 4.3.2 典型用法
```cpp
QVector<Task> tasks;
tasks << d1.request("scan",  QJsonObject{{"fps",10},{"mode","frame"}})
      << d2.request("mesh.union", QJsonObject{{"a","A.stl"},{"b","B.stl"}})
      << d3.request("info",  QJsonObject{});

AnyItem item;
while (waitAnyNext(tasks, item, /*timeoutMs*/ -1, /*breakFlag*/ []{ return g_cancel; })) {
    const auto& msg = item.msg;
    if (msg.status == "event") {
        qDebug() << "T" << item.taskIndex << "event" << msg.payload;
    } else if (msg.status == "done") {
        qDebug() << "T" << item.taskIndex << "done"  << msg.payload;
    } else { // error
        qWarning() << "T" << item.taskIndex << "error" << msg.code << msg.payload;
    }
}
// waitAnyNext 返回 false：表示所有任务都完成并且没有新消息了，或 break/timeout 触发退出
```

---

## 5. Driver 行为规范（oneshot / keepalive）

### 5.1 StdIO 模式 Driver 主循环
- 读 stdin：按 JSONL 逐行解析请求 `{cmd,data}`
- 执行业务：
  - 可输出 0..N 个 `event`
  - 最终必须输出 1 个 `done` 或 `error`
- oneshot：输出 done/error 后退出
- keepalive：输出 done/error 后回到读取下一请求

### 5.2 事件输出建议
- `event`：用于进度、日志、子结果、阶段通知等
- `done`：最终结果（无论成功失败都要有终态；失败用 `error` 终止）
- 一次调用中，允许：
  - 0..N 个 event
  - 1 个 done（成功终态）**或** 1 个 error（失败终态）

---

## 6. 最小参考实现（Driver / Task + waitAnyNext 骨架代码）

> 说明：以下是“接近可直接落地”的 Qt/C++ 骨架，突出 **request→Task** 与 **tryNext/waitNext/waitAnyNext** 的核心路径。  
> 你可以把它拆成 `.h/.cpp` 文件，或直接内联到项目里。

### 6.1 公共结构
```cpp
struct FrameHeader {
    QString status; // "event" | "done" | "error"
    int code = 0;
};

struct Message {
    QString status;
    int code = 0;
    QJsonValue payload;
};

struct TaskState {
    bool terminal = false;        // 是否已收到 done/error（终态已到达）
    int  exitCode = 0;            // 终态 code
    QString errorText;            // 可选：错误文本（来自 payload.message 或内部错误）
    QJsonValue finalPayload;      // 终态 payload（done/error 对应）
    std::deque<Message> queue;    // 待取消息（event/done/error）
};
```

### 6.2 Task 句柄（Future/Promise 风格）
```cpp
class Driver; // fwd

class Task {
public:
    Task() = default;
    Task(Driver* owner, std::shared_ptr<TaskState> s) : drv(owner), st(std::move(s)) {}

    bool isValid() const { return drv != nullptr && (bool)st; }

    // 是否已“彻底完成”：终态已到达，并且消息队列已取空（不会再有新消息）
    bool isDone() const { return st && st->terminal && st->queue.empty(); }

    // 最终 status=done/error 对应的 code（仅在 terminal==true 时有效）
    int  exitCode() const { return (st && st->terminal) ? st->exitCode : -1; }

    // 可选：错误文本（通常来自 payload 的 "message" 字段）
    QString errorText() const { return st ? st->errorText : QString(); }

    QJsonValue finalPayload() const { return st ? st->finalPayload : QJsonValue(); }

    // 非阻塞：每次最多取出 1 条新消息
    bool tryNext(Message& out) {
        if (!st || st->queue.empty()) return false;
        out = std::move(st->queue.front());
        st->queue.pop_front();
        return true;
    }

    // 阻塞：等待下一条消息（或 timeout / done）
    bool waitNext(Message& out, int timeoutMs = -1) {
        if (tryNext(out)) return true;
        if (!st || !drv) return false;
        if (isDone()) return false;

        QEventLoop loop;
        QTimer timer;

        auto quitIfReady = [&]{
            if (drv) drv->pumpStdout();
            if (hasQueued() || isDone()) loop.quit();
        };

        QObject::connect(drv->process(), &QProcess::readyReadStandardOutput, &loop, quitIfReady);
        QObject::connect(drv->process(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                         &loop, quitIfReady);

        if (timeoutMs >= 0) {
            timer.setSingleShot(true);
            QObject::connect(&timer, &QTimer::timeout, &loop, [&]{ loop.quit(); });
            timer.start(timeoutMs);
        }

        loop.exec();
        return tryNext(out);
    }

    bool hasQueued() const { return st && !st->queue.empty(); }

    Driver* owner() const { return drv; }

private:
    Driver* drv = nullptr;                   // task 不应超出 driver 生命周期
    std::shared_ptr<TaskState> st;
};
```

### 6.3 Driver（单进程 + 解析 stdout 推入当前 Task）
```cpp
class Driver {
public:
    bool start(const QString& program, const QStringList& args) {
        proc.setProgram(program);
        proc.setArguments(args);
        proc.setProcessChannelMode(QProcess::SeparateChannels);
        proc.start();
        return proc.waitForStarted(3000);
    }

    void terminate() {
        if (proc.state() != QProcess::NotRunning) {
            proc.terminate();
            if (!proc.waitForFinished(1000)) proc.kill();
        }
    }

    QProcess* process() { return &proc; }

    // 写 1 行请求（cmd + kvArgs），返回 Task
    Task request(const QString& cmd, const QJsonObject& kvArgs = {}) {
        // 协议极简：单 driver 只允许 1 个 inflight
        cur = std::make_shared<TaskState>();

        QJsonObject req;
        req["cmd"] = cmd;
        if (!kvArgs.isEmpty()) req["data"] = kvArgs;

        QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
        line.append('
');
        proc.write(line);
        proc.flush();

        // 重置解析状态机
        waitingHeader = true;
        buf.clear();

        return Task(this, cur);
    }

    // 在 readyReadStandardOutput 里调用：解析 stdout，塞入 cur->queue
    void pumpStdout() {
        if (!cur) return;

        buf.append(proc.readAllStandardOutput());

        QByteArray line;
        while (tryReadLine(line)) {
            if (waitingHeader) {
                FrameHeader tmp;
                if (!parseHeaderLine(line, tmp)) {
                    // 非法 header：直接结束任务
                    pushError(/*code*/1000, QJsonObject{
                        {"message","invalid header"},
                        {"raw", QString::fromUtf8(line)}
                    });
                    return;
                }
                hdr = tmp;
                waitingHeader = false;
            } else {
                // payload 行：允许任意 JSON 值（object/array/string/number/null）
                QJsonValue payload = parseJsonValue(line);
                Message msg{hdr.status, hdr.code, payload};
                cur->queue.push_back(msg);

                if (hdr.status == "done" || hdr.status == "error") {
                    cur->terminal = true;
                    cur->exitCode = hdr.code;
                    cur->finalPayload = payload;

                    if (hdr.status == "error") {
                        cur->errorText = extractErrorText(payload);
                    }
                }

                waitingHeader = true;
            }
        }
    }

    bool hasQueued() const { return cur && !cur->queue.empty(); }
    bool isCurrentTerminal() const { return cur && cur->terminal; }

private:
    QProcess proc;
    QByteArray buf;

    bool waitingHeader = true;
    FrameHeader hdr;
    std::shared_ptr<TaskState> cur;

    bool tryReadLine(QByteArray& outLine) {
        int idx = buf.indexOf('
');
        if (idx < 0) return false;
        outLine = buf.left(idx);
        buf.remove(0, idx + 1);
        return true;
    }

    static bool parseHeaderLine(const QByteArray& line, FrameHeader& out) {
        QJsonParseError err{};
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
        QJsonObject o = doc.object();
        if (!o.contains("status") || !o.contains("code")) return false;
        out.status = o.value("status").toString();
        out.code   = o.value("code").toInt();
        return (out.status == "event" || out.status == "done" || out.status == "error");
    }

    static QJsonValue parseJsonValue(const QByteArray& line) {
        QJsonParseError err{};
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error == QJsonParseError::NoError) {
            if (doc.isObject()) return doc.object();
            if (doc.isArray())  return doc.array();
        }
        // 允许 payload 是 "string"/number/bool/null：这里把无法解析的当作字符串
        return QJsonValue(QString::fromUtf8(line));
    }

    static QString extractErrorText(const QJsonValue& payload) {
        if (payload.isObject()) {
            const auto o = payload.toObject();
            const auto m = o.value("message");
            if (m.isString()) return m.toString();
        }
        if (payload.isString()) return payload.toString();
        return {};
    }

    void pushError(int code, const QJsonObject& payload) {
        Message msg{"error", code, payload};
        cur->queue.push_back(msg);

        cur->terminal = true;
        cur->exitCode = code;
        cur->finalPayload = payload;
        cur->errorText = extractErrorText(payload);
    }
};
```

### 6.4 waitAnyNext：等待“任意任务的新消息”（QEventLoop + readyRead）
```cpp
struct AnyItem {
    int taskIndex = -1;
    Message msg;
};

// breakFlag 返回 true 表示人工跳出；timeoutMs<0 表示无限等待
bool waitAnyNext(QVector<Task>& tasks, AnyItem& out,
                 int timeoutMs = -1,
                 std::function<bool()> breakFlag = {}) {

    // 1) 快速路径：先把已有队列吐一条
    for (int i = 0; i < tasks.size(); ++i) {
        Message m;
        if (tasks[i].tryNext(m)) {
            out.taskIndex = i;
            out.msg = std::move(m);
            return true;
        }
    }

    // 2) 如果全部任务都 isDone()（终态已到达且队列取空），结束
    auto allDone = [&]{
        for (const auto& t : tasks) {
            if (!t.isValid()) continue;
            if (!t.isDone()) return false;
        }
        return true;
    };
    if (allDone()) return false;

    // 3) 没有新消息，但还有未完成任务：用事件循环等“任意一个 Driver 有输出”
    QEventLoop loop;
    QTimer timer;

    QVector<QMetaObject::Connection> conns;

    auto quitIfReady = [&]{
        // 先把各个 driver 的 stdout 都 pump 一次
        for (auto& t : tasks) {
            if (!t.isValid()) continue;
            if (Driver* d = t.owner()) d->pumpStdout();
        }

        bool hasMsg = false;
        for (auto& t : tasks) {
            if (t.isValid() && t.hasQueued()) { hasMsg = true; break; }
        }

        // 有新消息或全部完成 → 退出，让 waitAnyNext 在 loop 外“每次吐 1 条”
        if (hasMsg || allDone()) loop.quit();
    };

    // 连接所有未完成任务的 driver stdout 信号
    for (int i = 0; i < tasks.size(); ++i) {
        auto& t = tasks[i];
        if (!t.isValid() || t.isDone()) continue;
        Driver* d = t.owner();
        if (!d) continue;

        conns.push_back(QObject::connect(d->process(), &QProcess::readyReadStandardOutput,
                                         &loop, quitIfReady));
        conns.push_back(QObject::connect(d->process(),
                                         qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                                         &loop, quitIfReady));
    }

    if (timeoutMs >= 0) {
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, [&]{ loop.quit(); });
        timer.start(timeoutMs);
    }

    // breakFlag：用一个轻量 timer 轮询（避免额外线程）
    QTimer breaker;
    if (breakFlag) {
        breaker.setInterval(30);
        QObject::connect(&breaker, &QTimer::timeout, &loop, [&]{
            if (breakFlag()) loop.quit();
        });
        breaker.start();
    }

    loop.exec();

    for (auto& c : conns) QObject::disconnect(c);

    // 4) loop 退出后再尝试吐一条消息（timeout/break 也可能刚好有新消息）
    for (int i = 0; i < tasks.size(); ++i) {
        Message m;
        if (tasks[i].tryNext(m)) {
            out.taskIndex = i;
            out.msg = std::move(m);
            return true;
        }
    }

    // timeout / breakFlag / allDone 都会走到这里返回 false
    return false;
}
```

> 实战里建议把 `Driver::pumpStdout()` 在信号回调里先调用一次，并在 `waitAnyNext` 里用 “hasQueued” 做快速判断，这样 loop 不会被无意义地 quit。

---

## 7. Console 模式（调试入口 + 直接命令行调用）

### 7.1 CLI 规范（去掉 `--data`，参数直接扁平化）
Console 用于：
- 快速调试 Driver 功能
- 便于上层脚本/工具封装（最终只拿到一次 done 的 payload）

#### 7.1.1 框架保留参数（由 stdiolink 统一处理）
- `--help` / `--version`
- `--mode=console|stdio`
- `--profile=oneshot|keepalive`
- `--cmd=<name>`（命令名）

> 以上参数**不参与** data 构建。

#### 7.1.2 data 参数规则（全部是“命令行第一层参数”）
除保留参数外，所有 `--key=value` 都会被收集进请求的 `data`：

- `--fps=10` → `data.fps = 10`
- `--enable=true` → `data.enable = true`
- `--roi={"x":1,"y":2,"w":3,"h":4}` → `data.roi = {x:1,...}`
- `--ids=[1,2,3]` → `data.ids = [1,2,3]`
- 支持点号嵌套：`--a.b.c=1` → `data.a.b.c = 1`

**类型推断（建议）**：
- `true/false/null` → bool/null
- `整数/小数` → number
- 以 `{` 或 `[` 开头 → JSON 解析
- 其它 → string

#### 7.1.3 data key 与框架参数冲突怎么办？
例如你希望传 `data.mode="frame"`，但 `--mode` 已被框架占用。

约定一个**显式 data 前缀**：`--arg-<key>=<value>`（仍然是第一层参数）。

- `--arg-mode=frame` → `data.mode = "frame"`
- `--arg-profile=fast` → `data.profile = "fast"`（即使与框架字段同名也没歧义）
- `--arg-a.b=1` → `data.a.b = 1`

> 推荐：只在冲突时使用 `--arg-` 前缀，其它情况直接 `--key=value`。

### 7.2 OneShot 调用例子（详细）

#### 7.2.1 Console oneshot（最常用：给脚本/CI 用）
```bash
stdiolink --mode=console --profile=oneshot --cmd=scan --fps=10 --arg-mode=frame
```

请求在 Driver 侧等价于：
```json
{"cmd":"scan","data":{"fps":10,"mode":"frame"}}
```

stdout（建议）：
- 只输出两行：`done/error 头` + `payload`
- 中间过程 event 全部写 stderr（或不输出），避免脚本解析复杂化

示例 stdout：
```json
{"status":"done","code":0}
{"result":{"count":128,"duration_ms":5321}}
```

#### 7.2.2 Host 启动 oneshot Driver（StdIO）并同步等待退出
Host：
1) `QProcess.start(driver, ["--mode=stdio","--profile=oneshot"])`
2) 写 1 行请求：`{"cmd":"scan","data":{...}}\n`
3) 读到 `done/error` 后：
   - `waitForFinished()` 确认进程退出（兜底）
   - 读取 exitCode / 崩溃信息做错误归因

### 7.3 上层封装：duktape JS（推荐）与批处理（可选）

#### 7.3.1 为什么推荐 duktape
- duktape 是**嵌入式 JS 引擎**：你可以做一个极小的 `stdiolink-js` 可执行程序（跨平台编译），把“进程启动 + stdio JSONL 读写 + 事件循环”封装成 JS API。
- 业务侧只写 JS：既可以当脚本跑，也可以当“跨平台 SDK”分发。
- 当然，底层是标准 IO，因此你也可以用 **Python/Node/Go/Rust/.bat/.ps1** 任意语言封装；duktape 只是推荐默认方案。

#### 7.3.2 duktape 封装建议（`stdiolink-js` runner 形态：Driver/Task/next）
推荐做一个 `stdiolink-js`（你们自研的小工具）：
- 内置 duktape
- 内部实现：启动/复用 Driver（keepalive 优先）→ `request()` 写入请求 → 在 stdout 事件里 `pumpStdout()` → 把消息塞入 Task 队列
- 对 JS 暴露的最小 API（建议）：

  - `stdiolink.driver({ program, profile }) -> driver`
  - `driver.request(cmd, kvArgs) -> task`
  - `task.tryNext() -> null | { status, code, payload }`  （非阻塞；每次最多返回 1 条新消息）
  - `task.waitNext(timeoutMs?) -> null | { status, code, payload }`（阻塞；超时/完成则返回 null）
  - `stdiolink.waitAnyNext(tasks, timeoutMs?, breakFlag?) -> null | { index, status, code, payload }`（多任务：每次返回 1 条新消息）

> 这里的 `tryNext/waitNext/waitAnyNext` 设计与 Host(Qt) 一致：同步/异步统一成 “Future/Promise 句柄 + 连续 next”。

**业务脚本示例（JS，单任务）**：
```js
const stdiolink = require("stdiolink");

const d = stdiolink.driver({ program: "scan_driver.exe", profile: "keepalive" });
const t = d.request("scan", { fps: 10, mode: "frame" });

while (true) {
  const msg = t.waitNext(1000);   // 1s 超时；无消息返回 null
  if (!msg) continue;

  if (msg.status === "event") {
    print("event:", JSON.stringify(msg.payload));
  } else if (msg.status === "done") {
    print("done:", JSON.stringify(msg.payload));
    break;
  } else { // error
    print("error:", msg.code, JSON.stringify(msg.payload));
    break;
  }
}
```

**业务脚本示例（JS，多任务并发）**：
```js
const stdiolink = require("stdiolink");

const d1 = stdiolink.driver({ program: "scan_driver.exe", profile: "keepalive" });
const d2 = stdiolink.driver({ program: "mesh_driver.exe", profile: "keepalive" });

const tasks = [
  d1.request("scan", { fps: 10, mode: "frame" }),
  d2.request("mesh.union", { a: "A.stl", b: "B.stl" }),
];

while (true) {
  const it = stdiolink.waitAnyNext(tasks, 1000 /*timeoutMs*/);
  if (!it) break; // 全部完成且无新消息（或 timeout/breakFlag 触发退出）

  if (it.status === "event") {
    print("T" + it.index + " event:", JSON.stringify(it.payload));
  } else if (it.status === "done") {
    print("T" + it.index + " done:", JSON.stringify(it.payload));
  } else {
    print("T" + it.index + " error:", it.code, JSON.stringify(it.payload));
  }
}
```

#### 7.3.3 最小批处理（仅做临时调试）
批处理/PowerShell 的价值主要是“方便点一下就跑”，但不适合做复杂事件流（stderr/stdout 分离、超时、并发等）。
如果只是要拿最终 payload，仍然可以沿用：**console + oneshot → 取最后一行 payload** 的方式。

---

## 8. 推荐落地步骤（最小实现）

1. 先实现 Driver（keepalive 优先）：
   - stdin 读请求 JSONL：`{"cmd","data"}`（无 `id/req/resp`）
   - stdout 写响应 JSONL：**header（status/code）+ 下一行 payload**，允许输出 0..N 次 `event`，最后必须 `done` 或 `error`
   - stderr 只写日志（不要混到 stdout）

2. Host 实现 `Driver + Task`：
   - `Driver.request()` 写入请求后立即返回 `Task`
   - `Driver.pumpStdout()` 解析 stdout，把消息推入 Task 队列
   - `Task.tryNext()` 每次吐 1 条消息（event/done/error），直到完成

3. Host 实现多进程并行：
   - 起 N 个 Driver（一个进程=一个并发槽）
   - 用 `waitAnyNext(tasks)`：`QEventLoop` 等待任意输出 → 每次返回 1 条新消息 → `while(waitAnyNext(...))` 直到全部完成或手动跳出

4. 最后补 Console 模式与脚本封装：
   - Console：`--cmd=<name>` + 扁平化 `--key=value`（构造 data）
   - 推荐：`stdiolink-js`（duktape runner），对脚本暴露 `driver/request/tryNext/waitNext/waitAnyNext`
   - 其它语言封装（Python/Node/Go/Rust/.bat/.ps1）都很容易（因为底层就是可执行程序 + 标准 IO）

---

## 9. 可选增强项（后续再做）
- 超时策略：按命令配置超时（scan 可能 5min，info 可能 3s）
- 统一错误码枚举与文档化（code 的语义要稳定）
- payload 压缩（大结果）或二进制旁路（需要再设计）
- 自动 schema 校验（Host/Driver 共用 JSON Schema）
- 上层封装：
  - 推荐：`stdiolink-js`（duktape runner，内置 stdio 读写与事件循环）
  - 其它语言：Python/Node/Go/Rust 都能非常容易封装（因为底层就是可执行程序 + 标准 IO）
  - 临时调试：.bat / PowerShell / bash（适合 oneshot 取最终 payload）
