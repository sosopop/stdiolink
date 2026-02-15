# 里程碑 70：ProcessGuard 工具类

> **前置条件**: 里程碑 37（InstanceManager）、里程碑 3（Host Driver）已完成
> **目标**: 在核心库中实现 ProcessGuardServer / ProcessGuardClient 工具类，提供基于 QLocalSocket 的父子进程存活监控能力，供 Server→Service 和 Service→Driver 两条链路共用

---

## 1. 目标

- 新增 `ProcessGuardServer` 类：父进程侧创建 QLocalServer，名称格式 `stdiolink_guard_{uuid}`
- 新增 `ProcessGuardClient` 类：子进程侧启动专用监控线程，连接父进程 guard server，断开时调用 `forceFastExit(1)`
- 新增 `forceFastExit(int code)` 跨平台快速退出封装，内部使用平台等价的无清理退出实现（如 `std::_Exit`）
- 提供 `guardName()` 接口，供父进程拼接 `--guard=<name>` 命令行参数
- 提供 `ProcessGuardClient::startFromArgs()` 静态便捷方法，从命令行参数中提取 `--guard` 并启动监控
- 不带 `--guard` 参数时不启动监控，向后兼容
- 完整单元测试覆盖

---

## 2. 背景与问题

- 当前 Server→Service→Driver 三级进程链中，父进程异常退出（崩溃、SIGKILL）后子进程成为孤儿，无法自动清理
- QProcess 的 `finished` 信号仅在父进程正常运行时有效；父进程自身崩溃后信号不会触发
- 操作系统层面（Windows/macOS/Linux）对孤儿进程的回收策略不一致，不能依赖
- 需要一种跨平台、低开销、可靠的父进程存活检测机制

**范围**：仅实现 ProcessGuard 工具类本身，不涉及任何集成改动

**非目标**：
- 不修改 InstanceManager、Driver、ServiceArgs 等现有代码（M71 负责）
- 不实现心跳协议（利用 TCP/pipe 连接断开即可检测）
- 不实现优雅关闭协商（guard 仅做强制退出，优雅关闭由现有 terminate 流程负责）

---

## 3. 技术要点

### 3.1 ProcessGuardServer

```cpp
namespace stdiolink {

class ProcessGuardServer {
public:
    ProcessGuardServer(QObject* parent = nullptr);
    ~ProcessGuardServer();

    bool start();                              // 创建 QLocalServer，自动生成唯一名称
    bool start(const QString& nameOverride);   // 使用指定名称（测试用，可制造冲突）
    void stop();                               // 关闭 server，触发所有客户端断开
    QString guardName() const;                 // 返回 server 名称，用于拼接 --guard=<name>
    bool isListening() const;

private:
    QLocalServer* m_server = nullptr;
    QString m_name;
    QList<QLocalSocket*> m_connections;  // 持有已接受的客户端连接
};

} // namespace stdiolink
```

- 名称格式：`stdiolink_guard_{uuid}`，使用 `QUuid::createUuid().toString(QUuid::WithoutBraces)` 保证唯一
- `start()` 调用 `QLocalServer::listen(m_name)`，失败返回 false
- `start(nameOverride)` 使用调用方指定的名称 listen，用于测试场景（制造名称冲突等）
- 连接 `newConnection` 信号，在回调中调用 `nextPendingConnection()` 接受所有待处理连接，存入 `m_connections` 列表持有（不依赖 Qt 内部队列行为）
- 析构时自动 `stop()`，关闭 server 并销毁所有已持有的 socket，触发客户端断开检测
- 不需要处理客户端发来的数据，仅维持连接存活

### 3.2 ProcessGuardClient

```cpp
namespace stdiolink {

class ProcessGuardClient {
public:
    // 从命令行参数中提取 --guard=<name>，如果存在则启动监控线程
    // 无 --guard 时返回空 unique_ptr
    static std::unique_ptr<ProcessGuardClient> startFromArgs(const QStringList& args);

    explicit ProcessGuardClient(const QString& guardName);
    ~ProcessGuardClient();

    void start();   // 启动监控线程
    void stop();    // 停止监控线程（正常退出时调用，避免误杀）

private:
    QString m_guardName;
    QThread* m_thread = nullptr;
    std::atomic<bool> m_stopped{false};
};

} // namespace stdiolink
```

- `start()` 创建 `QThread`，在线程中：
  1. 创建 `QLocalSocket`，调用 `connectToServer(m_guardName)`
  2. 连接失败 → 立即 `forceFastExit(1)`（父进程已不存在）
  3. 连接成功 → 进入阻塞读循环（`waitForReadyRead(-1)` 或事件循环等待 `disconnected` 信号）
  4. 检测到断开 → 调用 `forceFastExit(1)`
- `forceFastExit(1)` 而非 `exit(1)`：跳过 atexit 和全局析构，避免死锁
- `m_stopped` 原子标志：`stop()` 设置后线程正常退出，不触发 `forceFastExit`
- 析构时调用 `stop()`

### 3.3 --guard 参数约定

- 格式：`--guard=<server_name>`
- 父进程通过 `ProcessGuardServer::guardName()` 获取名称
- 父进程在启动子进程时追加参数：`args << "--guard=" + guard->guardName()`
- 子进程通过 `ProcessGuardClient::startFromArgs(app.arguments())` 一行代码完成接入
- 无 `--guard` 参数时 `startFromArgs` 返回空 unique_ptr，不启动任何监控

### 3.4 线程安全与生命周期

- ProcessGuardServer 生命周期跟随宿主对象（Instance / Driver），宿主销毁时 server 自动关闭
- ProcessGuardClient 在子进程 main() 早期创建，生命周期跟随进程
- 监控线程是守护线程，不阻止进程正常退出（`stop()` 后线程退出）
- `forceFastExit(1)` 是终极手段，仅在父进程死亡时触发

---

## 4. 实现步骤

### 4.1 新增 ProcessGuardServer

1. 创建 `src/stdiolink/guard/process_guard_server.h`，声明类接口
2. 创建 `src/stdiolink/guard/process_guard_server.cpp`，实现：
   - 构造函数：生成 UUID 名称
   - `start()`：创建 QLocalServer，listen(m_name)
   - `start(nameOverride)`：使用指定名称 listen（测试用）
   - 连接 `newConnection` 信号 → `nextPendingConnection()` 全部接受并存入 `m_connections`
   - `stop()`：close server，清理 m_connections
   - `guardName()`：返回 m_name
   - 析构函数：调用 stop()

### 4.2 新增 forceFastExit 封装

1. 创建 `src/stdiolink/guard/force_fast_exit.h`，声明 `void forceFastExit(int code)`
2. 创建 `src/stdiolink/guard/force_fast_exit.cpp`，实现：
   - 内部调用 `std::_Exit(code)`（C++11 标准，跨平台等价于 POSIX `_exit`）
   - 跳过 atexit 回调和全局析构，避免死锁

### 4.3 新增 ProcessGuardClient

1. 创建 `src/stdiolink/guard/process_guard_client.h`，声明类接口
2. 创建 `src/stdiolink/guard/process_guard_client.cpp`，实现：
   - `startFromArgs()`：扫描参数列表，提取 `--guard=`，创建并启动 client
   - `start()`：创建 QThread，在线程函数中连接 socket 并阻塞等待断开
   - `stop()`：设置 m_stopped，断开 socket，等待线程结束
   - 析构函数：调用 stop()

### 4.4 更新 CMakeLists.txt

- 在 `src/stdiolink/CMakeLists.txt` 中新增 `GUARD_SOURCES`
- 添加 `Qt6::Network` 依赖（QLocalServer/QLocalSocket）

### 4.5 单元测试

- 创建 `src/tests/test_process_guard.cpp`

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/stdiolink/guard/process_guard_server.h` — ProcessGuardServer 类声明
- `src/stdiolink/guard/process_guard_server.cpp` — ProcessGuardServer 实现
- `src/stdiolink/guard/process_guard_client.h` — ProcessGuardClient 类声明
- `src/stdiolink/guard/process_guard_client.cpp` — ProcessGuardClient 实现
- `src/stdiolink/guard/force_fast_exit.h` — forceFastExit 跨平台快速退出封装声明
- `src/stdiolink/guard/force_fast_exit.cpp` — forceFastExit 实现

### 5.2 修改文件

- `src/stdiolink/CMakeLists.txt` — 新增 GUARD_SOURCES，添加 Qt6::Network 链接
- `src/tests/CMakeLists.txt` — 新增 test_process_guard.cpp 到 TEST_SOURCES，新增 test_guard_stub 辅助可执行目标（链接 stdiolink），添加 stdiolink_tests 对 test_guard_stub 的依赖

### 5.3 测试文件

- `src/tests/test_process_guard.cpp` — ProcessGuard 单元测试
- `src/tests/test_guard_stub_main.cpp` — guard 集成测试辅助子进程（解析 --guard，启动 ProcessGuardClient，sleep 等待）

---

## 6. 测试与验收

### 6.1 单元测试与进程级验证（必填，重点）

测试对象：`ProcessGuardServer`、`ProcessGuardClient`、`ProcessGuardClient::startFromArgs`

测试文件：`src/tests/test_process_guard.cpp`

桩替身策略：不需要 mock，使用真实 QLocalServer/QLocalSocket 进行进程内验证。涉及 `forceFastExit(1)` 的行为（T03/T07/T08/T14）通过 QProcess 启动辅助子进程做进程级验证，属于集成测试层级。

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| Server::start() | listen 成功 | T01 |
| Server::start() | listen 失败（名称冲突） | T02 |
| Server::stop() | 正常关闭，客户端感知断开 | T03 |
| Server 析构 | 自动 stop | T04 |
| Server::guardName() | 返回格式正确的名称 | T05 |
| Client 连接 | 连接成功，保持存活 | T06 |
| Client 连接 | 连接失败（server 不存在） | T07 |
| Client 断开检测 | server 关闭后 client 检测到断开 | T08 |
| Client::stop() | 正常停止，不触发 forceFastExit | T09 |
| Client 析构 | 自动 stop | T10 |
| startFromArgs | 有 --guard 参数，返回非空 unique_ptr | T11 |
| startFromArgs | 无 --guard 参数，返回空 unique_ptr | T12 |
| startFromArgs | --guard 值为空字符串 | T13 |
| 多客户端 | 多个 client 同时连接同一 server | T14 |

#### 用例详情

**T01 — Server listen 成功**
- 输入：创建 ProcessGuardServer，调用 start()
- 预期：返回 true，isListening() == true，guardName() 非空且以 `stdiolink_guard_` 开头
- 断言：返回值、isListening 状态、名称前缀

**T02 — Server listen 失败（名称冲突）**
- 输入：创建第一个 ProcessGuardServer 并用 `start("fixed_test_name")` 占用名称，再创建第二个并用 `start("fixed_test_name")` 尝试 listen
- 预期：第二个 start() 返回 false，isListening() == false
- 断言：返回值、isListening 状态

**T03 — Server stop 触发客户端断开**
- 输入：启动 server，client 连接成功，调用 server.stop()
- 预期：client 侧 socket 收到 disconnected 信号
- 断言：通过辅助子进程验证退出码为 1

**T04 — Server 析构自动 stop**
- 输入：在作用域内创建 server 并启动，作用域结束后析构
- 预期：server 不再 listening
- 断言：析构后 QLocalSocket 无法连接该名称

**T05 — guardName 格式**
- 输入：创建 server，调用 guardName()
- 预期：格式为 `stdiolink_guard_{uuid}`，uuid 部分长度 >= 8
- 断言：正则匹配 `^stdiolink_guard_[0-9a-f-]+$`

**T06 — Client 连接成功保持存活**
- 输入：启动 server，创建 client 并 start()，等待 200ms
- 预期：client 线程存活，未触发 forceFastExit
- 断言：进程仍在运行（通过辅助子进程验证）

**T07 — Client 连接失败（server 不存在）**
- 输入：创建 client 指向不存在的 guard name，start()
- 预期：子进程以退出码 1 退出
- 断言：通过 QProcess 启动辅助子进程，验证 exitCode == 1

**T08 — Client 检测 server 关闭**
- 输入：启动 server + client，client 连接成功后销毁 server
- 预期：子进程以退出码 1 退出
- 断言：通过 QProcess 启动辅助子进程，验证 exitCode == 1

**T09 — Client stop 正常退出**
- 输入：启动 server + client，调用 client.stop()
- 预期：监控线程退出，不触发 forceFastExit，进程继续运行
- 断言：stop() 返回后进程仍存活

**T10 — Client 析构自动 stop**
- 输入：在作用域内创建 client 并启动，作用域结束后析构
- 预期：不触发 forceFastExit
- 断言：析构后进程仍存活

**T11 — startFromArgs 有 --guard**
- 输入：`startFromArgs({"app", "--guard=stdiolink_guard_test123"})`
- 预期：返回非空 unique_ptr
- 断言：返回值非空

**T12 — startFromArgs 无 --guard**
- 输入：`startFromArgs({"app", "--config.key=val"})`
- 预期：返回空 unique_ptr
- 断言：返回值为空

**T13 — startFromArgs --guard 值为空**
- 输入：`startFromArgs({"app", "--guard="})`
- 预期：返回空 unique_ptr（空名称无意义）
- 断言：返回值为空

**T14 — 多客户端连接同一 server**
- 输入：启动 server，创建 2 个 client 连接
- 预期：两个 client 均连接成功，server stop 后两个 client 均检测到断开
- 断言：两个辅助子进程均以退出码 1 退出

覆盖要求：所有 14 条路径均有对应用例，100% 可达路径覆盖。

不可达路径说明：
- `ProcessGuardClient::start()` 在 `m_thread` 已存在时的重复调用：通过前置条件约束（start 仅调用一次），属于 API 误用，不做防御分支。

### 6.2 集成测试（按需）

- 跨进程验证：通过 QProcess 启动辅助子进程（`test_guard_stub_main.cpp`），验证父进程退出后子进程自动终止
- 辅助子进程逻辑：调用 `ProcessGuardClient::startFromArgs()` 解析 `--guard` 参数，启动监控，然后 sleep 等待

### 6.3 验收标准

- [ ] ProcessGuardServer 能成功创建 QLocalServer 并返回唯一名称
- [ ] ProcessGuardClient 能连接到 guard server 并保持存活
- [ ] Server 关闭/析构后，Client 在 1 秒内检测到断开并调用 forceFastExit(1)
- [ ] Client 连接不存在的 server 时立即 forceFastExit(1)
- [ ] startFromArgs 正确解析 --guard 参数，无参数时返回空 unique_ptr
- [ ] Client::stop() 能正常停止监控线程，不触发 forceFastExit
- [ ] Windows / macOS / Linux 三平台编译通过
- [ ] 所有 14 条测试用例通过

---

## 7. 风险与控制

- 风险：QLocalServer 名称在某些平台上有长度限制（如 Unix domain socket 路径 108 字节）
  - 控制：UUID 使用 WithoutBraces 格式（36 字符），加前缀总长约 53 字符，远低于限制

- 风险：Windows 上 QLocalSocket 断开检测延迟
  - 控制：Windows named pipe 断开检测通常在毫秒级；测试中设置合理超时（1 秒）

- 风险：forceFastExit 跳过资源清理可能导致临时文件残留
  - 控制：guard 触发的场景是父进程已死，此时子进程的临时文件由 OS 回收；正常退出路径不经过 forceFastExit

- 风险：监控线程与主线程竞争（stop 与 forceFastExit 竞态）
  - 控制：m_stopped 使用 std::atomic，stop() 先设置标志再断开 socket，线程检查标志后决定是否 forceFastExit

---

## 8. 里程碑完成定义（DoD）

- [ ] ProcessGuardServer / ProcessGuardClient 代码完成并通过编译
- [ ] 14 条单元测试全部通过
- [ ] 跨进程集成测试通过（辅助子进程验证）
- [ ] CMakeLists.txt 更新，Qt6::Network 依赖添加
- [ ] 三平台（Windows/macOS/Linux）CI 编译通过
- [ ] 向后兼容：无 --guard 参数时 guard 相关路径行为不变
