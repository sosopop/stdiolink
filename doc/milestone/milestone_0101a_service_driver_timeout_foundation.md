# 里程碑 101A：Service/Driver 超时能力补齐（M101 前置）

> **前置条件**: M86（`resolveDriver` 绑定）、M89（统一 runtime 布局）、现有 `stdiolink_server` / `stdiolink_service` / Host `Task` 能力
> **目标**: 在不修改既有 driver 业务协议的前提下，为 `server -> service` 增加实例执行超时 watchdog，为 `service -> driver` 增加统一命令超时能力，作为 M101 料仓扫描编排服务的前置基础设施

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| `src/stdiolink_server` | `schedule.runTimeoutMs` 契约与 `InstanceManager` 运行期 watchdog |
| `src/stdiolink_service` | `drv.xxx(params, options)` 命令超时能力 |
| `src/webui` | Project Schedule 表单中的 service timeout 配置项 |
| `src/stdiolink/doc` | Driver Proxy TypeScript 类型声明同步 |
| `src/tests` | Server/runtime 单元测试与超时替身 driver |
| `src/smoke_tests` | 超时主链路冒烟脚本与 CTest 接入 |
| `doc/milestone` | M101 前置子里程碑开发计划文档 |

- `Project.schedule` 支持显式配置 service 实例执行超时。
- WebUI Project Schedule 页面支持查看、编辑和保存该超时字段。
- `InstanceManager` 在实例进入 `running` 后启动 watchdog，超时自动终止 service 进程。
- `openDriver()` 返回的 proxy 保持原有 `drv.xxx(params)` 用法，并扩展支持 `drv.xxx(params, { timeoutMs })`。
- driver 命令超时基于现有 `$rawRequest()` + `Task.waitNext()` 实现，不新增 host/driver 协议字段。
- 中间 `event` 对命令调用保持透明；超时后强制关闭当前 driver，防止悬空 in-flight 请求污染后续调用。
- 单元测试覆盖核心功能场景 100%，并提供 server/service 双链路冒烟脚本。

## 2. 背景与问题

- M101 `bin_scan_orchestrator` 需要两层超时控制：
  - `server -> service`：one-shot service 不能无限挂起。
  - `service -> driver`：JS 调 driver 命令不能无限等待。
- 当前 `InstanceManager` 只有固定 `5s` 启动超时，没有实例运行期 watchdog。
- 当前 JS Driver Proxy 只有 `drv.xxx(params)` Promise 封装，没有命令级 `timeoutMs` 参数；业务层若自行 `Promise.race()`，无法取消底层 in-flight 请求，超时后 driver 仍可能处于不安全状态。
- Host 层已经提供了可复用的底层积木：
  - `Task.waitNext(timeoutMs)` 单次等待超时；
  - driver 进程退出时，Task 会被强制收敛为终态失败；
  - `Driver::terminate()` / proxy `$close()` 会直接结束 driver 子进程。
- 因此本里程碑的正确落点不是“把 timeout 传给 driver 内部 HTTP 请求”，而是补齐宿主层 watchdog 与统一超时封装，供 M101 直接消费。

### 2.1 设计原则（强约束）

- **P1: 不改 driver 业务协议**。超时能力只发生在 server/service 宿主层，不向现有业务命令新增必选参数。
- **P2: 保持现有主路径兼容**。不破坏 `drv.xxx(params)`、`$rawRequest()`、`schedule.type` 既有行为；未配置超时时保持当前语义。
- **P3: 超时后的 driver 不可复用**。任何命令超时后必须关闭当前 driver，调用方若需重试必须重建实例。
- **P4: 失败路径本地可控**。server timeout 复用 `test_service_stub`，driver timeout 新增最小 slow-command test driver，不依赖真实硬件或公网。
- **P5: 先补基础能力，再推进 M101 业务编排**。本里程碑不实现 BinScanOrchestrator 业务流程，只交付其前置基础设施。

**范围**:
- `Project.schedule` 新增执行超时字段并接入 parse/serialize/API 输出。
- WebUI Project Schedule 表单、类型定义、多语言文案与前端测试同步支持该字段。
- `InstanceManager` 在实例运行期增加超时 watchdog 与清理逻辑。
- Driver Proxy 命令调用支持第二个 `options` 参数，并实现统一超时等待。
- TypeScript 文档声明、单元测试、冒烟测试同步更新。

**非目标**:
- 不修改 `driver_3dvision`、`driver_plc_crane` 等 driver 内部 HTTP/PLC 超时实现。
- 不新增 host 层“真正取消当前命令”的协议；超时动作是关闭 driver 进程。
- 不把 `$rawRequest()` 改造成高层业务默认入口。
- 不在本里程碑中实现 M101 的 orchestrator 业务逻辑。
- 不重构 WebUI Project 编辑器整体架构；仅在现有 Schedule 表单中增量补充 timeout 字段。

## 3. 技术方案

### 3.1 `server -> service` 执行超时契约

`Project.schedule` 采用字段名 `runTimeoutMs`，表示单个 service 实例从进入 `running` 状态起的最大允许运行时长；`0` 表示不启用 watchdog。

```json
{
  "type": "manual",
  "runTimeoutMs": 30000
}
```

结构定义建议如下：

```cpp
struct Schedule {
    ScheduleType type = ScheduleType::Manual;

    int intervalMs = 5000;
    int maxConcurrent = 1;

    int restartDelayMs = 3000;
    int maxConsecutiveFailures = 5;

    int runTimeoutMs = 0; // 0 = disabled, >0 = watchdog enabled

    static Schedule fromJson(const QJsonObject& obj, QString& error);
    QJsonObject toJson() const;
};
```

行为约束：

| 场景 | 行为 |
|------|------|
| `runTimeoutMs == 0` 或缺失 | 保持现状，不启动运行期 watchdog |
| `runTimeoutMs > 0`，实例成功进入 `running` | 启动单次 timer；到期时 `kill()` 当前 service |
| 实例在 watchdog 到期前自然退出 | 停止/销毁 timer，不触发 timeout kill |
| 实例仍处于 `starting` | 仍由现有 `5s` 启动超时负责，不提前套用 `runTimeoutMs` |

可观测性约束：

- 本里程碑先采用“日志记录 timeout reason”作为统一落点，不额外新增 API/WebUI 结构化 `finishReason` 字段。
- Runtime / Logs / 冒烟脚本 通过日志文本区分 timeout 与普通失败。
- 若 M101 业务接入后确认需要结构化展示，再单独立项扩展 `Instance` API。

推荐的 `Instance` 扩展字段：

```cpp
struct Instance {
    // existing fields ...
    std::unique_ptr<QTimer> runTimeoutTimer;
    int runTimeoutMs = 0;
    bool timedOut = false;
    QString timeoutReason;
};
```

### 3.2 `service -> driver` 命令超时契约

对外 API 维持原样并增加第二个可选参数：

```ts
export interface DriverCommandCallOptions {
    timeoutMs?: number;
}

export interface DriverProxy {
    add(params?: AddParams, options?: DriverCommandCallOptions): Promise<AddResult>;
    batch(params?: BatchParams, options?: DriverCommandCallOptions): Promise<BatchResult>;
    readonly $driver: Driver;
    readonly $meta: object;
    $rawRequest(cmd: string, data?: any): Task;
    $close(): void;
}
```

核心语义：

| 场景 | 行为 |
|------|------|
| `drv.xxx(params)` | 完全兼容现状，无命令超时 |
| `drv.xxx(params, { timeoutMs })` | 对该命令启用整体 deadline |
| 命令中途收到 `event` | 忽略事件，继续等待终态 |
| 收到 `done` | resolve `msg.data` |
| 收到 `error` | reject；保留 `err.code` 与 `err.data` |
| deadline 到期且 task 未终态 | `target.terminate()`，reject timeout error |
| `waitNext()` 期间 driver 已退出 | reject driver-exit error，不误报为 timeout |

统一等待逻辑建议抽成 proxy 内部 helper：

```js
function normalizeCommandOptions(options) {
  if (options == null) return { timeoutMs: 0 };
  if (typeof options !== "object" || Array.isArray(options)) {
    throw new TypeError("driver command options must be an object");
  }
  const allowed = new Set(["timeoutMs"]);
  for (const k of Object.keys(options)) {
    if (!allowed.has(k)) throw new TypeError("unknown driver command option: " + k);
  }
  const timeoutMs = options.timeoutMs ?? 0;
  if (!Number.isInteger(timeoutMs) || timeoutMs < 0) {
    throw new RangeError("timeoutMs must be a non-negative integer");
  }
  return { timeoutMs };
}

function waitTaskToTerminal(target, cmd, task, timeoutMs) {
  const deadline = timeoutMs > 0 ? (Date.now() + timeoutMs) : 0;
  while (true) {
    const remaining = deadline > 0 ? Math.max(0, deadline - Date.now()) : 0;
    const msg = deadline > 0 ? task.waitNext(remaining) : task.waitNext();

    if (!msg) {
      if (task.done) {
        const err = new Error(task.errorText || ("driver exited during command: " + cmd));
        err.code = task.exitCode;
        throw err;
      }
      target.terminate();
      const err = new Error("Command timeout: " + cmd + " (" + timeoutMs + "ms)");
      err.code = "ETIMEDOUT";
      throw err;
    }

    if (msg.status === "event") continue;
    if (msg.status === "error") {
      const err = new Error(msg.data?.message || ("Command failed: " + cmd));
      err.code = msg.code;
      err.data = msg.data;
      throw err;
    }
    if (msg.status === "done") return msg.data;

    target.terminate();
    throw new Error("Unexpected task status: " + msg.status);
  }
}
```

### 3.3 时序与资源释放

```text
Project.start / fixed_rate tick
  ↓
InstanceManager.startInstance()
  ↓
QProcess started -> instance.status = running
  ↓
if schedule.runTimeoutMs > 0:
    arm runTimeoutTimer
  ↓
service JS:
    const drv = await openDriver(...)
    await drv.some_command(params, { timeoutMs })
      ↓
      target.request(cmd, params)
      ↓
      loop task.waitNext(remaining)
        ├─ event -> continue
        ├─ done  -> resolve
        ├─ error -> reject
        ├─ timeout -> terminate driver, reject timeout
        └─ driver exited -> reject exit error
  ↓
service exits
  ↓
InstanceManager.onProcessFinished()
  ├─ stop watchdog
  └─ clean temp config / process / logs writer / instance map
```

资源释放原则：

- service 超时：由 `InstanceManager` 直接 `kill()` service 进程；清理由既有 `finished -> onProcessFinished()` 统一完成。
- driver 命令超时：由 proxy 关闭当前 driver；该 proxy 后续不再复用。
- `$rawRequest()` 保持原样，不自动注入命令级超时。

### 3.4 向后兼容与边界

| 维度 | 无本次改动 | 有本次改动 |
|------|------------|------------|
| `Project.schedule` | 仅 `type/intervalMs/maxConcurrent/restartDelayMs/maxConsecutiveFailures` | 新增可选 `runTimeoutMs`；缺省行为不变 |
| `drv.xxx(params)` | Promise 直到 `done/error` | 仍可无改动调用 |
| `drv.xxx(params, options)` | 不支持 | 新增 `timeoutMs`，仅该命令生效 |
| `$rawRequest()` | 返回原始 `Task` | 完全不变 |
| driver 协议 | 无 timeout 字段 | 完全不变 |

边界说明：

- 命令超时是“宿主等待超时”，不是“driver 内部网络请求超时”。
- 如果 command 在 driver 内部已经发出不可撤销的外部副作用，超时时只能通过杀掉 driver 停止本地等待，不能回滚外部状态。
- `runTimeoutMs` 建议首先覆盖 `manual` / `fixed_rate` one-shot 场景；若 `daemon` 也配置该字段，则语义为“单个 daemon 进程实例的最大运行时长”。

### 3.5 WebUI Schedule 配置约定

当前 WebUI 的 Project Schedule 不是 schema 驱动，而是硬编码表单，因此 `runTimeoutMs` 不能只停留在 server/API 层，必须同步进入前端类型与表单。

建议前端类型扩展如下：

```ts
export interface Schedule {
  type: ScheduleType;
  intervalMs?: number;
  maxConcurrent?: number;
  restartDelayMs?: number;
  maxConsecutiveFailures?: number;
  runTimeoutMs?: number;
}
```

表单交互约定：

| 调度类型 | 是否显示 `runTimeoutMs` | 默认值 | 文案 |
|---------|--------------------------|--------|------|
| `manual` | 显示 | `0` | `0 = disabled` |
| `fixed_rate` | 显示 | `0` | `0 = disabled` |
| `daemon` | 显示 | `0` | `0 = disabled` |

表单行为约束：

- 前端保存时沿用现有 `schedule` 整体提交方式，不新增单独 API。
- 输入组件使用整数毫秒；`0` 表示禁用 watchdog。
- 前端校验与后端校验保持一致：仅允许 `>= 0` 的整数值。
- 多语言文案至少补 `en/zh/zh-TW`，其余语言可以先回退英文或补齐同义文案，但不能出现空 key。

## 4. 实现步骤

### 4.1 扩展 `Schedule` 与 `Project` 序列化

- 修改 `src/stdiolink_server/model/schedule.h`：
  - 新增 `int runTimeoutMs = 0;`
  - 声明该字段为 `0=禁用`
  - 关键定义：
    ```cpp
    struct Schedule {
        // ...
        int runTimeoutMs = 0;
    };
    ```
- 修改 `src/stdiolink_server/model/schedule.cpp`：
  - 在 `fromJson()` 中读取 `runTimeoutMs`
  - 约束：`runTimeoutMs >= 0`
  - 在 `toJson()` 中对三类 schedule 都输出 `runTimeoutMs`
  - 关键伪代码：
    ```cpp
    schedule.runTimeoutMs = obj.value("runTimeoutMs").toInt(0);
    if (schedule.runTimeoutMs < 0) {
        error = "schedule.runTimeoutMs must be >= 0";
        return schedule;
    }
    ```
- 复核 `src/stdiolink_server/model/project.cpp`：
  - 保持 `Project::fromJson()` / `toJson()` 通过 `Schedule` 统一承载新字段
  - 不新增新的 project 顶层字段

改动理由：
- `runTimeoutMs` 属于调度与实例生命周期策略，应放在 `schedule`，而不是 service 业务 `config`。

验收方式：
- T01-T04。

### 4.2 为 `InstanceManager` 增加运行期 watchdog

- 修改 `src/stdiolink_server/model/instance.h`：
  - 新增 watchdog timer 与 timeout 元数据字段
  - 建议字段：
    ```cpp
    std::unique_ptr<QTimer> runTimeoutTimer;
    int runTimeoutMs = 0;
    bool timedOut = false;
    QString timeoutReason;
    ```
- 修改 `src/stdiolink_server/manager/instance_manager.cpp`：
  - `startInstance()` 中把 `project.schedule.runTimeoutMs` 拷入 `Instance`
  - `QProcess::started` 回调中，在状态切为 `running` 后创建/启动 `runTimeoutTimer`
  - timer 到期后：
    ```cpp
    inst->timedOut = true;
    inst->timeoutReason = QStringLiteral("service run timeout (%1 ms)").arg(inst->runTimeoutMs);
    inst->logWriter->appendStderr(inst->timeoutReason.toUtf8() + "\n");
    inst->process->kill();
    ```
  - `onProcessFinished()` 中统一停止/销毁 timer，避免悬挂
- 修改 `src/stdiolink_server/manager/instance_manager.h`：
  - 若需要对外查询 timeout 元数据，可补充只读辅助接口；本里程碑不强制新增 signal

改动理由：
- server 层必须能对 one-shot service 卡死场景兜底，M101 才能可靠调度。

验收方式：
- T05-T08，S01-S02。

### 4.3 扩展 Driver Proxy 命令超时

- 修改 `src/stdiolink_service/proxy/driver_proxy.cpp`：
  - 为命令函数增加第二个可选参数 `options`
  - 新增内部 helper：`normalizeCommandOptions()`、`waitTaskToTerminal()`
  - 保持 `busy` 语义不变；超时、异常、终态都必须释放 `busy`
  - 关键生成代码片段：
    ```js
    if (typeof prop === "string" && commands.has(prop)) {
      return (params = {}, options) => {
        if (busy) throw new Error("DriverBusyError: request already in flight");
        const opts = normalizeCommandOptions(options);
        busy = true;
        try {
          const task = target.request(prop, params);
          return Promise.resolve(waitTaskToTerminal(target, prop, task, opts.timeoutMs))
            .finally(() => { busy = false; });
        } catch (e) {
          busy = false;
          throw e;
        }
      };
    }
    ```
- 修改 `src/stdiolink/doc/doc_generator.cpp`：
  - 为生成的 TypeScript proxy 接口增加 `DriverCommandCallOptions`
  - 每个命令签名从 `cmd(params?)` 改为 `cmd(params?, options?)`
  - 保持 `$rawRequest()` 与 `$close()` 声明不变

改动理由：
- 统一超时入口必须落在日常业务最常用的 `drv.xxx(...)` 路径上，而不是要求每个 service 手写 `$rawRequest()` 循环。

验收方式：
- T09-T15，S03-S04。

### 4.4 新增测试替身、单测与冒烟脚本

- 新增 `src/tests/test_slow_command_driver_main.cpp`：
  - 最小 driver stub，支持以下命令：
    ```text
    ping          -> 立即 done
    delayed_done  -> sleep N ms 后 done
    delayed_error -> sleep N ms 后 error
    delayed_exit  -> sleep N ms 后直接 exit
    delayed_batch -> 可选先发 event，再 sleep N ms 后 done
    ```
  - 参数建议：`delayMs`、`emitEvent`
- 修改 `src/tests/test_proxy_and_scheduler.cpp`：
  - 增加 proxy command timeout 用例
- 修改 `src/tests/test_instance_manager.cpp`、`src/tests/test_schedule.cpp`：
  - 增加 schedule / runtime watchdog 覆盖
- 修改 `src/tests/test_doc_generator.cpp`：
  - 增加 TypeScript 声明断言
- 新增 `src/smoke_tests/m101a_service_driver_timeout.py`
- 修改 `src/smoke_tests/run_smoke.py` 与 `src/smoke_tests/CMakeLists.txt`

改动理由：
- 超时相关逻辑只有在“慢、挂、退出、事件流”这些失败路径上才能真正验证；必须有本地可控替身。

验收方式：
- T01-T15，S01-S04。

### 4.5 补齐 WebUI Schedule 表单与前端测试

- 修改 `src/webui/src/types/project.ts`：
  - 为 `Schedule` 增加 `runTimeoutMs?: number`
  - 确保 Project 详情、创建向导、列表等依赖该类型的页面可正常编译
- 修改 `src/webui/src/pages/Projects/components/ScheduleForm.tsx`：
  - 在现有 `manual` / `fixed_rate` / `daemon` 表单区域中加入 `runTimeoutMs` 输入项
  - 输入逻辑示例：
    ```tsx
    <Form.Item label={t('projects.schedule.run_timeout')}>
      <InputNumber
        min={0}
        step={100}
        value={value.runTimeoutMs ?? 0}
        onChange={(v) => onChange({ ...value, runTimeoutMs: v ?? 0 })}
      />
    </Form.Item>
    ```
- 修改 `src/webui/src/locales/*.json`：
  - 新增 `projects.schedule.run_timeout`
  - 新增 `projects.schedule.run_timeout_hint` 或等价说明文案
- 修改 `src/webui/src/pages/Projects/__tests__/ProjectSchedule.test.tsx`：
  - 验证表单渲染该字段
  - 验证修改后保存 payload 包含 `runTimeoutMs`

改动理由：
- 如果前端不支持该字段，用户无法在 Project Schedule 页面直接配置 service timeout，M101 实际落地会退化成“只能手改 JSON”。

验收方式：
- T16-T18。

### 4.6 同步更新手册与开发文档

- 修改 `doc/manual/` 下与 Project Schedule、JS runtime driver 调用相关的文档：
  - 增加 `runTimeoutMs` 的字段说明、取值语义与适用范围
  - 增加 `drv.xxx(params, { timeoutMs })` 的调用示例、错误语义和“超时后 driver 不可复用”说明
- 修改相关 milestone / 设计文档：
  - 将 M101 主里程碑中依赖 timeout 的部分替换为本里程碑落地后的正式契约
  - 如实现过程中与本文档产生偏差，必须同步回写本里程碑文档
- 文档同步要求：
  - 新增字段、新增调用签名、新增错误语义都必须有对应文档落点
  - 不允许代码已合入但手册仍保留旧契约

改动理由：
- 超时能力属于公共契约变更；如果代码已改而文档未同步，M101 和后续 service 开发会直接基于过时信息实现。

验收方式：
- T15，外加文档检查项见 DoD。

## 5. 文件变更清单

### 5.1 新增文件
- `doc/milestone/milestone_0101a_service_driver_timeout_foundation.md` - M101 前置子里程碑开发计划
- `src/tests/test_slow_command_driver_main.cpp` - Driver 命令超时测试替身
- `src/smoke_tests/m101a_service_driver_timeout.py` - service/server/driver timeout 冒烟脚本

### 5.2 修改文件
- `src/stdiolink_server/model/schedule.h` - 新增 `runTimeoutMs`
- `src/stdiolink_server/model/schedule.cpp` - `runTimeoutMs` parse/serialize/校验
- `src/stdiolink_server/model/project.cpp` - 通过 `Schedule` 输出新字段
- `src/stdiolink_server/model/instance.h` - 保存 watchdog/timedOut 元数据
- `src/stdiolink_server/manager/instance_manager.h` - timeout 相关接口或辅助声明
- `src/stdiolink_server/manager/instance_manager.cpp` - 运行期 watchdog
- `src/stdiolink_service/proxy/driver_proxy.cpp` - `drv.xxx(params, options)` 超时实现
- `src/stdiolink/doc/doc_generator.cpp` - TypeScript 命令签名同步
- `src/webui/src/types/project.ts` - `Schedule` 类型新增 `runTimeoutMs`
- `src/webui/src/pages/Projects/components/ScheduleForm.tsx` - Project Schedule 表单新增 timeout 输入
- `src/webui/src/locales/en.json` - 英文文案新增 timeout 字段
- `src/webui/src/locales/zh.json` - 简中文案新增 timeout 字段
- `src/webui/src/locales/zh-TW.json` - 繁中文案新增 timeout 字段
- `doc/manual/*` - Project Schedule 与 JS driver timeout 相关手册更新
- `doc/milestone/milestone_0101_bin_scan_orchestrator.md` - 引用 timeout 能力的主里程碑文档同步
- `src/tests/CMakeLists.txt` - 注册 slow-command driver 与新增测试/冒烟
- `src/smoke_tests/run_smoke.py` - 注册 M101A smoke plan
- `src/smoke_tests/CMakeLists.txt` - 接入 `smoke_m101a_service_driver_timeout`

### 5.3 测试文件
- `src/tests/test_schedule.cpp` - `runTimeoutMs` parse/serialize
- `src/tests/test_instance_manager.cpp` - service runtime watchdog
- `src/tests/test_proxy_and_scheduler.cpp` - driver command timeout
- `src/tests/test_doc_generator.cpp` - TypeScript 声明回归
- `src/webui/src/pages/Projects/__tests__/ProjectSchedule.test.tsx` - Schedule 表单 timeout 配置回归

## 6. 测试与验收

### 6.1 单元测试

- 测试对象:
  - `Schedule::fromJson()` / `toJson()`
  - `InstanceManager` 运行期 watchdog
  - JS Driver Proxy 命令超时与兼容行为
  - TypeScript 文档生成
  - WebUI Project Schedule 表单
- 用例分层:
  - 正常路径：配置成功、命令成功、事件后成功
  - 边界值：`runTimeoutMs=0`、`timeoutMs=0`
  - 异常输入：负数 timeout、未知 options 字段
  - 错误传播：driver 命令报错、driver 提前退出、service 被 watchdog kill
  - 兼容回归：旧调用方式不变、`$rawRequest()` 不受影响
- 断言要点:
  - parse 错误文本
  - 进程是否被 kill
  - proxy Promise resolve/reject 语义
  - `busy` 标志释放
  - 生成的 TypeScript 签名文本
- 桩替身策略:
  - `test_service_stub` 触发 server timeout
  - `test_slow_command_driver` 触发 command done/error/exit/event/timeout
  - 复用现有 `calculator` driver 覆盖兼容与基础事件流
- 测试文件:
  - `src/tests/test_schedule.cpp`
  - `src/tests/test_instance_manager.cpp`
  - `src/tests/test_proxy_and_scheduler.cpp`
  - `src/tests/test_doc_generator.cpp`
  - `src/webui/src/pages/Projects/__tests__/ProjectSchedule.test.tsx`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `Schedule::fromJson()` | `runTimeoutMs` 缺失，默认禁用 | T01 |
| `Schedule::fromJson()` | `runTimeoutMs > 0` 被接受并 round-trip | T02 |
| `Schedule::fromJson()` | `runTimeoutMs < 0` 被拒绝 | T03 |
| `Project::toJson()` | `schedule.runTimeoutMs` 被透传输出 | T04 |
| `InstanceManager` runtime watchdog | 进程运行超时，被 kill 并清理 | T05 |
| `InstanceManager` runtime watchdog | 进程提前正常退出，不误杀 | T06 |
| `InstanceManager` startup/runtime 边界 | `starting` 仍走现有 5s 启动超时，不提前套用 `runTimeoutMs` | T07 |
| `InstanceManager` cleanup | timeout 后 timer 与实例记录被回收 | T08 |
| Driver Proxy 调用 | 旧签名 `drv.xxx(params)` 仍成功 | T09 |
| Driver Proxy 调用 | `drv.xxx(params, { timeoutMs })` 成功返回 | T10 |
| Driver Proxy options 校验 | 非对象或未知字段被拒绝 | T11 |
| Driver Proxy wait loop | `event -> done` 仍返回最终 `done` 数据 | T12 |
| Driver Proxy timeout | 超时后关闭 driver 并抛 `ETIMEDOUT` | T13 |
| Driver Proxy driver-exit | driver 提前退出时报 driver-exit 错误，不误报 timeout | T14 |
| TypeScript 生成 | 代理命令签名包含 `options?: DriverCommandCallOptions` | T15 |
| WebUI ScheduleForm | 渲染 `runTimeoutMs` 输入项 | T16 |
| WebUI ScheduleForm | 编辑后保存 payload 包含 `runTimeoutMs` | T17 |
| WebUI i18n | timeout 字段文案存在且可渲染 | T18 |

覆盖要求（硬性）: 核心功能场景 `100%` 有用例；非核心路径若本里程碑未覆盖，需在实现 PR 中补充“未覆盖原因 + 后续计划”。当前文档定义的核心路径即上表 T01-T18。

#### 执行约束（硬性）

- T05-T14 不得禁用；这些用例直接对应本里程碑核心承诺。
- T05/T13/T14 必须使用本地可控替身触发，禁止依赖随机 sleep 或外部网络。
- timeout 相关用例必须使用充足的时间裕量，避免 CI 抖动导致假阳性。

#### 用例详情

**T01 — Manual schedule 默认禁用 runTimeoutMs**
- 前置条件: 构造 `{ "type": "manual" }`
- 输入: `Schedule::fromJson(obj, error)`
- 预期: `error` 为空，`runTimeoutMs == 0`
- 断言: `schedule.type == Manual && schedule.runTimeoutMs == 0`

**T02 — FixedRate schedule 接受并回写 runTimeoutMs**
- 前置条件: 构造 `{ "type": "fixed_rate", "intervalMs": 1000, "runTimeoutMs": 2000 }`
- 输入: `fromJson()` 后再 `toJson()`
- 预期: 解析成功且输出仍包含 `runTimeoutMs=2000`
- 断言: `schedule.runTimeoutMs == 2000` 且 `json["runTimeoutMs"] == 2000`

**T03 — 负数 runTimeoutMs 被拒绝**
- 前置条件: 构造 `{ "type": "manual", "runTimeoutMs": -1 }`
- 输入: `Schedule::fromJson(obj, error)`
- 预期: 解析失败
- 断言: `error.contains("runTimeoutMs") == true`

**T04 — Project 序列化透传 runTimeoutMs**
- 前置条件: 构造 `Project`，其 `schedule.runTimeoutMs = 3000`
- 输入: `project.toJson()`
- 预期: JSON 中 `schedule.runTimeoutMs == 3000`
- 断言: `obj["schedule"].toObject()["runTimeoutMs"].toInt() == 3000`

**T05 — InstanceManager 会 kill 超时的 service**
- 前置条件: 使用 `test_service_stub`，配置 `_test.sleepMs = 5000`，`schedule.runTimeoutMs = 100`
- 输入: `startInstance()`
- 预期: 实例进入 `running` 后被 watchdog kill，最终从实例表移除
- 断言: 在超时时间窗口内收到 `instanceFinished`；日志包含 timeout 文本；`instanceCount(projectId) == 0`

**T06 — 正常提前退出的 service 不被误杀**
- 前置条件: `test_service_stub`，`sleepMs = 20`，`runTimeoutMs = 1000`
- 输入: `startInstance()`
- 预期: 进程正常退出
- 断言: `finished` 在超时前到达；无 timeout 标记；实例被正常清理

**T07 — starting 阶段仍由启动超时负责**
- 前置条件: 使用不会成功启动的 service program 或触发 `FailedToStart`
- 输入: `startInstance()`
- 预期: 仍按原有启动失败路径结束，而不是标成 runtime timeout
- 断言: `instanceStartFailed` 被发射；错误文本匹配启动失败路径

**T08 — runtime timeout 后 watchdog/timer 被释放**
- 前置条件: 复用 T05
- 输入: 等待 `onProcessFinished()` 清理完成
- 预期: `Instance` 不再残留；timer 不再触发二次 kill
- 断言: `instanceCount() == 0` 且进程/临时配置文件已回收

**T09 — 旧签名 drv.xxx(params) 保持兼容**
- 前置条件: 打开现有 `calculator` driver
- 输入: `await calc.add({ a: 5, b: 3 })`
- 预期: 成功返回
- 断言: `result.result == 8`

**T10 — drv.xxx(params, { timeoutMs }) 成功路径**
- 前置条件: 打开 `test_slow_command_driver`
- 输入: `await drv.delayed_done({ delayMs: 20 }, { timeoutMs: 1000 })`
- 预期: 在 deadline 内返回 done
- 断言: 返回对象中 `ok == true`

**T11 — 无效 command options 被拒绝**
- 前置条件: 打开任意可用 driver
- 输入: `drv.add({}, "bad")` 或 `drv.add({}, { foo: 1 })`
- 预期: 立即抛 `TypeError`
- 断言: 错误文本包含 `driver command options` 或 `unknown driver command option`

**T12 — event 后 done 仍返回最终结果**
- 前置条件: 打开 `test_slow_command_driver`
- 输入: `await drv.delayed_batch({ delayMs: 20, emitEvent: true }, { timeoutMs: 1000 })`
- 预期: 中间事件被消费但对调用方透明，最终成功
- 断言: Promise resolve；返回对象匹配 done payload

**T13 — 命令超时会关闭 driver 并抛 ETIMEDOUT**
- 前置条件: 打开 `test_slow_command_driver`
- 输入: `await drv.delayed_done({ delayMs: 5000 }, { timeoutMs: 50 })`
- 预期: 超时失败，driver 被终止
- 断言: 错误 `code == "ETIMEDOUT"`；后续同一 driver 再调用失败或 `running == false`

**T14 — driver 提前退出时不误报超时**
- 前置条件: 打开 `test_slow_command_driver`
- 输入: `await drv.delayed_exit({ delayMs: 20 }, { timeoutMs: 1000 })`
- 预期: 抛 driver-exit 错误
- 断言: 错误文本包含 `driver exited` 或 `without sending a response`；错误码不是 `ETIMEDOUT`

**T15 — TypeScript 声明包含命令 options 参数**
- 前置条件: 使用包含至少一个命令的 meta 调用 `DocGenerator::toTypeScript()`
- 输入: 生成 TS 文本
- 预期: 存在 `DriverCommandCallOptions` 接口与 `cmd(params?, options?)`
- 断言: `ts.contains("options?: DriverCommandCallOptions") == true`

**T16 — Schedule 表单渲染 runTimeoutMs 输入项**
- 前置条件: 渲染 `ProjectSchedule` 或 `ScheduleForm`，初始 `schedule.type = "manual"`
- 输入: 页面渲染
- 预期: 可见 timeout 输入组件
- 断言: `screen.getByLabelText(...)` 或等价 query 命中 `run_timeout` 字段

**T17 — 保存 schedule 时包含 runTimeoutMs**
- 前置条件: 渲染 `ProjectSchedule`，注入 `onSave` mock
- 输入: 将 timeout 修改为非 0 后点击保存
- 预期: 回调收到的 `schedule.runTimeoutMs` 为修改值
- 断言: `expect(onSave).toHaveBeenCalledWith(expect.objectContaining({ runTimeoutMs: 3000 }))`

**T18 — timeout 字段文案存在并可渲染**
- 前置条件: 加载默认 i18n 资源
- 输入: 渲染 ScheduleForm
- 预期: timeout 字段 label/hint 存在
- 断言: 页面上出现 `Run Timeout` / `运行超时` 或对应 key 已被解析，不显示裸 key

#### 测试代码

```cpp
TEST(ScheduleTest, T02_FixedRateRoundTripsRunTimeoutMs) {
    const QJsonObject obj{
        {"type", "fixed_rate"},
        {"intervalMs", 1000},
        {"maxConcurrent", 1},
        {"runTimeoutMs", 2000}
    };
    QString error;
    const Schedule schedule = Schedule::fromJson(obj, error);
    ASSERT_TRUE(error.isEmpty());
    EXPECT_EQ(schedule.runTimeoutMs, 2000);
    EXPECT_EQ(schedule.toJson().value("runTimeoutMs").toInt(), 2000);
}

TEST_F(JsProxyTest, T13_CommandTimeoutTerminatesDriver) {
    const QString driverPath = slowCommandDriverPath();
    const QString scriptPath = writeScript(
        m_tmpDir, "proxy_timeout.js",
        QString("import { openDriver } from 'stdiolink';\n"
                "(async () => {\n"
                "  const drv = await openDriver('%1');\n"
                "  try {\n"
                "    await drv.delayed_done({ delayMs: 5000 }, { timeoutMs: 50 });\n"
                "    globalThis.ok = 0;\n"
                "  } catch (e) {\n"
                "    globalThis.ok = (String(e.code) === 'ETIMEDOUT') ? 1 : 0;\n"
                "  }\n"
                "})();\n").arg(escapeJsString(driverPath)));
    ASSERT_FALSE(scriptPath.isEmpty());
    EXPECT_EQ(runScript(scriptPath), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "ok"), 1);
}
```

```tsx
it('T17 saves runTimeoutMs in schedule payload', async () => {
  const onSave = vi.fn().mockResolvedValue(true);
  render(<ProjectSchedule schedule={{ type: 'manual', runTimeoutMs: 0 }} onSave={onSave} />);
  fireEvent.change(screen.getByDisplayValue('0'), { target: { value: '3000' } });
  fireEvent.click(screen.getByTestId('save-schedule-btn'));
  await waitFor(() => {
    expect(onSave).toHaveBeenCalledWith(expect.objectContaining({ runTimeoutMs: 3000 }));
  });
});
```

### 6.2 冒烟测试脚本

- 脚本目录: `src/smoke_tests/`
- 脚本文件: `m101a_service_driver_timeout.py`
- 统一入口: `python src/smoke_tests/run_smoke.py --plan m101a_service_driver_timeout`
- CTest 接入: `smoke_m101a_service_driver_timeout`
- 覆盖范围:
  - Server `manual` 启动长运行 service 并由 `runTimeoutMs` 杀掉
  - Server 启动短任务 service 不误杀
  - `stdiolink_service` 运行 JS 脚本，通过 `drv.xxx(..., { timeoutMs })` 触发 driver 命令超时
  - 同一 JS 脚本在足够 timeout 下成功
- 用例清单:
  - `S01`: `manual` project + `runTimeoutMs=200ms` + `test_service_stub sleepMs=5000` -> 实例退出、日志含 timeout
  - `S02`: `manual` project + `runTimeoutMs=1000ms` + `sleepMs=20` -> 实例成功退出，无 timeout
  - `S03`: `stdiolink_service` + `test_slow_command_driver delayed_done(5000)` + `timeoutMs=50` -> service 非 0 退出，stderr 含 command timeout
  - `S04`: 同一 service 改为 `delayMs=20` + `timeoutMs=1000` -> service 退出码 `0`
- 失败输出规范:
  - 输出命令行、退出码、stdout/stderr、命中的日志/HTTP 响应
  - 若找不到可执行文件，必须列出候选路径并 `FAIL`
- 环境约束与跳过策略:
  - 默认要求存在 `stdiolink_server`、`stdiolink_service`、`test_service_stub`、`test_slow_command_driver`
  - 这些二进制缺失时应判定 `FAIL`，不允许静默通过
- 产物定位契约:
  - 优先查找 `build/runtime_debug/bin`、`build/runtime_release/bin` 以及测试二进制目录
  - temp `data_root` 由脚本动态创建，service/project 文件写入临时目录
- 跨平台运行契约:
  - 使用 `subprocess.run(..., text=True, encoding="utf-8", errors="replace")`
  - Windows/Linux/macOS 均通过绝对路径运行可执行文件

### 6.3 集成/端到端测试

- `stdiolink_server` HTTP API -> `InstanceManager` -> `test_service_stub` 的完整 server timeout 链路
- `stdiolink_service` -> JS runtime -> Driver Proxy -> `test_slow_command_driver` 的完整 command timeout 链路
- Project/API 输出对 `schedule.runTimeoutMs` 的序列化兼容性验证

### 6.4 验收标准

- [ ] `Project.schedule` 已支持 `runTimeoutMs` 且 parse/serialize 行为稳定（T01-T04）
- [ ] `InstanceManager` 可对已进入 `running` 的 service 实例执行超时 kill，并完成清理（T05-T08, S01-S02）
- [ ] JS `drv.xxx(params, { timeoutMs })` 已可对任意命令提供统一超时能力，且旧调用方式不回归（T09-T14, S03-S04）
- [ ] TypeScript 文档声明已同步新的命令调用签名（T15）
- [ ] WebUI Project Schedule 页面已支持配置并保存 `runTimeoutMs`（T16-T18）
- [ ] 冒烟脚本已接入统一入口并能覆盖 server/service 两层 timeout 主链路（S01-S04）

## 7. 风险与控制

- 风险: command timeout 只结束本地 driver 进程，无法回滚已发出的外部副作用
  - 控制: 文档与错误语义明确说明其是“宿主等待超时”，不是业务回滚
  - 控制: M101 业务层在有副作用的命令后自行实现幂等/重试策略
  - 测试覆盖: T13, T14, S03

- 风险: runtime watchdog 和启动超时混淆，导致 `starting` 阶段误杀
  - 控制: 仅在 `QProcess::started` 后 arm `runTimeoutTimer`
  - 控制: 保留现有 `5s` 启动超时逻辑不变
  - 测试覆盖: T05-T07

- 风险: proxy wait loop 处理不当导致 `busy` 标志泄漏或 driver 资源泄漏
  - 控制: 统一在 helper 外层 `try/finally` 释放 `busy`
  - 控制: 超时分支固定 `terminate()` 当前 driver
  - 测试覆盖: T10-T14

- 风险: 新增字段未同步到 API/TS 文档，WebUI 或调用方看不到新能力
  - 控制: `Project::toJson()`、`DocGenerator::toTypeScript()` 与对应测试同步修改
  - 测试覆盖: T04, T15

- 风险: server/API 已支持 `runTimeoutMs`，但 WebUI 未暴露该字段，导致用户只能手改 JSON
  - 控制: 把 `ScheduleForm`、`Schedule` 前端类型、多语言文案和页面测试纳入同一里程碑
  - 测试覆盖: T16-T18

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] 冒烟测试脚本已新增并接入统一入口（`run_smoke.py`）与 CTest
- [ ] 冒烟测试在目标环境执行通过（或有明确 fail/skip 记录）
- [ ] `schedule.runTimeoutMs` 与 `drv.xxx(params, { timeoutMs })` 已形成稳定契约
- [ ] 文档同步完成，并明确了与 M101 的前置依赖关系
- [ ] 代码对应的手册、milestone 文档和对外契约说明已同步更新
- [ ] 向后兼容策略确认：未配置超时的旧项目/旧脚本不受影响
