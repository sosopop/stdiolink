# 里程碑 71：ProcessGuard 全链路集成与终止简化

> **前置条件**: 里程碑 70（ProcessGuard 工具类）已完成
> **目标**: 将 ProcessGuard 集成到 Server→Service 和 Service→Driver 两条链路，简化终止流程为直接 kill()，补齐 spawn timeoutMs，无 --guard 时 guard 相关路径行为不变

---

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| stdiolink 核心库 | Driver::start() 创建 guard 并通过 `--guard` 传参；Driver::terminate() 改为直接 kill()；ConsoleArgs 识别 `--guard` 为框架参数（兼容 DriverCore） |
| stdiolink_service | ServiceArgs 解析 --guard；main.cpp 启动 ProcessGuardClient |
| stdiolink_server | InstanceManager::startInstance() 创建 guard 并通过 `--guard` CLI 传参；terminateInstance() 改为直接 kill()；DriverLabWsConnection::stopDriver() 改为直接 kill() |
| JS 运行时 | spawn options 新增 timeoutMs 参数，超时后 kill() |

- Server→Service 链路：InstanceManager 创建 ProcessGuardServer，通过 `--guard=<name>` 传给 Service 进程
- Service→Driver 链路：Driver::start() 创建 ProcessGuardServer，通过 `--guard=<name>` 传给 Driver 子进程
- Service 进程：解析 `--guard` 参数，启动 ProcessGuardClient 监控线程
- Driver 进程：通过 ProcessGuardClient::startFromArgs() 接入（Driver 为用户编写的程序，由用户自行调用；stdiolink 内置测试 Driver 作为示范）
- 终止简化：Server 终止 Service、Service 终止 Driver 均改为直接 kill()，不保留优雅窗口
- spawn 超时：options 新增 `timeoutMs`，超时后直接 kill() 子进程
- DriverLab WebSocket 连接：DriverLabWsConnection::stopDriver() 同步简化为直接 kill()（DriverLab 直接管理 QProcess，不经过 Driver 类）
- 不带 `--guard` 参数时 guard 相关路径行为不变
- ConsoleArgs 将 `guard` 加入框架参数白名单，使 `--guard` 被 DriverCore 识别为框架参数而非 data 参数

---

## 2. 背景与问题

- M70 提供了 ProcessGuard 工具类，但尚未集成到任何进程启动/终止流程中
- 当前 InstanceManager::terminateInstance() 和 Driver::terminate() 使用三阶段优雅关闭（closeWriteChannel → terminate → kill），在 guard 机制下不再需要优雅窗口
- 本里程碑将终止策略从"优雅优先"调整为"直接 kill"，属于有意行为变更，目标是简化异常终止路径并降低挂死风险
- spawn() 缺少超时机制，长时间运行的子进程无法自动回收
- 需要在明确行为变更边界的前提下完成集成

**范围**：
- InstanceManager guard 集成与终止简化
- Driver guard 集成与终止简化
- DriverLabWsConnection 终止简化
- ServiceArgs --guard 解析
- Service main.cpp guard client 启动
- spawn timeoutMs

**非目标**：
- 不修改 js_process_async 的 execAsync（已有 timeoutMs 支持）
- 不修改 ScheduleEngine 调度逻辑
- 不修改 WebUI

---

## 3. 技术要点

### 3.1 Server→Service 链路

```
InstanceManager::startInstance()
  ├─ 创建 ProcessGuardServer（生命周期绑定 Instance）
  ├─ guard.start() → 失败则设置 error 并返回空 instanceId
  ├─ 拼接参数：args << "--guard=" + guard.guardName()
  ├─ proc->start()
  └─ Instance 持有 guard 指针

Instance 销毁时
  └─ guard 析构 → server 关闭 → Service 进程感知断开 → forceFastExit(1)
```

Instance 模型新增字段：

```cpp
struct Instance {
    // ... 现有字段 ...
    std::unique_ptr<stdiolink::ProcessGuardServer> guard;
};
```

### 3.2 Service→Driver 链路

```
Driver::start(program, args)
  ├─ 创建 ProcessGuardServer（生命周期绑定 Driver 成员）
  ├─ guard.start() → 失败则返回 false
  ├─ 拼接参数：finalArgs = args + ["--guard=" + guard.guardName()]
  ├─ m_proc.setArguments(finalArgs)
  └─ m_proc.start()

Driver 析构时
  └─ guard 析构 → server 关闭 → Driver 子进程感知断开 → forceFastExit(1)
```

`--guard` 作为框架参数已纳入 ConsoleArgs 白名单（3.7 节），基于 DriverCore 的 Driver 不会将其误解析为 data。

`--guard` 属于 stdiolink Driver 协议的一部分，项目支持范围内的 Driver 必须兼容该参数。非 stdiolink 或不兼容 `--guard` 的外部 Driver 不在本里程碑支持范围内。

Driver 类新增成员：

```cpp
class Driver {
    // ... 现有成员 ...
    std::unique_ptr<ProcessGuardServer> m_guard;
};
```

### 3.3 终止简化

设计理念：Driver 和 Service 应在具体业务命令内自行完成并退出，不依赖外部终止信号。一旦需要外部终止，即视为异常情况，直接 kill 是最安全的做法——避免优雅关闭窗口内引发的二次问题（死锁、挂起、资源泄漏）。

**InstanceManager::terminateInstance() 改动前：**
```cpp
proc->closeWriteChannel();
if (!proc->waitForFinished(200)) {
    proc->terminate();
    if (!proc->waitForFinished(200)) {
        proc->kill();
    }
}
```

**改动后：**
```cpp
proc->kill();
proc->waitForFinished(1000);
```

**Driver::terminate() 改动前：**
```cpp
m_proc.closeWriteChannel();
if (!m_proc.waitForFinished(100)) {
    m_proc.terminate();
    if (!m_proc.waitForFinished(100)) {
        m_proc.kill();
        m_proc.waitForFinished(100);
    }
}
```

**改动后：**
```cpp
m_proc.kill();
m_proc.waitForFinished(1000);
```

同步简化 `InstanceManager::waitAllFinished()` 中的强制终止逻辑。

**DriverLabWsConnection::stopDriver() 改动前：**
```cpp
m_process->closeWriteChannel();
if (!m_process->waitForFinished(2000)) {
    m_process->terminate();
    if (!m_process->waitForFinished(2000)) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}
```

**改动后：**
```cpp
m_process->kill();
m_process->waitForFinished(1000);
```

注意：DriverLabWsConnection 直接管理 QProcess（非通过 Driver 类），因此需要独立简化。

### 3.4 ServiceArgs --guard 解析

在 `ServiceArgs::parse()` 中新增对 `--guard=` 的识别：

```cpp
if (arg.startsWith("--guard=")) {
    result.guardName = arg.mid(8);  // len("--guard=") == 8
    continue;
}
```

ParseResult 新增字段：

```cpp
struct ParseResult {
    // ... 现有字段 ...
    QString guardName;  // --guard=<name>，为空表示未指定
};
```

### 3.5 Service main.cpp guard client 启动

在 `QCoreApplication` 创建后、业务逻辑前启动 guard client：

```cpp
QCoreApplication app(argc, argv);
auto parsed = ServiceArgs::parse(app.arguments());

// Guard client（必须在业务逻辑前启动）
std::unique_ptr<stdiolink::ProcessGuardClient> guardClient;
if (!parsed.guardName.isEmpty()) {
    guardClient.reset(new stdiolink::ProcessGuardClient(parsed.guardName));
    guardClient->start();
}
```

正常退出前调用 `guardClient->stop()` 避免误杀。

### 3.6 spawn timeoutMs

在 `jsSpawn()` 的 options 中新增 `timeoutMs` 支持：

```js
const handle = spawn("long_running", [], { timeoutMs: 30000 });
```

实现：
- allowedKeys 新增 `"timeoutMs"`
- 解析 timeoutMs 值：与 execAsync 保持一致的宽松策略——`JS_IsNumber` 为 true 时取整，正值生效，0/负值/非数字均视为无超时
- 创建 QTimer，超时后调用 `proc->kill()` 并触发 onExit 回调
- Timer 生命周期绑定 ProcessHandleData，进程退出或 handle 销毁时取消 timer

与 execAsync 的 timeoutMs 行为一致：超时直接 kill，不做 SIGTERM 优雅等待；非法值宽松忽略而非抛异常。

### 3.7 ConsoleArgs 框架参数白名单

将 `guard` 加入 `SystemOptionRegistry` 框架参数列表，使 ConsoleArgs 识别 `--guard=<name>` 为框架参数而非 data 参数。这确保基于 DriverCore::run(argc, argv) 的 Driver 收到 `--guard` 时不会报错或将其误入 data。

ConsoleArgs::parseFrameworkArg() 新增分支：
```cpp
} else if (key == "guard") {
    // 识别但不处理，guard client 由 ProcessGuardClient::startFromArgs() 负责
}
```

### 3.8 测试注入策略

InstanceManager 和 Driver 内部自动创建 ProcessGuardServer 并使用 UUID 命名，测试无法预知名称以制造冲突。为使 T02/T06（guard.start() 失败路径）稳定可执行，需提供测试注入点：

- `InstanceManager::setGuardNameForTesting(const QString& name)`：设置后 startInstance() 内部创建的 guard 使用指定名称而非 UUID
- `Driver::setGuardNameForTesting(const QString& name)`：设置后 start() 内部创建的 guard 使用指定名称

测试流程：
1. 用 `ProcessGuardServer::start("conflict_name")` 预先占用名称
2. 调用 `setGuardNameForTesting("conflict_name")`
3. 调用 startInstance() / start() → guard.start() 必定失败 → 验证错误处理路径

生产环境不调用 setGuardNameForTesting，默认 UUID 自动命名。注入方法通过编译宏 `STDIOLINK_TESTING` 控制可见性（仅测试构建定义该宏），生产构建中方法不存在，避免 API 污染和误用。

### 3.9 向后兼容与支持边界

- 无 `--guard` 参数时：ServiceArgs.guardName 为空，不创建 guard client，所有行为不变
- Driver::start() 始终创建 guard 并追加 `--guard` 参数，但子进程是否处理取决于子进程自身
- 基于 DriverCore 的 Driver：ConsoleArgs 识别 `--guard` 为框架参数并忽略，不影响业务逻辑
- `--guard` 属于 stdiolink Driver 协议的一部分，项目支持范围内的 Driver 必须兼容该参数
- 非 stdiolink 或不兼容 `--guard` 的外部 Driver 不在本里程碑支持范围内
- spawn timeoutMs 缺省时无超时，与现有行为一致

---

## 4. 实现步骤

### 4.1 Instance 模型扩展

- 修改 `src/stdiolink_server/model/instance.h`：新增 `std::unique_ptr<stdiolink::ProcessGuardServer> guard` 字段
- 添加 `#include "stdiolink/guard/process_guard_server.h"`

### 4.2 InstanceManager guard 集成与终止简化

- 修改 `src/stdiolink_server/manager/instance_manager.h`：新增 `setGuardNameForTesting(const QString&)` 方法声明（`#ifdef STDIOLINK_TESTING` 包裹），新增 `m_guardNameOverride` 成员
- 修改 `src/stdiolink_server/manager/instance_manager.cpp`：
  - `startInstance()`：在 `proc->start()` 前创建 ProcessGuardServer，若 m_guardNameOverride 非空则使用 `start(m_guardNameOverride)` 否则 `start()`；失败则设置 error 并返回空 instanceId；成功则拼接 `--guard=` 参数，存入 Instance
  - `terminateInstance()`：替换三阶段关闭为 `proc->kill(); proc->waitForFinished(1000);`
  - `waitAllFinished()`：简化强制终止逻辑

### 4.3 Driver guard 集成与终止简化

- 修改 `src/stdiolink/host/driver.h`：新增 `std::unique_ptr<ProcessGuardServer> m_guard` 成员，新增 `setGuardNameForTesting(const QString&)` 方法声明（`#ifdef STDIOLINK_TESTING` 包裹），新增 `m_guardNameOverride` 成员，添加 include
- 修改 `src/stdiolink/host/driver.cpp`：
  - `start()`：创建 ProcessGuardServer，若 m_guardNameOverride 非空则使用 `start(m_guardNameOverride)` 否则 `start()`；失败则返回 false；将 `--guard=<guardName>` 追加到参数列表
  - `terminate()`：替换三阶段关闭为 `m_proc.kill(); m_proc.waitForFinished(1000);`

### 4.3.1 DriverLabWsConnection 终止简化

- 修改 `src/stdiolink_server/http/driverlab_ws_connection.cpp`：
  - `stopDriver()`：替换三阶段关闭为 `m_process->kill(); m_process->waitForFinished(1000);`

### 4.4 ConsoleArgs 框架参数白名单

- 修改 `src/stdiolink/console/system_options.cpp`：在 `options()` 列表中新增 `{"guard", "", "<name>", "Process guard name", {}, "", true}`
- 修改 `src/stdiolink/console/console_args.cpp`：`parseFrameworkArg()` 新增 `guard` 分支（识别但不存储，仅防止误入 data）

### 4.5 ServiceArgs --guard 解析

- 修改 `src/stdiolink_service/config/service_args.h`：ParseResult 新增 `guardName` 字段
- 修改 `src/stdiolink_service/config/service_args.cpp`：parse() 中新增 `--guard=` 分支

### 4.6 Service main.cpp guard client 启动

- 修改 `src/stdiolink_service/main.cpp`：
  - 添加 `#include "stdiolink/guard/process_guard_client.h"`
  - 在 ServiceArgs::parse() 后创建 ProcessGuardClient（如果 guardName 非空）
  - 在 return 前调用 guardClient->stop()

### 4.7 spawn timeoutMs

- 修改 `src/stdiolink_service/bindings/js_process_async.cpp`：
  - `jsSpawn()` 的 allowedKeys 新增 `"timeoutMs"`
  - 解析 timeoutMs 选项（与 execAsync 一致的宽松策略）
  - 创建 QTimer 绑定到 ProcessHandleData
  - 超时回调：kill 进程

### 4.8 CMakeLists.txt 更新

- 修改 `src/stdiolink_server/CMakeLists.txt`：确保链接 `Qt6::Network`（通过 stdiolink 库传递依赖）
- 修改 `src/tests/CMakeLists.txt`：
  - TEST_SOURCES 新增 `test_process_guard_integration.cpp`
  - 确认 M70 已创建的 `test_guard_stub` 辅助可执行目标可用
  - 添加 `stdiolink_tests` 对 `test_guard_stub` 的构建依赖
  - 为 `stdiolink_tests` 目标添加 `target_compile_definitions(stdiolink_tests PRIVATE STDIOLINK_TESTING)`

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/tests/test_process_guard_integration.cpp` — 全链路集成测试

（`test_guard_stub_main.cpp` 已在 M70 中创建）

### 5.2 修改文件

- `src/stdiolink/host/driver.h` — 新增 m_guard 成员、setGuardNameForTesting 方法、m_guardNameOverride 成员
- `src/stdiolink/host/driver.cpp` — start() 创建 guard 并通过 --guard 传参，terminate() 改为直接 kill
- `src/stdiolink/console/system_options.cpp` — options() 新增 guard 框架参数
- `src/stdiolink/console/console_args.cpp` — parseFrameworkArg() 新增 guard 分支
- `src/stdiolink_server/model/instance.h` — 新增 guard 字段
- `src/stdiolink_server/manager/instance_manager.h` — 新增 setGuardNameForTesting 方法、m_guardNameOverride 成员
- `src/stdiolink_server/manager/instance_manager.cpp` — startInstance() 创建 guard 传参（guard.start() 失败则返回错误），terminateInstance() 改为直接 kill
- `src/stdiolink_service/config/service_args.h` — ParseResult 新增 guardName
- `src/stdiolink_service/config/service_args.cpp` — parse() 新增 --guard= 分支
- `src/stdiolink_service/main.cpp` — 启动 ProcessGuardClient
- `src/stdiolink_service/bindings/js_process_async.cpp` — spawn 新增 timeoutMs
- `src/stdiolink_server/http/driverlab_ws_connection.cpp` — stopDriver() 改为直接 kill
- `src/stdiolink_server/CMakeLists.txt` — 确保 Qt6::Network 链接
- `src/tests/CMakeLists.txt` — 新增 test_process_guard_integration.cpp 到 TEST_SOURCES，确认 test_guard_stub 依赖

### 5.3 测试文件（修改）

- `src/tests/test_service_args.cpp` — 补充 --guard 解析用例
- `src/tests/test_process_async_binding.cpp` — 补充 spawn timeoutMs 用例
- `src/tests/test_host_driver.cpp` — 补充 guard 参数传递验证
- `src/tests/test_instance_manager.cpp` — 补充 guard 参数传递与直接 kill 验证
- `src/tests/test_driverlab_ws_handler.cpp` — 补充 stopDriver 直接 kill 验证

---

## 6. 测试与验收

### 6.1 测试矩阵（单元 + 集成）

测试对象：InstanceManager guard 集成、Driver guard 集成、DriverLabWsConnection 终止简化、ServiceArgs --guard 解析、spawn timeoutMs

桩替身策略：
- InstanceManager 测试使用 `test_service_stub_main`（现有辅助进程）验证参数传递
- Driver 测试使用 `test_driver_main`（现有辅助进程）验证参数传递
- spawn timeoutMs 测试使用 `test_process_async_stub_main`（现有辅助进程）

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| InstanceManager::startInstance() | guard 创建成功，--guard 参数出现在子进程命令行 | T01 |
| InstanceManager::startInstance() | guard.start() 失败 → 返回错误，不启动进程 | T02 |
| InstanceManager::terminateInstance() | 直接 kill，进程在 1s 内退出 | T03 |
| Instance 销毁 | guard server 关闭 | T04 |
| Driver::start() | guard 创建成功，--guard 参数出现在子进程参数列表 | T05 |
| Driver::start() | guard.start() 失败 → 返回 false，不启动进程 | T06 |
| Driver::terminate() | 直接 kill，进程在 1s 内退出 | T07 |
| Driver 析构 | guard server 关闭 | T08 |
| ServiceArgs::parse() | 有 --guard=name → guardName == "name" | T09 |
| ServiceArgs::parse() | 无 --guard → guardName 为空 | T10 |
| ServiceArgs::parse() | --guard= 空值 → guardName 为空字符串 | T11 |
| ServiceArgs::parse() | --guard 与其他参数混合 | T12 |
| Service main.cpp | 有 guardName → 创建 ProcessGuardClient | T13（集成） |
| Service main.cpp | 无 guardName → 不创建 client | T14（集成） |
| ConsoleArgs | --guard=xxx 不报错，不进入 data | T15 |
| spawn timeoutMs | 无 timeoutMs → 无超时（现有行为） | T16 |
| spawn timeoutMs | timeoutMs=0 → 无超时 | T17 |
| spawn timeoutMs | timeoutMs > 0，进程在超时前退出 → 正常 onExit | T18 |
| spawn timeoutMs | timeoutMs > 0，进程超时 → kill 并触发 onExit | T19 |
| spawn timeoutMs | timeoutMs 负值/非数字 → 宽松忽略，视为无超时 | T20 |
| spawn timeoutMs | handle.kill() 在超时前调用 → timer 取消 | T21 |
| spawn allowedKeys | 未知 key 仍报错 | T22 |
| 向后兼容 | 无 --guard 参数时 Service 正常运行 | T23 |
| DriverLabWsConnection::stopDriver() | 直接 kill，进程在 1s 内退出 | T24 |

#### 用例详情

**T01 — InstanceManager startInstance guard 参数传递**
- 输入：调用 startInstance()，检查 Instance 的 commandLine
- 预期：commandLine 中包含 `--guard=stdiolink_guard_*` 格式的参数
- 断言：参数列表中存在匹配 `--guard=stdiolink_guard_` 前缀的项

**T02 — InstanceManager startInstance guard 失败**
- 前置：调用 `setGuardNameForTesting("conflict_name")`，并用 `ProcessGuardServer::start("conflict_name")` 预先占用名称
- 输入：调用 startInstance()
- 预期：startInstance() 返回空 instanceId，error 非空，不启动子进程
- 断言：返回值为空，error 包含 guard 相关信息，无进程被创建

**T03 — InstanceManager terminateInstance 直接 kill**
- 输入：启动 Instance，调用 terminateInstance()
- 预期：进程在 1 秒内终止，不经过 closeWriteChannel/terminate 阶段
- 断言：proc->state() == NotRunning，耗时 < 2s

**T04 — Instance 销毁关闭 guard**
- 输入：启动 Instance，从 map 中移除（触发 unique_ptr 析构）
- 预期：guard server 不再 listening
- 断言：QLocalSocket 无法连接该 guard name

**T05 — Driver start guard 参数传递**
- 输入：调用 Driver::start("test_driver_main", {})
- 预期：子进程实际收到的参数中包含 `--guard=stdiolink_guard_*`
- 断言：m_proc.arguments() 中存在匹配项

**T06 — Driver start guard 失败**
- 前置：调用 `setGuardNameForTesting("conflict_name")`，并用 `ProcessGuardServer::start("conflict_name")` 预先占用名称
- 输入：调用 Driver::start()
- 预期：Driver::start() 返回 false，不启动子进程
- 断言：返回值 false，isRunning() == false

**T07 — Driver terminate 直接 kill**
- 输入：启动 Driver，调用 terminate()
- 预期：进程在 1 秒内终止
- 断言：isRunning() == false

**T08 — Driver 析构关闭 guard**
- 输入：在作用域内创建 Driver 并 start，作用域结束
- 预期：guard server 关闭
- 断言：析构后子进程终止

**T09 — ServiceArgs 有 --guard**
- 输入：`parse({"app", "svc_dir", "--guard=stdiolink_guard_abc123"})`
- 预期：guardName == "stdiolink_guard_abc123"
- 断言：字段值精确匹配

**T10 — ServiceArgs 无 --guard**
- 输入：`parse({"app", "svc_dir", "--config-file=test.json"})`
- 预期：guardName.isEmpty() == true
- 断言：字段为空

**T11 — ServiceArgs --guard 空值**
- 输入：`parse({"app", "svc_dir", "--guard="})`
- 预期：guardName == ""（空字符串）
- 断言：字段为空字符串

**T12 — ServiceArgs --guard 与其他参数混合**
- 输入：`parse({"app", "svc_dir", "--config-file=f.json", "--guard=g1", "--config.key=val"})`
- 预期：guardName == "g1"，configFilePath == "f.json"，rawConfigValues 包含 key
- 断言：三个字段均正确

**T15 — ConsoleArgs --guard 框架参数兼容**
- 输入：构造 ConsoleArgs，parse `{"app", "--guard=stdiolink_guard_test", "--cmd=echo", "--key=val"}`
- 预期：parse 返回 true，cmd == "echo"，data 中包含 key 但不包含 guard
- 断言：parse 成功，guard 不进入 data 对象

**T16 — spawn 无 timeoutMs**
- 输入：`spawn("echo_prog", ["hello"])`，无 options
- 预期：进程正常运行，无超时限制
- 断言：onExit 正常触发，exitCode == 0

**T17 — spawn timeoutMs=0**
- 输入：`spawn("echo_prog", ["hello"], { timeoutMs: 0 })`
- 预期：等同于无超时
- 断言：进程正常退出

**T18 — spawn timeoutMs 进程提前退出**
- 输入：`spawn("echo_prog", ["hello"], { timeoutMs: 5000 })`，进程 100ms 内退出
- 预期：onExit 正常触发，timer 自动取消
- 断言：exitCode == 0，无超时错误

**T19 — spawn timeoutMs 超时 kill**
- 输入：`spawn("sleep_prog", ["60"], { timeoutMs: 200 })`
- 预期：200ms 后进程被 kill，onExit 触发
- 断言：进程在 500ms 内退出，exitCode 非 0 或 exitStatus == CrashExit

**T20 — spawn timeoutMs 非法值宽松忽略**
- 输入：`spawn("echo_prog", ["hello"], { timeoutMs: -1 })` / `spawn("echo_prog", ["hello"], { timeoutMs: "abc" })`
- 预期：不抛异常，视为无超时，进程正常运行和退出
- 断言：onExit 正常触发，exitCode == 0（与 execAsync 行为一致）

**T21 — spawn handle.kill 在超时前取消 timer**
- 输入：`spawn("sleep_prog", ["60"], { timeoutMs: 5000 })`，立即调用 `handle.kill()`
- 预期：进程被 kill，timer 取消，不会二次 kill
- 断言：onExit 只触发一次

**T22 — spawn 未知 key 仍报错**
- 输入：`spawn("prog", [], { timeoutMs: 1000, unknownKey: true })`
- 预期：TypeError，消息包含 "unknownKey"
- 断言：JS 异常

**T23 — 向后兼容：无 --guard 时 Service 正常**
- 输入：以现有方式启动 Service（不传 --guard）
- 预期：所有现有功能正常
- 断言：与 M70 前行为一致

**T24 — DriverLabWsConnection stopDriver 直接 kill**
- 输入：通过 DriverLabWsConnection 启动 Driver 进程，调用 stopDriver()
- 预期：进程在 1 秒内终止，不经过 closeWriteChannel/terminate 阶段
- 断言：m_process->state() == NotRunning，耗时 < 2s

覆盖要求：所有 24 条路径均有对应用例，100% 可达路径覆盖。

不可达路径说明：
- `ProcessGuardClient::start()` 在 `m_thread` 已存在时的重复调用：通过前置条件约束（start 仅调用一次），属于 API 误用，不做防御分支。

### 6.2 集成测试

**T13 — Service main 有 guard 启动 client**
- 输入：通过 QProcess 启动 stdiolink_service 并传入 --guard=<valid_server>
- 预期：Service 进程正常启动，guard server 关闭后 Service 进程退出
- 断言：exitCode == 1

**T14 — Service main 无 guard 正常运行**
- 输入：通过 QProcess 启动 stdiolink_service 不传 --guard
- 预期：Service 进程正常运行和退出
- 断言：行为与 M70 前一致

**端到端验证：**
- **Server→Service 全链路**：启动 InstanceManager，创建 Instance，验证 Service 进程收到 --guard 参数，终止 Instance 后 Service 进程退出
- **Service→Driver 全链路**：通过 JS 脚本启动 Driver，验证 Driver 进程收到 --guard 参数
- **父进程崩溃模拟**：启动 Server→Service 链路，kill Server 进程，验证 Service 在 1 秒内退出

### 6.3 验收标准

- [ ] InstanceManager::startInstance() 创建的 Instance 包含 guard，子进程命令行含 --guard
- [ ] InstanceManager::startInstance() 在 guard.start() 失败时返回错误，不启动子进程
- [ ] InstanceManager::terminateInstance() 直接 kill，不保留优雅窗口
- [ ] Driver::start() 创建 guard，子进程参数含 --guard
- [ ] Driver::start() 在 guard.start() 失败时返回 false
- [ ] Driver::terminate() 直接 kill，不保留优雅窗口
- [ ] DriverLabWsConnection::stopDriver() 直接 kill，不保留优雅窗口
- [ ] ConsoleArgs 识别 --guard 为框架参数，不误入 data
- [ ] ServiceArgs 正确解析 --guard 参数
- [ ] Service main.cpp 在有 --guard 时启动 ProcessGuardClient
- [ ] spawn timeoutMs 超时后 kill 子进程
- [ ] spawn timeoutMs 缺省/0/负值/非数字时无超时（与 execAsync 一致的宽松策略）
- [ ] 不带 --guard 参数时 guard 相关路径行为不变（终止策略变更为有意行为变更，不属于兼容性回归）
- [ ] 所有 24 条测试用例通过

---

## 7. 风险与控制

- 风险：直接 kill() 替代优雅关闭可能导致 Service/Driver 正在写入的文件损坏
  - 控制：设计上 Driver/Service 应在业务命令内自行完成并退出，外部终止本身即为异常场景。直接 kill 避免优雅关闭窗口内的二次问题（死锁、挂起）。JS 脚本的文件写入应使用原子写入模式
  - 控制：业务层不得依赖终止信号触发关键清理逻辑
  - 控制：回归验证需覆盖"直接 kill 下的数据一致性"场景

- 风险：guard.start()（QLocalServer::listen）可能因权限、资源、路径等原因失败
  - 控制：InstanceManager::startInstance() 和 Driver::start() 均将 guard.start() 失败视为硬错误，返回失败状态，不启动子进程。T02/T06 用例覆盖此路径

- 风险：spawn timeoutMs 与 handle.kill() 竞态
  - 控制：timer 回调中检查 proc->state()，已退出则跳过；handle.kill() 中取消 timer

- 风险：ServiceArgs 新增 --guard 可能影响现有参数解析顺序
  - 控制：--guard 作为独立分支插入，不影响其他参数的解析逻辑；T12 用例验证混合参数场景

- 风险：ConsoleArgs 不识别 --guard 导致现有 DriverCore::run(argc, argv) 的 Driver 报错
  - 控制：将 guard 加入 SystemOptionRegistry 框架参数白名单，ConsoleArgs 识别后忽略；T15 用例验证

---

## 8. 里程碑完成定义（DoD）

- [ ] InstanceManager guard 集成代码完成（含 guard.start() 失败处理、setGuardNameForTesting 注入点）
- [ ] Driver guard 集成代码完成（--guard 传参，含 guard.start() 失败处理、setGuardNameForTesting 注入点）
- [ ] ConsoleArgs 框架参数白名单更新（guard 加入 SystemOptionRegistry）
- [ ] ServiceArgs --guard 解析完成
- [ ] Service main.cpp guard client 启动完成
- [ ] 终止流程简化为直接 kill()（InstanceManager、Driver、DriverLabWsConnection）
- [ ] spawn timeoutMs 实现完成（与 execAsync 一致的宽松策略）
- [ ] 所有 24 条测试用例通过（含单元测试 + 集成测试）
- [ ] 集成测试（跨进程 guard 验证）通过
- [ ] 向后兼容验证通过（无 --guard 时 guard 相关路径行为不变）
- [ ] 三平台（Windows/macOS/Linux）CI 编译通过
