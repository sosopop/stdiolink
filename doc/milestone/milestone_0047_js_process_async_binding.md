# 里程碑 47：JS 异步进程绑定（process）

> **前置条件**: 里程碑 46 已完成
> **目标**: 新增 `stdiolink/process`，提供 `execAsync` 与 `spawn`，解决同步 `exec` 阻塞问题，并保持现有 `stdiolink.exec` 完全兼容

---

## 1. 目标

- 新增内置模块 `stdiolink/process`
- 提供 API：
  - `execAsync(program, args?, options?)`
  - `spawn(program, args?, options?)`
- 提供 `ProcessHandle` 运行时句柄：
  - `onStdout/onStderr/onExit`
  - `write/closeStdin/kill`
  - `pid/running`
- 保留 `stdiolink` 主模块中的同步 `exec`（`js_process.cpp`）原行为不变

---

## 2. 设计原则（强约束）

- **简约**: 仅提供最小可用异步进程能力，不引入管道 DSL、PTY、进程组管理
- **可靠**: 进程生命周期、Promise 生命周期、JS 句柄生命周期必须一致
- **稳定**: 回调触发顺序可预测，`onExit` 只触发一次
- **避免过度设计**: 不做 signal 全语义映射，只支持 `SIGTERM/SIGKILL` 最小集合

---

## 3. 范围与非目标

### 3.1 范围（M47 内）

- `stdiolink/process` 模块
- `execAsync/spawn` 能力
- `ProcessHandle` class 绑定
- 进程与回调资源清理机制
- 完整单元测试（正常、异常、并发、生命周期）

### 3.2 非目标（M47 外）

- 不替换或重构 `stdiolink.exec`
- 不实现 PTY 交互
- 不实现 shell 语法（`|`、重定向、变量展开）
- 不实现复杂进程树控制（如子孙进程回收策略）

---

## 4. 技术方案

### 4.1 模块接口

```js
import { execAsync, spawn } from "stdiolink/process";
```

#### 4.1.1 `execAsync`

```ts
execAsync(
  program: string,
  args?: string[],
  options?: {
    cwd?: string;
    env?: Record<string, string>;
    input?: string;
    timeoutMs?: number;
  }
): Promise<{
  exitCode: number;
  stdout: string;
  stderr: string;
}>;
```

语义：

- **正常退出（含非零退出码）**: Promise `resolve`
- **启动失败**: Promise `reject`
- **超时**: Promise `reject`（并主动终止子进程）

#### 4.1.2 `spawn`

```ts
spawn(
  program: string,
  args?: string[],
  options?: {
    cwd?: string;
    env?: Record<string, string>;
  }
): ProcessHandle;
```

```ts
interface ProcessHandle {
  readonly pid: number;
  readonly running: boolean;

  onStdout(cb: (chunk: string) => void): ProcessHandle;
  onStderr(cb: (chunk: string) => void): ProcessHandle;
  onExit(cb: (result: { exitCode: number; exitStatus: "normal" | "crash" }) => void): ProcessHandle;

  write(data: string): boolean;
  closeStdin(): void;

  // signal: 'SIGTERM' | 'SIGKILL'，默认 'SIGTERM'
  kill(signal?: string): void;
}
```

### 4.2 语义约束

- `program` 必须为非空字符串，否则抛 `TypeError`
- `args` 必须为字符串数组；数组内任一项非字符串抛 `TypeError`
- `options` 必须为对象；未知 key 抛 `TypeError`
- `execAsync.timeoutMs`：
  - 必须为正整数
  - 默认值 `30000`（对齐当前同步 `exec` 默认超时）
- `onExit` 注册多个回调时，按注册顺序触发
- 进程已退出后再注册 `onExit`：应在下一轮微任务触发一次回调（保持行为一致）
- `write()` 在进程未运行时返回 `false`，运行中写入返回 `true`
- `closeStdin()`、`kill()` 必须幂等，不因重复调用崩溃

### 4.3 创建 `bindings/js_process_async.h`

```cpp
#pragma once

#include <quickjs.h>

namespace stdiolink_service {

/// @brief stdiolink/process 内置模块绑定
///
/// 提供异步进程执行 API（execAsync/spawn）。
/// 底层使用 QProcess，Promise/回调通过 Qt 信号桥接。
/// 绑定状态按 JSRuntime 隔离，支持在 runtime 销毁时统一清理。
class JsProcessAsyncBinding {
public:
    /// 绑定 runtime（main.cpp 初始化时调用）
    static void attachRuntime(JSRuntime* rt);

    /// 解绑 runtime（JsEngine 析构时调用）
    static void detachRuntime(JSRuntime* rt);

    /// 模块初始化回调（注册给 ModuleLoader）
    static JSModuleDef* initModule(JSContext* ctx, const char* name);

    /// 测试辅助：重置 runtime 状态
    static void reset(JSContext* ctx);
};

} // namespace stdiolink_service
```

### 4.4 创建 `bindings/js_process_async.cpp`（关键实现）

```cpp
#include "js_process_async.h"

#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <quickjs.h>

namespace stdiolink_service {

namespace {

struct ExitResult {
    int exitCode = 0;
    bool crashed = false;
};

struct ProcessHandleData {
    JSContext* ctx = nullptr;
    QProcess* proc = nullptr;
    bool running = false;
    bool exitNotified = false;

    // JS callbacks
    QList<JSValue> stdoutCallbacks;
    QList<JSValue> stderrCallbacks;
    QList<JSValue> exitCallbacks;

    // 仅 execAsync 使用
    JSValue resolve = JS_UNDEFINED;
    JSValue reject = JS_UNDEFINED;
    QTimer* timeoutTimer = nullptr;
    QByteArray capturedStdout;
    QByteArray capturedStderr;
};

struct ProcessState {
    JSContext* ctx = nullptr;
    QSet<ProcessHandleData*> handles;
};

QHash<quintptr, ProcessState> s_states;
JSClassID s_processHandleClassId = 0;

quintptr runtimeKey(JSContext* ctx) {
    return reinterpret_cast<quintptr>(JS_GetRuntime(ctx));
}

ProcessState& stateFor(JSContext* ctx) {
    return s_states[runtimeKey(ctx)];
}

void freeCallbacks(JSContext* ctx, QList<JSValue>& callbacks) {
    for (JSValue cb : callbacks) {
        JS_FreeValue(ctx, cb);
    }
    callbacks.clear();
}

void destroyHandle(ProcessHandleData* h) {
    if (!h) return;
    JSContext* ctx = h->ctx;

    if (h->timeoutTimer) {
        h->timeoutTimer->stop();
        h->timeoutTimer->deleteLater();
        h->timeoutTimer = nullptr;
    }

    if (h->proc) {
        if (h->proc->state() != QProcess::NotRunning) {
            h->proc->terminate();
            if (!h->proc->waitForFinished(100)) {
                h->proc->kill();
                h->proc->waitForFinished(100);
            }
        }
        h->proc->deleteLater();
        h->proc = nullptr;
    }

    if (ctx) {
        freeCallbacks(ctx, h->stdoutCallbacks);
        freeCallbacks(ctx, h->stderrCallbacks);
        freeCallbacks(ctx, h->exitCallbacks);
        if (!JS_IsUndefined(h->resolve)) JS_FreeValue(ctx, h->resolve);
        if (!JS_IsUndefined(h->reject)) JS_FreeValue(ctx, h->reject);
    }

    delete h;
}

} // namespace

} // namespace stdiolink_service
```

关键实现约束：

1. `ProcessHandleData` 与 `QProcess` 一一对应
2. `QProcess::readyReadStandardOutput/readyReadStandardError` 触发时：
   - `execAsync`: 累积到 `capturedStdout/capturedStderr`
   - `spawn`: 推送到已注册 JS 回调
3. `QProcess::finished`：
   - 更新 `running=false`
   - 触发 `onExit`（一次）
   - `execAsync` 分支执行 `resolve`
4. `QProcess::errorOccurred` 在“启动失败”场景触发 `reject`
5. runtime detach 时，遍历 `state.handles` 统一销毁

### 4.5 `main.cpp` / `js_engine.cpp` 集成

#### 4.5.1 `main.cpp` 集成

```cpp
#include "bindings/js_process_async.h"

// engine 初始化后
stdiolink_service::JsProcessAsyncBinding::attachRuntime(engine.runtime());

// 注册模块
engine.registerModule("stdiolink/process",
                      stdiolink_service::JsProcessAsyncBinding::initModule);
```

#### 4.5.2 `js_engine.cpp` 析构清理

```cpp
// JsEngine::~JsEngine()
stdiolink_service::JsProcessAsyncBinding::detachRuntime(oldRt);
```

#### 4.5.3 保持 `stdiolink` 主模块兼容

- 不修改 `js_stdiolink_module.cpp` 的 `exec` 导出行为
- `execAsync/spawn` 仅在 `stdiolink/process` 暴露

### 4.6 JS 使用示例

```js
import { execAsync, spawn } from "stdiolink/process";

const r = await execAsync("echo", ["hello"], { timeoutMs: 2000 });
console.log(r.exitCode, r.stdout);

const p = spawn("python", ["-u", "worker.py"], { cwd: "/tmp" });
p.onStdout((chunk) => console.log("out", chunk));
p.onStderr((chunk) => console.error("err", chunk));
p.onExit((e) => console.log("exit", e.exitCode, e.exitStatus));
p.write("ping\n");
p.closeStdin();
```

---

## 5. 实现步骤

1. 新增 `js_process_async.h/.cpp`，实现 runtime state 与 `ProcessHandle` class
2. 在 `js_process_async.cpp` 实现参数解析、`execAsync` Promise、`spawn` 句柄
3. 接入 `main.cpp` 模块注册与 runtime attach
4. 在 `js_engine.cpp` 析构时新增 detach 清理
5. 调整 `src/stdiolink_service/CMakeLists.txt` 纳入新文件
6. 新增测试桩进程（见 6.1）与 `test_process_async_binding.cpp`
7. 更新 manual 文档和模块总览文档
8. 全量执行相关测试并修复回归

---

## 6. 文件改动清单

### 6.1 新增文件

- `src/stdiolink_service/bindings/js_process_async.h`
- `src/stdiolink_service/bindings/js_process_async.cpp`
- `src/tests/test_process_async_binding.cpp`
- `src/tests/test_process_async_stub_main.cpp`
- `doc/manual/10-js-service/process-async-binding.md`

### 6.2 修改文件

- `src/stdiolink_service/main.cpp`
- `src/stdiolink_service/engine/js_engine.cpp`
- `src/stdiolink_service/CMakeLists.txt`
- `src/tests/CMakeLists.txt`
- `doc/manual/10-js-service/module-system.md`
- `doc/manual/10-js-service/README.md`

---

## 7. 单元测试计划（全面覆盖）

新增测试文件：`src/tests/test_process_async_binding.cpp`

### 7.1 测试 Fixture

```cpp
class JsProcessAsyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        ConsoleBridge::install(engine->context());

        // 保留 stdiolink 主模块（回归对照）
        engine->registerModule("stdiolink", jsInitStdiolinkModule);

        // 新增 stdiolink/process
        JsProcessAsyncBinding::attachRuntime(engine->runtime());
        engine->registerModule("stdiolink/process", JsProcessAsyncBinding::initModule);
    }

    int runScript(const QString& filePath) {
        int ret = engine->evalFile(filePath);

        // 异步测试必须驱动 Qt 事件循环 + QuickJS job queue
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 10000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            while (engine->hasPendingJobs()) {
                engine->executePendingJobs();
            }
            if (isScriptDone()) {
                break;
            }
        }
        if (ret == 0 && engine->hadJobError()) {
            ret = 1;
        }
        return ret;
    }

    bool isScriptDone(); // 读取 globalThis.__done

    std::unique_ptr<JsEngine> engine;
};
```

`test_process_async_stub_main.cpp` 作为稳定测试桩，支持参数：

- `--mode=echo`：回显 stdin
- `--mode=stdout`：向 stdout 输出指定文本
- `--mode=stderr`：向 stderr 输出指定文本
- `--sleep-ms=<n>`：延迟退出
- `--exit-code=<n>`：指定退出码

### 7.2 `execAsync` 功能正确性

```cpp
TEST_F(JsProcessAsyncTest, ImportExecAsyncAndSpawn) {
    // import { execAsync, spawn } from 'stdiolink/process'
}

TEST_F(JsProcessAsyncTest, ExecAsyncResolvesOnExitCodeZero) {
    // stdout/stderr/exitCode 正确
}

TEST_F(JsProcessAsyncTest, ExecAsyncNonZeroStillResolves) {
    // exitCode=42 时仍 resolve，reject 不触发
}

TEST_F(JsProcessAsyncTest, ExecAsyncCwdAndEnvAndInputWorks) {
    // 验证 cwd / env / input
}
```

### 7.3 `execAsync` 异常与边界

```cpp
TEST_F(JsProcessAsyncTest, ExecAsyncTimeoutRejectsAndKillsProcess) {
    // timeoutMs=50，子进程 sleep 5000
}

TEST_F(JsProcessAsyncTest, ExecAsyncMissingProgramRejects) {
    // 程序不存在
}

TEST_F(JsProcessAsyncTest, ExecAsyncInvalidArgsTypeThrowsTypeError) {
    // args 非数组
}

TEST_F(JsProcessAsyncTest, ExecAsyncUnknownOptionThrowsTypeError) {
    // options 含未知键
}
```

### 7.4 `spawn` 事件与控制面

```cpp
TEST_F(JsProcessAsyncTest, SpawnOnStdoutReceivesChunks) {
    // onStdout 至少收到一段数据
}

TEST_F(JsProcessAsyncTest, SpawnOnStderrReceivesChunks) {
    // onStderr 收到错误输出
}

TEST_F(JsProcessAsyncTest, SpawnOnExitTriggeredExactlyOnce) {
    // onExit 计数=1
}

TEST_F(JsProcessAsyncTest, SpawnWriteAndCloseStdinWorks) {
    // 写入后 closeStdin，进程正常退出
}

TEST_F(JsProcessAsyncTest, SpawnKillSigtermAndSigkill) {
    // 默认 kill + SIGKILL 都可终止进程
}
```

### 7.5 生命周期与并发稳定性

```cpp
TEST_F(JsProcessAsyncTest, MultipleSpawnInParallelNoCrossTalk) {
    // 并发启动 5 个进程，输出互不串扰
}

TEST_F(JsProcessAsyncTest, RepeatedKillAndCloseStdinAreIdempotent) {
    // 重复调用不崩溃
}

TEST_F(JsProcessAsyncTest, RuntimeDetachCleansPendingProcesses) {
    // 销毁 JsEngine 后无挂起进程
}
```

### 7.6 回归测试

- `src/tests/test_process_binding.cpp`（同步 `exec` 不变）
- `src/tests/test_driver_task_binding.cpp`
- `src/tests/test_proxy_and_scheduler.cpp`
- `src/tests/test_js_stress.cpp`

---

## 8. 验收标准（DoD）

- `stdiolink/process` 模块可导入、API 与文档一致
- `execAsync` 与 `spawn` 正常/异常路径全部可测
- runtime 销毁无资源泄漏、无悬挂进程
- 现有 `stdiolink.exec` 相关测试不回归
- 新增与回归测试全部通过

---

## 9. 风险与控制

- **风险 1**：QProcess 信号回调与 JSValue 生命周期竞态
  - 控制：统一 `ProcessHandleData` 状态机；所有回调前检查 `running/exitNotified`
- **风险 2**：异步测试不稳定（依赖系统命令差异）
  - 控制：引入 `test_process_async_stub_main.cpp`，避免依赖 shell 行为
- **风险 3**：新增模块影响现有 `exec` 行为
  - 控制：`stdiolink/process` 与 `stdiolink` 主模块隔离；回归测试强制覆盖
