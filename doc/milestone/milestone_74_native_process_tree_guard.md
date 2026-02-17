# 里程碑 74：OS 级进程树守护（ProcessTreeGuard）

> **前置条件**: 里程碑 70（ProcessGuard 工具类）、里程碑 71（ProcessGuard 全链路集成）已完成
> **目标**: 在 Driver 类中引入 OS 原生机制（Windows Job Object / Linux PDEATHSIG），确保 stdiolink_service 被杀死时其 Driver 子进程也被内核级清理，与现有 QLocalSocket 方案形成双保险

---

## 1. 目标

- 新增 `ProcessTreeGuard` 工具类，封装 Windows Job Object 和 Linux PDEATHSIG 两套平台原生进程树守护机制
- 集成到 `Driver::start()` 流程，对 Driver 子进程自动启用 OS 级守护
- 现有 QLocalSocket 方案（ProcessGuardServer/Client）完全不修改
- 完整单元测试覆盖

---

## 2. 背景与问题

- 现有 ProcessGuard 基于 QLocalSocket 轮询（200ms 间隔），父进程被 SIGKILL/TerminateProcess 后子进程需要最多 200ms 才能感知
- QLocalSocket 方案无法覆盖孙进程（Windows 场景下 Driver 可能 fork 出更深层子进程）
- OS 原生机制可以做到零延迟、内核级强保障：
  - Windows Job Object：父进程句柄关闭时 OS 立即终止 Job 内所有进程（含整个进程树）
  - Linux PDEATHSIG：父进程退出时内核立即向子进程发送指定信号
- 两套机制互补：OS 级做硬保障，QLocalSocket 做应用层兜底与跨平台一致性

**范围**：
- 仅 `Driver` 类（stdiolink_service 作为父进程，Driver 可执行文件作为子进程）
- Windows 和 Linux 两个平台

**非目标**：
- 不修改 InstanceManager（Server→Service 链路不在本里程碑范围）
- 不修改 ProcessGuardServer / ProcessGuardClient
- 不支持 macOS（macOS 无 PDEATHSIG 等价物，依赖现有 QLocalSocket 方案）
- 不处理孙进程的 PDEATHSIG 传递（Linux PDEATHSIG 仅对直接子进程生效，孙进程由 QLocalSocket 方案覆盖）

---

## 3. 技术要点

### 3.1 ProcessTreeGuard 接口

```cpp
namespace stdiolink {

class ProcessTreeGuard {
public:
    ProcessTreeGuard();
    ~ProcessTreeGuard();

    // 禁止拷贝和移动
    ProcessTreeGuard(const ProcessTreeGuard&) = delete;
    ProcessTreeGuard& operator=(const ProcessTreeGuard&) = delete;

    // QProcess::start() 前调用 — Linux 下设置 setChildProcessModifier
    void prepareProcess(QProcess* process);

    // QProcess::start() 后调用 — Windows 下将子进程加入 Job Object
    bool adoptProcess(QProcess* process);

    // Job Object 是否有效（Windows），Linux 始终返回 true
    bool isValid() const;

private:
#ifdef Q_OS_WIN
    void* m_jobHandle = nullptr;  // HANDLE
#endif
#ifdef Q_OS_LINUX
    pid_t m_parentPid = 0;
#endif
};

} // namespace stdiolink
```

### 3.2 Windows 实现：Job Object

```
构造函数:
  m_jobHandle = CreateJobObjectW(NULL, NULL)
  设置 JOBOBJECT_EXTENDED_LIMIT_INFORMATION:
    BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
  SetInformationJobObject(m_jobHandle, ..., &info, sizeof(info))

prepareProcess(QProcess*):
  空实现（Windows 不需要 child modifier）

adoptProcess(QProcess*):
  DWORD pid = process->processId()
  HANDLE hProc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, pid)
  AssignProcessToJobObject(m_jobHandle, hProc)
  CloseHandle(hProc)

析构函数:
  CloseHandle(m_jobHandle)
```

父进程（stdiolink_service）被杀死时，进程句柄关闭 → Job Object 引用计数归零 → OS 立即终止 Job 内所有进程。

### 3.3 Linux 实现：PDEATHSIG

```
构造函数:
  m_parentPid = getpid()   // 记录父进程 PID，供子进程校验

prepareProcess(QProcess*):
  pid_t parentPid = m_parentPid;
  process->setChildProcessModifier([parentPid] {
      prctl(PR_SET_PDEATHSIG, SIGKILL);
      // 防竞态：若父进程在 fork 后、prctl 前已退出，
      // 此时 getppid() 返回的是 init/systemd (1) 而非原父进程
      if (getppid() != parentPid) {
          _exit(1);
      }
  })

adoptProcess(QProcess*):
  空实现（PDEATHSIG 已在 fork 后 exec 前由子进程自行设置）

析构函数:
  空实现
```

`setChildProcessModifier` 的 lambda 在 `fork()` 之后、`exec()` 之前由子进程执行。

**PDEATHSIG 竞态防护**：经典问题——若父进程在 `fork()` 之后、`prctl()` 之前退出，子进程会漏掉信号。解决方案：在 `prctl` 之后立即用 `getppid()` 校验父进程 PID，若已变化（被 init 收养）则立即 `_exit(1)` 自杀。`m_parentPid` 在父进程构造时通过 `getpid()` 捕获，通过 lambda 值捕获传入子进程。

### 3.4 Driver 集成时序

```
Driver::start(program, args)
  ├─ 创建 ProcessGuardServer（现有逻辑，不变）
  ├─ 拼接 --guard 参数（现有逻辑，不变）
  ├─ m_treeGuard.prepareProcess(&m_proc)   ← 新增：Linux 设置 modifier
  ├─ m_proc.start()
  ├─ m_treeGuard.adoptProcess(&m_proc)     ← 新增：Windows 加入 Job
  └─ waitForStarted()
```

### 3.5 竞态分析（Windows adoptProcess）

`adoptProcess()` 在 `start()` 之后调用，存在极短窗口（子进程已启动但尚未加入 Job）。对本项目可接受：
- Driver 子进程启动后先做参数解析和 guard client 连接，不会立即 fork 孙进程
- 窗口通常在微秒级

如需彻底消除竞态，可后续通过 `QProcess::setCreateProcessArgumentsModifier` 在 `CreateProcess` 时注入 `PROC_THREAD_ATTRIBUTE_JOB_LIST`，但复杂度显著增加，不在本里程碑范围。

### 3.6 双保险机制对比

| | QLocalSocket (M70/M71) | ProcessTreeGuard (本里程碑) |
|---|---|---|
| 触发方式 | 子进程轮询 socket 连接状态 | OS 内核级 |
| 父进程被 SIGKILL | ~200ms 延迟检测 | Windows: 立即；Linux: 立即 |
| 孙进程覆盖 | 不覆盖 | Windows Job Object 覆盖整个进程树；Linux 仅直接子进程 |
| 失败回退 | 无（独立机制） | Job 创建失败时仅日志警告，不阻塞启动 |

### 3.7 容错策略

- Windows：`CreateJobObject` 或 `AssignProcessToJobObject` 失败时，`adoptProcess()` 返回 false，`Driver::start()` 仅输出警告日志，不阻塞启动（QLocalSocket 方案仍然生效）
- Linux：`setChildProcessModifier` 不会失败（仅注册 lambda），`prctl` 在子进程中执行，失败时子进程仍正常运行（QLocalSocket 方案兜底）
- 构造函数中 Job Object 创建失败时 `isValid()` 返回 false，后续 `adoptProcess()` 直接跳过

---

## 4. 实现步骤

### 4.1 新增 ProcessTreeGuard 头文件

- 创建 `src/stdiolink/guard/process_tree_guard.h`
- 声明类接口（3.1 节所述）
- 使用 `#ifdef Q_OS_WIN` / `#ifdef Q_OS_LINUX` 条件编译

### 4.2 新增 ProcessTreeGuard 实现

- 创建 `src/stdiolink/guard/process_tree_guard.cpp`
- Windows 段（`#ifdef Q_OS_WIN`）：
  - 包含 `<windows.h>`
  - 构造函数：`CreateJobObjectW` + `SetInformationJobObject`（`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`）
  - `adoptProcess()`：`OpenProcess` + `AssignProcessToJobObject` + `CloseHandle`
  - 析构函数：`CloseHandle(m_jobHandle)`
- Linux 段（`#ifdef Q_OS_LINUX`）：
  - 包含 `<sys/prctl.h>`、`<signal.h>` 和 `<unistd.h>`
  - 构造函数：`m_parentPid = getpid()`
  - `prepareProcess()`：通过 `setChildProcessModifier` 注册 lambda，lambda 值捕获 `m_parentPid`，内部执行 `prctl(PR_SET_PDEATHSIG, SIGKILL)` 后立即 `getppid()` 校验，若父进程已变则 `_exit(1)`
- 其他平台：所有方法为空实现

### 4.3 更新 CMakeLists.txt

- 修改 `src/stdiolink/CMakeLists.txt`：在 `GUARD_SOURCES` 中追加 `guard/process_tree_guard.cpp`

### 4.4 集成到 Driver

- 修改 `src/stdiolink/host/driver.h`：新增 `ProcessTreeGuard m_treeGuard` 成员，添加 include
- 修改 `src/stdiolink/host/driver.cpp`：在 `start()` 中 `m_proc.start()` 前后插入 `prepareProcess` / `adoptProcess` 调用

### 4.5 单元测试

- 创建 `src/tests/test_process_tree_guard.cpp`

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/stdiolink/guard/process_tree_guard.h` — ProcessTreeGuard 类声明
- `src/stdiolink/guard/process_tree_guard.cpp` — 平台条件编译实现
- `src/tests/test_tree_guard_parent_stub_main.cpp` — T08/T09 辅助父进程 stub（创建 ProcessTreeGuard + 启动子进程 + 输出 PID）
- `src/tests/test_tree_guard_check_stub_main.cpp` — T10 辅助 stub（输出自身守护状态：Job/PDEATHSIG）

### 5.2 修改文件

- `src/stdiolink/CMakeLists.txt` — GUARD_SOURCES 追加 `guard/process_tree_guard.cpp`
- `src/stdiolink/host/driver.h` — 新增 `ProcessTreeGuard m_treeGuard` 成员
- `src/stdiolink/host/driver.cpp` — `start()` 中插入 `prepareProcess` / `adoptProcess` 调用
- `src/tests/CMakeLists.txt` — TEST_SOURCES 新增 `test_process_tree_guard.cpp`；新增 `test_tree_guard_parent_stub`、`test_tree_guard_check_stub` 辅助可执行目标

### 5.3 测试文件

- `src/tests/test_process_tree_guard.cpp` — ProcessTreeGuard 单元测试与进程级验证

---

## 6. 测试与验收

### 6.1 单元测试（必填，重点）

测试对象：`ProcessTreeGuard`（构造/析构、prepareProcess、adoptProcess、isValid）及 Driver 集成后的进程树守护行为

测试文件：`src/tests/test_process_tree_guard.cpp`

桩替身策略：
- 使用现有 `test_guard_stub`（M70 创建的辅助子进程，sleep 等待）验证进程树守护行为
- 不需要 mock，使用真实 QProcess 进行进程级验证
- 平台相关测试用 `#ifdef` 条件编译

#### 路径矩阵

| 决策点 | 路径 | 用例 ID | 平台 |
|--------|------|---------|------|
| 构造函数 | Job Object 创建成功，isValid() == true | T01 | Windows |
| prepareProcess (Linux) | modifier 注册成功，子进程设置 PDEATHSIG | T02 | Linux |
| adoptProcess | 子进程成功加入 Job Object | T03 | Windows |
| adoptProcess | isValid() == false 时跳过，返回 false | T04 | Windows |
| adoptProcess | AssignProcessToJobObject 失败（外部 Job 限制等），返回 false + qWarning 日志 | T04_b | Windows |
| adoptProcess | 进程未启动（pid 无效）时返回 false | T05 | Windows |
| prepareProcess (Windows) | 空实现，不影响 QProcess | T06 | Windows |
| adoptProcess (Linux) | 空实现，返回 true | T07 | Linux |
| 父进程退出 → 子进程被杀 | Job Object 关闭后子进程终止 | T08 | Windows |
| 父进程退出 → 子进程被杀 | PDEATHSIG 触发后子进程终止 | T09 | Linux |
| Driver 集成 | start() 后子进程受 OS 级守护 | T10 | 跨平台 |
| 多子进程 | 多个子进程均受同一 Job Object 守护 | T11 | Windows |
| 析构安全 | 正常退出时子进程已终止，Job 关闭无副作用 | T12 | Windows |

#### 用例详情

**T01 — Windows Job Object 创建成功**
- 输入：构造 ProcessTreeGuard
- 预期：isValid() == true
- 断言：isValid() 返回 true

**T02 — Linux PDEATHSIG 设置验证**
- 输入：构造 ProcessTreeGuard，对 QProcess 调用 prepareProcess，启动子进程
- 预期：子进程内 `prctl(PR_GET_PDEATHSIG, &sig)` 返回 SIGKILL
- 断言：通过辅助子进程输出验证 PDEATHSIG 已设置为 SIGKILL
- 实现：辅助子进程启动后调用 `prctl(PR_GET_PDEATHSIG, &sig)` 并将 sig 值写入 stdout，父进程读取验证

**T03 — Windows adoptProcess 成功**
- 输入：构造 ProcessTreeGuard，启动 QProcess，调用 adoptProcess
- 预期：返回 true
- 断言：返回值为 true

**T04 — Windows adoptProcess 在 isValid()==false 时跳过**
- 输入：构造 ProcessTreeGuard 后手动关闭 m_jobHandle（通过测试友元或构造失败模拟），调用 adoptProcess
- 预期：返回 false，不崩溃
- 断言：返回值为 false
- 实现：由于无法直接模拟 CreateJobObject 失败，使用 `#ifdef STDIOLINK_TESTING` 暴露 `invalidateForTesting()` 方法将 m_jobHandle 置 nullptr

**T04_b — Windows adoptProcess 失败时输出警告日志**
- 输入：构造 ProcessTreeGuard（isValid()==true），启动 QProcess，模拟 `AssignProcessToJobObject` 失败（通过 `invalidateForTesting()` 在 adoptProcess 前将 handle 置 nullptr）
- 预期：返回 false，输出包含 `ProcessTreeGuard` 关键字的 qWarning 日志
- 断言：返回值为 false；通过 `QTest::ignoreMessage(QtWarningMsg, ...)` 验证日志输出

**T05 — Windows adoptProcess 进程未启动**
- 输入：构造 ProcessTreeGuard 和 QProcess（未 start），调用 adoptProcess
- 预期：返回 false（processId() == 0，OpenProcess 失败）
- 断言：返回值为 false

**T06 — Windows prepareProcess 空实现**
- 输入：构造 ProcessTreeGuard，对 QProcess 调用 prepareProcess，启动子进程
- 预期：子进程正常启动和退出，不受影响
- 断言：子进程 exitCode == 0

**T07 — Linux adoptProcess 空实现**
- 输入：构造 ProcessTreeGuard，启动 QProcess，调用 adoptProcess
- 预期：返回 true
- 断言：返回值为 true

**T08 — Windows 父进程退出后子进程被 Job Object 终止**
- 输入：启动一个中间进程 A，A 创建 ProcessTreeGuard 并启动子进程 B（sleep 长时间），然后杀死 A
- 预期：B 在 A 退出后被 OS 终止
- 断言：B 进程在 5 秒内退出
- 实现：测试进程启动辅助进程 A（`test_tree_guard_parent_stub`），A 内部创建 ProcessTreeGuard + 启动 B（`test_guard_stub`），A 将 B 的 PID 输出到 stdout。测试进程杀死 A 后，通过 `OpenProcess(SYNCHRONIZE, FALSE, pidB)` + `WaitForSingleObject(handle, 5000)` 等待 B 退出，避免 PID 轮询的竞态与复用问题。`WaitForSingleObject` 返回 `WAIT_OBJECT_0` 即判定通过。

**T09 — Linux 父进程退出后子进程被 PDEATHSIG 终止**
- 输入：与 T08 相同流程，中间进程 A 使用 ProcessTreeGuard 启动子进程 B，杀死 A
- 预期：B 收到 SIGKILL 并退出
- 断言：B 进程在 5 秒内退出
- 实现：与 T08 相同的辅助进程架构。测试进程杀死 A 后，通过 `waitpid(pidB, &status, 0)` 配合 `alarm(5)` 超时等待 B 退出（测试进程需先通过 `prctl(PR_SET_CHILD_SUBREAPER, 1)` 成为 subreaper 以接管孙进程 B 的 wait）。若 `waitpid` 在超时内返回且 `WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL` 即判定通过。

**T10 — Driver 集成后子进程受 OS 级守护**
- 输入：通过 Driver::start() 启动子进程（使用专用 stub `test_tree_guard_check_stub`）
- 预期：Windows 下子进程在 Job Object 中；Linux 下子进程 PDEATHSIG 已设置
- 断言：
  - Windows：`test_tree_guard_check_stub` 启动后调用 `IsProcessInJob(GetCurrentProcess(), NULL, &result)` 并将 result 写入 stdout，父进程读取验证 result == TRUE
  - Linux：`test_tree_guard_check_stub` 启动后调用 `prctl(PR_GET_PDEATHSIG, &sig)` 并将 sig 写入 stdout，父进程读取验证 sig == 9 (SIGKILL)
- 实现：新增辅助可执行目标 `test_tree_guard_check_stub`，启动后输出平台对应的守护状态到 stdout 后退出

**T11 — Windows 多子进程均受同一 Job Object 守护**
- 输入：构造一个 ProcessTreeGuard，启动两个 QProcess 并分别 adoptProcess
- 预期：两个子进程均成功加入 Job
- 断言：两次 adoptProcess 均返回 true

**T12 — 正常退出时 Job 关闭无副作用**
- 输入：构造 ProcessTreeGuard，启动子进程并 adoptProcess，子进程正常退出后析构 ProcessTreeGuard
- 预期：无崩溃、无异常
- 断言：析构正常完成

覆盖要求：所有 13 条路径均有对应用例，100% 可达路径覆盖。

不可达路径说明：
- `CreateJobObjectW` 返回 NULL：仅在系统资源极端耗尽时发生，无法在单元测试中可靠复现。通过 T04 的 `invalidateForTesting()` 间接覆盖 isValid()==false 的后续路径。
- `prctl(PR_SET_PDEATHSIG)` 失败：Linux 内核保证对 SIGKILL 参数不会失败（除非 signal 编号非法），不可达。

### 6.2 集成测试

**跨进程父子守护验证（T08/T09）：**
- 需要新增辅助可执行目标 `test_tree_guard_parent_stub`
- 该 stub 的逻辑：创建 ProcessTreeGuard → 启动 `test_guard_stub` 子进程 → 将子进程 PID 输出到 stdout → sleep 等待被杀
- T08（Windows）：测试进程通过 `OpenProcess` + `WaitForSingleObject` 等待子进程 B 退出
- T09（Linux）：测试进程先 `prctl(PR_SET_CHILD_SUBREAPER, 1)` 成为 subreaper，再通过 `waitpid` 等待孙进程 B 退出

**Driver 集成守护状态验证（T10）：**
- 需要新增辅助可执行目标 `test_tree_guard_check_stub`
- 该 stub 的逻辑：启动后检测自身守护状态（Windows: `IsProcessInJob`；Linux: `prctl(PR_GET_PDEATHSIG)`），将结果写入 stdout 后退出

### 6.3 验收标准

- [ ] Windows：ProcessTreeGuard 构造成功创建 Job Object，isValid() == true
- [ ] Windows：adoptProcess 成功将子进程加入 Job Object
- [ ] Windows：父进程被杀后子进程被 OS 终止（T08）
- [ ] Linux：prepareProcess 成功设置 PDEATHSIG（T02 验证）
- [ ] Linux：父进程被杀后子进程被内核终止（T09）
- [ ] Driver::start() 中 prepareProcess/adoptProcess 正确调用
- [ ] ProcessTreeGuard 失败不阻塞 Driver 启动（容错降级）
- [ ] 现有 ProcessGuardServer/Client 代码无任何修改
- [ ] 所有 13 条测试用例通过（平台条件编译，各平台运行对应子集）

---

## 7. 风险与控制

- 风险：Windows `adoptProcess` 在 `start()` 之后调用，存在极短竞态窗口
  - 控制：Driver 子进程启动后先做参数解析和 guard client 连接，不会立即 fork 孙进程，竞态窗口在微秒级，实际无影响
  - 控制：后续可通过 `setCreateProcessArgumentsModifier` + `PROC_THREAD_ATTRIBUTE_JOB_LIST` 彻底消除，不在本里程碑范围

- 风险：Linux PDEATHSIG 经典竞态——父进程在 `fork()` 后、`prctl()` 前退出，子进程漏掉信号
  - 控制：`setChildProcessModifier` lambda 中 `prctl` 后立即 `getppid()` 校验父 PID，若已变化则 `_exit(1)` 自杀（见 3.3 节）

- 风险：Linux PDEATHSIG 仅对直接子进程生效，不传递到孙进程
  - 控制：孙进程的清理由现有 QLocalSocket 方案覆盖；Driver 子进程自身也可为其子进程设置 ProcessTreeGuard

- 风险：`setChildProcessModifier` 覆盖问题——如果其他代码也调用了 `setChildProcessModifier`，后者会覆盖前者
  - 控制：当前 Driver::start() 中无其他 `setChildProcessModifier` 调用；如未来需要多个 modifier，可合并为单个 lambda

- 风险：Windows Job Object 嵌套——如果 stdiolink_service 自身已在外部 Job Object 中（CI runner、Docker、Windows 服务管理器等场景），`AssignProcessToJobObject` 可能失败
  - 控制：Windows 8+ 支持嵌套 Job Object，多数场景可正常工作，但外部 Job 若设置了限制性标志（如禁止 breakaway）仍可能导致失败
  - 控制：`adoptProcess()` 失败时输出 `qWarning` 日志（含 `GetLastError()` 错误码），返回 false，不阻塞启动；QLocalSocket 方案仍然生效
  - 控制：T04_b 新增测试用例验证 `adoptProcess` 失败时的日志输出与返回值
  - 控制：已知受影响场景记录于文档，运维可通过日志关键字 `ProcessTreeGuard: adoptProcess failed` 监控

- 风险：Job Object 创建失败导致 OS 级守护缺失
  - 控制：adoptProcess 返回 false 时仅输出警告日志，QLocalSocket 方案仍然生效，不影响功能正确性

---

## 8. 里程碑完成定义（DoD）

- [ ] ProcessTreeGuard 类实现完成（Windows Job Object + Linux PDEATHSIG）
- [ ] Driver::start() 集成 prepareProcess / adoptProcess 调用
- [ ] CMakeLists.txt 更新，新增源文件
- [ ] 13 条测试用例全部通过（各平台运行对应子集）
- [ ] 跨进程集成测试通过（父进程被杀后子进程自动终止）
- [ ] 现有 ProcessGuardServer / ProcessGuardClient 代码无修改
- [ ] Windows / Linux 双平台编译通过
