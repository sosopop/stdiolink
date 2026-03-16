# 里程碑 0104：通用可执行进程 Service（exec_runner）

> **前置条件**: 已有 `stdiolink/process.spawn()` 回调能力；已有 `Project.schedule.runTimeoutMs` 调度超时能力
> **目标**: 提供一个可通过配置启动任意外部进程的通用 Service，统一使用 `spawn()` 启动并实时输出日志；进程是一次退出还是常驻运行，由被执行进程自身行为和 Project 调度共同决定

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| Service（`src/data_root/services/exec_runner/`） | 完整 Service 三件套（`manifest.json` + `config.schema.json` + `index.js`） |
| Project 模板（`src/data_root/projects/`） | 至少两个默认 `disabled` 的 Project 模板：短任务模板 + 常驻任务模板 |
| 测试（`src/tests/` + `src/smoke_tests/`） | JS/Service 集成测试 + 冒烟测试脚本 |
| 知识库（`doc/knowledge/`） | 更新 Service 索引与新增 Service/Project 工作流说明 |

- 新增 `exec_runner` Service，通过配置导出 `program`、`args`、`cwd`、`env`、`success_exit_codes`、日志开关等参数。
- Service 只提供一种执行模型：`spawn(program, args, options)`。
- 子进程 `stdout` / `stderr` 通过回调实时写入 Service 日志，不再等待进程退出后集中输出。
- 子进程退出码和 `exitStatus` 映射为 Service 成功/失败，供 Server 调度层统计与重启。
- 是否“一次执行即退出”还是“常驻运行”，由目标进程自身决定；Service 不再维护 `mode=oneshot/spawn/shell` 等运行模式。
- 调度超时不进入 Service 参数面；需要限制实例运行时长时，统一使用现有 `Project.schedule.runTimeoutMs`。
- 提供本地可控的测试路径，优先复用 `test_process_async_stub`，保证主链路和失败链路都可稳定复现。

## 2. 背景与问题

- 当前很多“启动某个外部程序”的需求都要为每个场景单独写一个 JS Service，重复度高。
- 现有草案把需求拆成 `oneshot` / `spawn` / `shell` 三种模式，导致 Service 和调度层职责混淆。
- 仓库已经具备 `spawn()` 所需的基础能力：`onStdout`、`onStderr`、`onExit`、`kill()`、`write()`、`closeStdin()` 都已存在。
- 对于“进程是否应在多久后被终止”，系统已有 `Project.schedule.runTimeoutMs` 能力；再在 Service 配一层 `timeout_ms` 会造成双重语义和优先级歧义。
- 对于“是否一次退出还是常驻运行”，更合理的边界是由目标进程自身行为决定，和 Windows 任务计划一样：调度器只负责何时启动、是否重启，不负责定义进程模式。

### 2.1 设计原则（强约束）

- **P1: 单一执行模型**。`exec_runner` 只负责“启动一个进程并等待它结束”，不再内置多种运行模式分支。
- **P2: 调度与执行解耦**。`manual` / `fixed_rate` / `daemon` 是 Project 调度语义，不是 Service 执行语义。
- **P3: 实时日志优先**。进程输出通过 `onStdout` / `onStderr` 即时转发，避免 `execAsync()` 这种完成后一次性收集输出的模型。
- **P4: shell 显式化**。如需 shell 特性，调用方自己把 `cmd.exe` / `/bin/sh` 配成 `program + args`；Service 不再提供 `command` 捷径字段。
- **P5: 超时单一归属**。Service 不新增 `timeout_ms` 外部参数；实例运行超时统一归 `schedule.runTimeoutMs` 管。
- **P6: V1 不做交互控制面**。不支持持续 stdin 交互、运行中动态写入、信号代理或自定义 kill 策略。

**范围**:
- 新增 `exec_runner` Service 三件套。
- 新增 2 个默认禁用的 Project 模板，用于演示短任务和常驻任务接法。
- 新增 Service 集成测试与冒烟测试，覆盖 `spawn` 成功路径、失败路径、日志回调和退出传播。
- 更新知识库，让“新增通用进程 Service”能从 `04-service` / `08-workflows` 检索到。

**非目标**:
- 不修改 `stdiolink/process` C++ 绑定层。
- 不修改 Server 调度引擎、`InstanceManager` 或 `runTimeoutMs` 既有契约。
- 不新增 `exec_runner` 专用 HTTP API。
- 不改动 WebUI 通用 Service 表单机制之外的页面结构。
- 不支持持续 stdin 交互、运行中发送命令、stdout/stderr 结构化解析。
- 不在本里程碑中实现平台自适配 shell 模式、字符串拆参器或命令行 DSL。

## 3. 技术要点

### 3.1 Service 配置契约与调度边界

旧草案中的“多模式 + 命令字符串 + Service 级 timeout”需要收敛为单一契约：

| 主题 | 旧草案 | 本里程碑定稿 |
|------|--------|--------------|
| 执行入口 | `execAsync` / `spawn` / shell 三分支 | 仅 `spawn()` |
| 进程模式 | `mode=oneshot/spawn/shell` | 无 `mode` 字段 |
| shell 命令 | `command` 字段 | 调用方显式配置 `program=cmd.exe` / `/bin/sh` |
| 进程超时 | `timeout_ms` 由 Service 自己处理 | 不暴露；统一交给 `schedule.runTimeoutMs` |
| 参数表达 | 字符串拆参 | 结构化 `args: array<string>`、`env: object` |

`config.schema.json` 建议定稿如下：

```json
{
  "program": {
    "type": "string",
    "required": true,
    "description": "要启动的可执行文件路径或名称",
    "constraints": {
      "minLength": 1
    }
  },
  "args": {
    "type": "array",
    "default": [],
    "description": "命令行参数数组，按顺序透传给目标进程",
    "items": {
      "type": "string"
    }
  },
  "cwd": {
    "type": "string",
    "default": "",
    "description": "工作目录；空字符串表示沿用 Service 当前工作目录"
  },
  "env": {
    "type": "object",
    "default": {},
    "description": "附加环境变量；与系统环境合并",
    "additionalProperties": true
  },
  "success_exit_codes": {
    "type": "array",
    "default": [0],
    "description": "视为成功的退出码列表",
    "items": {
      "type": "int"
    }
  },
  "log_stdout": {
    "type": "bool",
    "default": true,
    "description": "是否将 stdout chunk 实时输出到日志"
  },
  "log_stderr": {
    "type": "bool",
    "default": true,
    "description": "是否将 stderr chunk 实时输出到日志"
  }
}
```

边界约束：

- Service 内部固定 `timeoutMs = 0`，不从配置读取。
- 若需要限制单个实例最长运行时长，Project 侧使用 `schedule.runTimeoutMs`。
- `args`、`env`、`success_exit_codes` 采用结构化类型，直接复用现有 schema / CLI path 能力，不再手写字符串解析器。
- `program` 的“必填且非空”优先交给现有 schema 校验处理；JS 层只保留防御式检查，不再重复发明另一套字段校验协议。

### 3.2 执行流程与实时日志语义

核心执行流程如下：

```text
getConfig()
  ↓
validate(program, args, success_exit_codes)
  ↓
spawn(program, args, { cwd?, env? })
  ↓
onStdout(chunk) ──> logger.info("stdout", { data: chunk })
onStderr(chunk) ──> logger.warn("stderr", { data: chunk })
  ↓
onExit({ exitCode, exitStatus })
  ├─ exitStatus == "crash"                     -> reject / throw
  ├─ exitCode ∈ success_exit_codes            -> resolve
  └─ exitCode ∉ success_exit_codes            -> reject / throw
```

建议的主逻辑伪代码：

```js
import { getConfig } from "stdiolink";
import { createLogger } from "stdiolink/log";
import { spawn } from "stdiolink/process";

const cfg = getConfig();
const logger = createLogger({ service: "exec_runner" });

function normalizeArgs(value) {
  return Array.isArray(value) ? value.map((v) => String(v)) : [];
}

function normalizeEnv(value) {
  return (value && typeof value === "object" && !Array.isArray(value)) ? value : undefined;
}

function normalizeSuccessCodes(value) {
  if (!Array.isArray(value) || value.length === 0) return new Set([0]);
  return new Set(value.map((v) => Number(v)).filter((v) => Number.isInteger(v)));
}

function runProcess() {
  const program = String(cfg.program ?? "");
  if (!program) throw new Error("program is required");

  const args = normalizeArgs(cfg.args);
  const env = normalizeEnv(cfg.env);
  const cwd = String(cfg.cwd ?? "");
  const successCodes = normalizeSuccessCodes(cfg.success_exit_codes);
  const logStdout = cfg.log_stdout !== false;
  const logStderr = cfg.log_stderr !== false;

  return new Promise((resolve, reject) => {
    const handle = spawn(program, args, {
      cwd: cwd || undefined,
      env,
    });

    logger.info("exec_runner start", { program, args });

    if (logStdout) {
      handle.onStdout((chunk) => logger.info("stdout", { data: chunk }));
    }
    if (logStderr) {
      handle.onStderr((chunk) => logger.warn("stderr", { data: chunk }));
    }

    handle.onExit((info) => {
      logger.info("exec_runner exit", info);
      if (info.exitStatus === "crash") {
        reject(new Error("process crashed"));
        return;
      }
      if (!successCodes.has(info.exitCode)) {
        reject(new Error(`process exited with code ${info.exitCode}`));
        return;
      }
      resolve();
    });
  });
}

await runProcess();
```

### 3.3 错误处理与退出码策略

| 场景 | 行为 | Service 退出语义 |
|------|------|------------------|
| `program` 缺失或空字符串 | schema 校验失败；JS 层保留防御式检查 | 非零退出 |
| `spawn()` 启动失败（程序不存在等） | `onExit` 收到 `exitCode=-1, exitStatus="crash"`，按失败处理 | 非零退出 |
| `stdout` 输出 | 按 chunk 实时记 `info` | 不改变退出语义 |
| `stderr` 输出 | 按 chunk 实时记 `warn` | 不改变退出语义 |
| `exitStatus == "crash"` | 记录 `error` | 非零退出 |
| `exitCode` 在 `success_exit_codes` 内 | 记录 `info` | 0 |
| `exitCode` 不在 `success_exit_codes` 内 | 记录 `error` | 非零退出 |

日志约束：

- 不在 Service 内额外缓存完整 stdout/stderr 再统一输出，避免大输出场景重复占用内存。
- `stdout` / `stderr` 的 chunk 边界以 `spawn()` 回调实际到达为准，不强制在 Service 层重切分。
- 如业务需要 shell 能力，推荐这样配置：

```json
{
  "program": "cmd.exe",
  "args": ["/c", "echo hello from shell"]
}
```

或

```json
{
  "program": "/bin/sh",
  "args": ["-c", "echo hello from shell"]
}
```

### 3.4 向后兼容与边界

- 本里程碑是纯新增 Service，对现有 Service / Project / API 无破坏性变更。
- 与旧草案相比，文档明确删除 `mode`、`command`、`timeout_ms`、`input` 四类字段；实现时不得再把这些字段作为 v1 契约写回去。
- 如后续确实需要“启动后自动写一次 stdin”或“运行中交互”，应单开后续里程碑，不在当前实现中隐式保留未使用字段。

## 4. 实现步骤

### 4.1 Service 三件套（`src/data_root/services/exec_runner/`）

- 新增 `manifest.json`：
  ```json
  {
    "manifestVersion": "1",
    "id": "exec_runner",
    "name": "通用进程执行服务",
    "version": "1.0.0",
    "description": "启动任意外部进程并实时输出日志"
  }
  ```

- 新增 `config.schema.json`：
  - 字段以 `program` / `args` / `cwd` / `env` / `success_exit_codes` / `log_stdout` / `log_stderr` 为准。
  - 不新增 `mode`、`command`、`timeout_ms`、`input`。

- 新增 `index.js`：
  - 涉及模块：`stdiolink`、`stdiolink/log`、`stdiolink/process`
  - 关键入口：
    ```js
    async function main() {
      await runProcess();
      logger.info("exec_runner completed successfully");
    }

    main().catch((error) => {
      logger.error("exec_runner failed", {
        message: error?.message ?? String(error),
      });
      throw error;
    });
    ```
  - 改动理由：把“执行一个外部进程”沉淀为通用 Service，而不是为每种命令单独造 Service。
  - 验收方式：`stdiolink_service <service_dir> --config.program=...` 可直接运行；日志实时可见；退出码传播正确。

### 4.2 Project 模板（`src/data_root/projects/`）

- 新增短任务模板 `exec_runner_task_template/`：
  - `config.json` 示例：
    ```json
    {
      "id": "exec_runner_task_template",
      "name": "exec_runner 短任务模板",
      "serviceId": "exec_runner",
      "enabled": false,
      "schedule": {
        "type": "fixed_rate",
        "intervalMs": 60000,
        "maxConcurrent": 1,
        "runTimeoutMs": 30000
      }
    }
    ```
  - `param.json` 示例：
    ```json
    {
      "program": "your_program",
      "args": ["--do-work"],
      "success_exit_codes": [0],
      "log_stdout": true,
      "log_stderr": true
    }
    ```

- 新增常驻任务模板 `exec_runner_daemon_template/`：
  - `config.json` 示例：
    ```json
    {
      "id": "exec_runner_daemon_template",
      "name": "exec_runner 常驻任务模板",
      "serviceId": "exec_runner",
      "enabled": false,
      "schedule": {
        "type": "daemon",
        "restartDelayMs": 5000,
        "maxConsecutiveFailures": 3,
        "runTimeoutMs": 0
      }
    }
    ```
  - `param.json` 示例：
    ```json
    {
      "program": "your_long_running_program",
      "args": ["--serve"],
      "success_exit_codes": [0],
      "log_stdout": true,
      "log_stderr": true
    }
    ```

- 改动理由：
  - 让模板明确体现“同一 Service，不同调度语义”。
  - 模板默认 `disabled`，避免仓库自带示例在不同平台误启动。
- 验收方式：Server 能扫描到模板 Project；启用后按 `schedule.type` 拉起实例。

### 4.3 测试与接入（`src/tests/` + `src/smoke_tests/`）

- 新增 `src/tests/test_exec_runner_service.cpp`：
  - 通过 `stdiolink_service` 直接运行 `exec_runner` Service。
  - 复用 `test_process_async_stub` 作为被启动的本地桩进程。
  - 测试辅助函数应解析出 runtime `bin/` 下的绝对可执行路径，避免依赖当前 `PATH` 或偶然工作目录。
  - 关键代码框架：
    ```cpp
    TEST_F(ExecRunnerServiceTest, T01_SpawnSuccessStdout) {
        auto r = runService({
            "--config.program=" + stubPath("test_process_async_stub"),
            "--config.args[0]=--mode=stdout",
            "--config.args[1]=--text=hello_exec_runner"
        });
        EXPECT_EQ(r.exitCode, 0);
        EXPECT_TRUE(r.stdoutStr.contains("hello_exec_runner")
                 || r.stderrStr.contains("hello_exec_runner"));
    }
    ```

- 新增 `src/smoke_tests/m104_exec_runner.py`：
  - 统一入口注册到 `run_smoke.py`，计划名使用现有风格 `m104_exec_runner`。
  - 主成功链路和关键失败链路都走 `test_process_async_stub`。

- 改动理由：保证该 Service 不是“只写文档无验证”的方案，而是能在 runtime 中独立执行并被 smoke 覆盖。
- 验收方式：新增 GTest 与 smoke 用例都能在本地 runtime 产物上执行。

## 5. 文件变更清单

### 5.1 新增文件

- `src/data_root/services/exec_runner/manifest.json` - Service 清单
- `src/data_root/services/exec_runner/config.schema.json` - Service 参数定义
- `src/data_root/services/exec_runner/index.js` - Service 主入口
- `src/data_root/projects/exec_runner_task_template/config.json` - 短任务模板 Project 配置
- `src/data_root/projects/exec_runner_task_template/param.json` - 短任务模板参数
- `src/data_root/projects/exec_runner_daemon_template/config.json` - 常驻任务模板 Project 配置
- `src/data_root/projects/exec_runner_daemon_template/param.json` - 常驻任务模板参数
- `src/tests/test_exec_runner_service.cpp` - Service 集成测试
- `src/smoke_tests/m104_exec_runner.py` - 冒烟测试脚本

### 5.2 修改文件

- `src/smoke_tests/run_smoke.py` - 注册 `m104_exec_runner` 冒烟入口
- `src/smoke_tests/CMakeLists.txt` - 接入 `smoke_m104_exec_runner`
- `doc/knowledge/04-service/README.md` - 补充 `exec_runner` 到 Service 索引
- `doc/knowledge/08-workflows/add-service-or-project.md` - 补充“通用进程 Service”接入路径

### 5.3 测试文件

- `src/tests/test_exec_runner_service.cpp` - `spawn` 主路径/失败路径/日志路径
- `src/smoke_tests/m104_exec_runner.py` - runtime 主链路冒烟

## 6. 测试与验收

### 6.1 单元测试（必填，重点）

- 测试对象：`exec_runner` Service 的进程启动、日志回调和退出传播行为
- 用例分层：正常路径、边界输入、异常输入、错误传播、调度边界说明
- 断言要点：退出码、stdout/stderr 日志出现、错误信息、`success_exit_codes` 判定
- 桩替身策略：优先复用 `test_process_async_stub`；失败路径通过本地可控 `--exit-code`、`--mode=stderr`、不存在程序名等方式触发；`env` 注入路径使用显式 shell 程序做最小验证，不再单独新增 env stub
- 测试文件：`src/tests/test_exec_runner_service.cpp`

#### 路径矩阵（必填）

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `runProcess()` | `program` 合法，stdout 正常输出，退出码 0 | T01 |
| `runProcess()` | `program` 合法，stderr 正常输出，退出码 0 | T02 |
| `schema + runProcess()` | `program` 缺失或空字符串，配置校验失败 | T03 |
| `spawn()` 启动 | 程序不存在，`onExit` 返回 `exitCode=-1, exitStatus=crash` | T04 |
| `onExit()` | 退出码不在 `success_exit_codes` 中 | T05 |
| `onExit()` | 非零退出码被配置为成功 | T06 |
| `args` 结构化传递 | `args[]` 按顺序透传到子进程 | T07 |
| `env` 结构化传递 | 附加环境变量对子进程可见 | T08 |
| `--dump-config-schema` | 输出包含结构化字段且不含被淘汰字段 | T09 |
| 调度边界 | `runTimeoutMs` 不属于 Service 参数面，文档性约束 | 不可达 — 由 `Project.schedule` 覆盖，非本 Service 分支 |

覆盖要求（硬性）：核心决策路径 `100%` 有用例；调度超时路径由既有 `runTimeoutMs` 测试体系覆盖，本里程碑不重复在 Service 层新增同义分支；`cwd` 透传与 `log_stdout=false` / `log_stderr=false` 属于低风险守卫或直接透传路径，本轮不作为关键验收项，若实现阶段出现回归再补 `T10/T11`。

#### 执行约束（硬性）

- 被验收标准引用的 T01-T09 必须实际执行，不得禁用。
- 所有失败路径都必须通过本地 stub 或本地不存在程序名触发，禁止依赖公网或外部服务。
- 快速退出子进程用例必须在断言前完整读取 stdout/stderr，避免尾包漏读。
- 对 `test_process_async_stub` 的调用必须使用绝对路径，或在测试辅助函数中显式把工作目录设到 runtime `bin/`。

#### 用例详情（必填）

**T01 — stdout 成功路径**
- 前置条件：`exec_runner` Service 和 `test_process_async_stub` 已组装到运行目录
- 输入：`--config.program=<stub_abs_path> --config.args[0]=--mode=stdout --config.args[1]=--text=hello_exec_runner`
- 预期：Service 退出码为 `0`，日志中出现 `hello_exec_runner`
- 断言：`exitCode == 0`；输出文本包含 `hello_exec_runner`

**T02 — stderr 实时输出路径**
- 前置条件：同上
- 输入：`--config.program=<stub_abs_path> --config.args[0]=--mode=stderr --config.args[1]=--text=stderr_line`
- 预期：Service 退出码为 `0`，日志中出现 `stderr_line`
- 断言：`exitCode == 0`；输出文本包含 `stderr_line`

**T03 — program 缺失**
- 前置条件：同上
- 输入：不传 `--config.program`
- 预期：`stdiolink_service` 在配置校验阶段直接失败
- 断言：`exitCode != 0`；stderr 含 `config validation failed`

**T04 — 程序不存在**
- 前置条件：同上
- 输入：`--config.program=nonexistent_exec_runner_binary_xyz`
- 预期：Service 非零退出，并按 `crash` 路径处理
- 断言：`exitCode != 0`；日志或 stderr 含 `crash` 或 `-1`

**T05 — 非零退出码判失败**
- 前置条件：同上
- 输入：`--config.program=<stub_abs_path> --config.args[0]=--mode=stdout --config.args[1]=--exit-code=7`
- 预期：Service 非零退出
- 断言：`exitCode != 0`；日志含 `process exited with code 7`

**T06 — 非零退出码白名单**
- 前置条件：同上
- 输入：`--config.program=<stub_abs_path> --config.args[0]=--mode=stdout --config.args[1]=--exit-code=7 --config.success_exit_codes[0]=0 --config.success_exit_codes[1]=7`
- 预期：Service 退出码为 `0`
- 断言：`exitCode == 0`

**T07 — args 数组透传**
- 前置条件：同上
- 输入：`--config.program=<stub_abs_path> --config.args[0]=--mode=stdout --config.args[1]=--text=arg_ok`
- 预期：子进程收到按顺序透传的参数
- 断言：输出包含 `arg_ok`

**T08 — env 对象透传**
- 前置条件：Windows 使用 `cmd.exe /c echo %MY_VAR%`，Unix 使用 `/bin/sh -c 'printf %s \"$MY_VAR\"'`
- 输入：显式 shell 程序 + `--config.env.MY_VAR=test123`
- 预期：子进程可读取 `MY_VAR`
- 断言：输出包含 `test123`

**T09 — schema 导出**
- 前置条件：`exec_runner` Service 目录存在
- 输入：`stdiolink_service <service_dir> --dump-config-schema`
- 预期：导出的 schema 含 `program`、`args`、`env`、`success_exit_codes`，且不含 `mode`、`command`、`timeout_ms`、`input`
- 断言：`exitCode == 0`；stdout 中字段集合匹配预期

#### 测试代码（必填）

```cpp
TEST_F(ExecRunnerServiceTest, T01_SpawnSuccessStdout) {
    auto r = runService({
        "--config.program=" + stubPath("test_process_async_stub"),
        "--config.args[0]=--mode=stdout",
        "--config.args[1]=--text=hello_exec_runner"
    });
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stdoutStr.contains("hello_exec_runner")
             || r.stderrStr.contains("hello_exec_runner"));
}

TEST_F(ExecRunnerServiceTest, T03_RejectsMissingProgram) {
    auto r = runService({});
    EXPECT_NE(r.exitCode, 0);
    EXPECT_TRUE(r.stderrStr.contains("config validation failed"));
}

TEST_F(ExecRunnerServiceTest, T05_NonZeroExitCodeFails) {
    auto r = runService({
        "--config.program=" + stubPath("test_process_async_stub"),
        "--config.args[0]=--mode=stdout",
        "--config.args[1]=--exit-code=7"
    });
    EXPECT_NE(r.exitCode, 0);
}

TEST_F(ExecRunnerServiceTest, T09_DumpConfigSchemaDoesNotExposeLegacyFields) {
    auto r = dumpSchema();
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stdoutStr.contains("\"program\""));
    EXPECT_FALSE(r.stdoutStr.contains("\"mode\""));
    EXPECT_FALSE(r.stdoutStr.contains("\"timeout_ms\""));
}
```

### 6.2 冒烟测试脚本（必填）

- 脚本目录：`src/smoke_tests/`
- 脚本文件：`m104_exec_runner.py`
- 统一入口：`python src/smoke_tests/run_smoke.py --plan m104_exec_runner`
- CTest 接入：`smoke_m104_exec_runner`
- 覆盖范围：`exec_runner` 在 runtime 产物中的最小成功链路 + 关键失败链路
- 用例清单：
  - `S01`：启动 runtime `bin/` 下的 `test_process_async_stub --mode=stdout --text=smoke_ok` -> 退出码 `0`
  - `S02`：启动不存在程序 -> 退出码非 `0`
  - `S03`：启动 `test_process_async_stub --exit-code=9`，且 `success_exit_codes=[0]` -> 退出码非 `0`
- 失败输出规范：打印执行命令、stdout、stderr、退出码、超时信息
- 环境约束与跳过策略：若 `stdiolink_service` 或 `test_process_async_stub` 不存在则判定 `FAIL`，禁止静默通过
- 产物定位契约：`build/runtime_debug/bin/stdiolink_service[.exe]` 与 `build/runtime_debug/bin/test_process_async_stub[.exe]`
- 跨平台运行契约：通过 `PlatformUtils::executablePath` 或脚本内后缀判断处理 `.exe`；参数使用数组，不依赖 shell 转义

### 6.3 集成/端到端测试

- `I01`：Server 集成：启用 `exec_runner_task_template` / `exec_runner_daemon_template` 后，`ServiceScanner` 能扫描到 Service，`ProjectManager` / `ServerManager` 能加载对应 Project 模板。
- 调度集成：`daemon` 模板的重启行为、`runTimeoutMs` 行为继续依赖既有 Server 调度测试与能力，不在本里程碑重复实现另一套超时逻辑。
- 日志链路：验证 Service 输出能进入实例日志文件，供后续 WebUI/日志页面查看。

### 6.4 验收标准

- [ ] `exec_runner` Service 可通过 `stdiolink_service <service_dir> --config.program=<stub_abs_path> --config.args[0]=--mode=stdout` 独立运行（T01, S01）
- [ ] `exec_runner` 的 `config.schema.json` 仅暴露单一 `spawn` 契约字段，不包含 `mode`、`command`、`timeout_ms`、`input`（T09）
- [ ] stdout/stderr 会通过回调实时写入日志，而不是等待进程结束后统一输出（T01, T02）
- [ ] `program` 缺失或程序不存在时，Service 非零退出并输出错误信息（T03, T04, S02）
- [ ] 子进程退出码会按 `success_exit_codes` 正确映射为 Service 成功/失败（T05, T06, S03）
- [ ] 模板 Project 可被 Server 扫描到，且其超时配置通过 `schedule.runTimeoutMs` 表达，而非 Service 参数（I01）
- [ ] 冒烟测试 `S01-S03` 全部通过

## 7. 风险与控制

- 风险：实现阶段又把 `mode` / `command` / `timeout_ms` 加回去，导致职责重新混淆
  - 控制：文档中把删除字段列为显式边界；`config.schema.json` 验收时逐项比对
  - 控制：单元测试和 smoke 只覆盖单一 `spawn` 契约，不为旧字段留测试入口
  - 测试覆盖：T01-T06, T09, S01-S03

- 风险：大输出场景下日志回调顺序或尾包处理不稳定
  - 控制：使用回调即到即记，不自行缓存整段输出
  - 控制：测试读取完整 stdout/stderr，并为快速退出场景保留尾包断言
  - 测试覆盖：T01, T02

- 风险：Project 模板使用平台特定命令导致仓库示例不可移植
  - 控制：模板默认 `disabled` 且使用占位参数，不把它们作为 smoke 对象
  - 控制：自动化验证统一依赖本地 stub，而不是平台 shell 或公网命令
  - 测试覆盖：S01-S03

- 风险：Service 自己实现超时后与 `schedule.runTimeoutMs` 冲突
  - 控制：明确不暴露 `timeout_ms` 字段，内部固定 `timeoutMs = 0`
  - 控制：文档在技术要点和验收标准中重复强调“超时归属 schedule”
  - 测试覆盖：文档性边界，结合既有 `runTimeoutMs` 测试体系验证

- 风险：测试依赖相对路径或 `PATH` 偶然命中，导致本地能过、CI 不稳定
  - 控制：单元测试与 smoke 一律解析 runtime `bin/` 下的绝对路径
  - 控制：测试辅助函数显式设置工作目录或输出候选路径
  - 测试覆盖：T01-T08, S01-S03

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] `exec_runner` Service 三件套已落地并可被 `ServiceScanner` 识别
- [ ] 冒烟测试脚本已新增并接入统一入口（`run_smoke.py`）与 CTest
- [ ] 冒烟测试在目标环境执行通过（或有明确失败记录，不以 skip 代替通过）
- [ ] 知识库 `doc/knowledge/04-service/README.md` 与 `doc/knowledge/08-workflows/add-service-or-project.md` 已同步
- [ ] 向后兼容确认：本里程碑为纯新增，不改动既有 Service/Project/C++ 绑定公共契约
- [ ] `exec_runner` 文档与实现均未引入 `mode`、`command`、`timeout_ms`、`input` 这些被明确排除的旧设计字段
