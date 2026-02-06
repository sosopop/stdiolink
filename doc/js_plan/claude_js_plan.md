# stdiolink JS Runtime 设计开发计划

## 1. 项目概述

### 1.1 目标

基于 [quickjs-ng](https://github.com/nicedoc/nicedoc.io) (QuickJS-NG) 引擎，为 stdiolink 框架开发 JavaScript 运行时绑定，产出一个独立可执行文件 `stdiolink_service`。用户编写 JS 脚本即可调用任意 stdiolink Driver，享受类型安全的开发体验。

### 1.2 核心特性

| 特性 | 说明 |
|------|------|
| Driver Host 绑定 | 在 JS 中启动、调用、管理 Driver 进程 |
| Proxy 代理调用 | 利用 JS Proxy 将 Driver 命令映射为对象方法调用 |
| ES Module 支持 | `import`/`export` 模块系统 |
| console.log 桥接 | `console.log/warn/error` 输出到 `qDebug/qWarning/qCritical` |
| 进程调用绑定 | JS 中执行外部进程并获取输出 |
| TypeScript 声明导出 | Driver 通过 `--export-doc=ts` 导出 `.d.ts` 类型声明文件 |

### 1.3 范围边界

本期为 MVP，以下内容**不在范围内**：

| 排除项 | 说明 |
|--------|------|
| JS 实现 Driver | 不提供 `defineDriver`，现有 Driver 仍为独立 exe，无需改动 |
| 单实例内多请求并发 | 单个 Driver 实例为单请求在途模型，不做实例内并行复用 |
| npm 生态兼容 | 不支持 `node_modules`、`package.json` 解析 |
| 浏览器 API polyfill | 不提供 `fetch`、`setTimeout`、DOM 等 |
| 多线程 JS runtime | 单线程执行，不支持 Worker |

### 1.4 产出物

- **`stdiolink_service`** — 可执行文件，执行单个 JS 入口文件，完成后退出
- **TypeScript 声明生成器** — 集成到现有 `--export-doc` 体系，新增 `ts` 格式

### 1.5 使用示例（最终效果）

```js
import { openDriver } from "stdiolink";

// 方式一：Proxy 代理调用（推荐）
const calc = await openDriver("./calculator_driver.exe");
const result = await calc.add({ a: 10, b: 20 });
console.log("Result:", result); // → qDebug 输出

// 方式二：底层 API 调用
import { Driver } from "stdiolink";
const d = new Driver();
d.start("./calculator_driver.exe");
const task = d.request("add", { a: 10, b: 20 });
const msg = task.waitNext();
console.log(msg.data);
d.terminate();
```

**多实例并发调用**：

```js
import { openDriver } from "stdiolink";

const drvA = await openDriver("./driver_a.exe");
const drvB = await openDriver("./driver_b.exe");

// 不同实例可并行，由 JsTaskScheduler 基于 waitAnyNext 调度
const [a, b] = await Promise.all([
  drvA.scan({ fps: 30 }),
  drvB.scan({ fps: 24 })
]);

await Promise.all([drvA.$close(), drvB.$close()]);
```

---

## 2. 架构设计

### 2.1 整体架构

```
┌─────────────────────────────────────────────────┐
│                 JS 用户脚本 (.js)                │
├─────────────────────────────────────────────────┤
│              stdiolink 内置模块                   │
│  ┌───────────┐ ┌──────────┐ ┌────────────────┐  │
│  │  Driver    │ │  Process │ │  openDriver()  │  │
│  │  Task      │ │  exec()  │ │  (Proxy 代理)  │  │
│  └───────────┘ └──────────┘ └────────────────┘  │
├─────────────────────────────────────────────────┤
│              JS Runtime 层                       │
│  ┌───────────┐ ┌──────────┐ ┌────────────────┐  │
│  │ QuickJS   │ │ Module   │ │  console 桥接  │  │
│  │ Engine    │ │ Loader   │ │  qDebug/qWarn  │  │
│  └───────────┘ └──────────┘ └────────────────┘  │
├─────────────────────────────────────────────────┤
│              C++ 绑定层                          │
│  ┌───────────┐ ┌──────────┐ ┌────────────────┐  │
│  │ Driver    │ │ Process  │ │  Meta → TS     │  │
│  │ Bindings  │ │ Bindings │ │  声明生成器     │  │
│  └───────────┘ └──────────┘ └────────────────┘  │
├─────────────────────────────────────────────────┤
│           stdiolink 核心库 (DLL)                 │
│  ┌───────────┐ ┌──────────┐ ┌────────────────┐  │
│  │ host/     │ │protocol/ │ │    doc/         │  │
│  │ Driver    │ │ JSONL    │ │  DocGenerator   │  │
│  │ Task      │ │ MetaTypes│ │  MetaExporter   │  │
│  └───────────┘ └──────────┘ └────────────────┘  │
└─────────────────────────────────────────────────┘
```

### 2.2 目录结构

```
src/
└── stdiolink_service/          # JS Runtime 可执行文件
    ├── CMakeLists.txt
    ├── main.cpp                # 入口：解析参数、初始化引擎、执行脚本
    ├── engine/
    │   ├── js_engine.h/cpp         # QuickJS 引擎封装
    │   ├── module_loader.h/cpp     # ES Module 加载器
    │   └── console_bridge.h/cpp    # console → qDebug 桥接
    ├── bindings/
    │   ├── js_driver.h/cpp         # Driver 类绑定
    │   ├── js_task.h/cpp           # Task 类绑定
    │   ├── js_process.h/cpp        # 进程调用绑定
    │   ├── js_task_scheduler.h/cpp # 基于 waitAnyNext 的多任务调度器
    │   └── js_stdiolink_module.h/cpp  # "stdiolink" 内置模块注册
    └── proxy/
        ├── driver_proxy.js         # Proxy 代理实现（JS 层）
        └── driver_proxy_gen.h/cpp  # openDriver() 的 C++ 辅助
```

### 2.3 quickjs-ng 集成方式

通过项目已有的 vcpkg 包管理方案引入 quickjs-ng，静态链接到 `stdiolink_service`：

1. 在 `vcpkg.json` 中添加依赖：
```json
{
  "dependencies": [
    "spdlog",
    "qtbase",
    "qtwebsockets",
    "gtest",
    "quickjs-ng"
  ]
}
```

2. 在顶层 `CMakeLists.txt` 中添加：
```cmake
find_package(qjs CONFIG REQUIRED)
```

3. 在子项目中链接：
```cmake
target_link_libraries(stdiolink_service PRIVATE qjs)
```

> **注意**：quickjs-ng 是 QuickJS 的活跃维护分支，支持最新 ES2023 特性，包括完整的 ES Module 支持。vcpkg 端口名为 `quickjs-ng`，CMake find_package 名为 `qjs`。

### 2.4 quickjs-ng 与官方 QuickJS API 差异

quickjs-ng 的接口和实现与官方 QuickJS 存在多处不兼容。**遇到编译问题时以 quickjs-ng 的 `quickjs.h` 头文件为准**，不要参考官方 QuickJS 文档或网上基于官方版本的示例代码。

#### 关键差异一览

| 差异项 | 官方 QuickJS | quickjs-ng | 影响 |
|--------|-------------|------------|------|
| `JS_NewClassID` | `JS_NewClassID(&id)` 全局静态分配 | `JS_NewClassID(rt, &id)` 基于 Runtime 分配 | Driver/Task 类注册必须传 `JSRuntime*` |
| 构建系统 | 纯 Makefile | CMake（vcpkg 已封装） | 无需手动处理 |
| ES 标准兼容性 | ES2020 部分 | ES2023+，Test262 通过率大幅提升 | Atomics、WeakRef 等可用 |
| Promise Job 队列 | 执行时机有 bug | 修复执行时机，Top-level await 更健壮 | 异步代码行为更可靠 |
| 非标扩展 | BigFloat / BigDecimal | 已移除，专注标准 BigInt | 不要使用 BigFloat/BigDecimal |
| Error 堆栈 | 基础堆栈信息 | 优化堆栈生成，调用栈更准确可读 | 错误诊断体验更好 |
| 模块解析 | 基础模块加载 | 增强模块加载器，路径解析更灵活 | ModuleLoader 实现可利用 |

#### 实践要点

1. **所有 API 调用以头文件实际签名为准**——不要从网上搜索官方 QuickJS 用法直接套用
2. **`JS_NewClassID` 是最常见的编译错误来源**——ng 版本需要 `JSRuntime*` 参数
3. **Top-level await 可直接使用**——ng 的 Promise Job 队列修复使其行为正确
4. **不要使用 BigFloat / BigDecimal**——ng 已移除这些非标扩展

---

## 3. 模块详细设计

### 3.1 JS 引擎封装 (`engine/js_engine`)

对 QuickJS Runtime/Context 的 RAII 封装，提供统一的脚本执行入口。

```cpp
class JsEngine {
public:
    JsEngine();
    ~JsEngine();

    // 注册内置模块（在 eval 之前调用）
    void registerModule(const QString& name, JSModuleDef* (*init)(JSContext*, const char*));

    // 执行脚本文件（作为 ES Module）
    int evalFile(const QString& filePath);

    // 驱动 Promise microtask 队列，返回是否还有待执行任务
    bool executePendingJobs();

    // 获取底层上下文（供绑定层使用）
    JSContext* context() const;
    JSRuntime* runtime() const;

private:
    JSRuntime* m_rt = nullptr;
    JSContext* m_ctx = nullptr;
};
```

**职责**：
- 创建/销毁 QuickJS Runtime 和 Context
- 配置内存限制、栈大小
- 注册所有内置模块（stdiolink、console 等）
- 以 ES Module 模式加载并执行入口文件
- **Promise job pump**：脚本执行后循环调用 `JS_ExecutePendingJob()` 驱动 microtask 队列，确保所有 `async/await` 和 Promise 链正确完成
- 统一错误处理：捕获 JS 异常并输出到 stderr

### 3.2 ES Module 加载器 (`engine/module_loader`)

支持文件系统模块和内置模块两种加载方式。

**模块解析规则**：

| import 路径 | 解析方式 |
|-------------|---------|
| `"stdiolink"` | 内置模块，C++ 注册 |
| `"./foo.js"` / `"../lib/bar.js"` | 相对路径，基于当前文件目录解析 |
| `"/abs/path.js"` | 绝对路径，直接加载 |

```cpp
class ModuleLoader {
public:
    // 注册为 QuickJS 的 module_normalize + module_loader
    static void install(JSContext* ctx);

    // 注册内置模块名称
    static void addBuiltin(const QString& name,
                           JSModuleDef* (*init)(JSContext*, const char*));

private:
    // QuickJS 回调：规范化模块名
    static char* normalize(JSContext* ctx,
                           const char* baseName,
                           const char* name, void* opaque);

    // QuickJS 回调：加载模块
    static JSModuleDef* loader(JSContext* ctx,
                               const char* moduleName, void* opaque);
};
```

**关键设计**：
- 内置模块（如 `"stdiolink"`）通过名称匹配直接返回预注册的 `JSModuleDef`
- 文件模块读取 `.js` 文件内容，调用 `JS_Eval` 以 `JS_EVAL_TYPE_MODULE` 编译
- 模块路径规范化：将相对路径转为绝对路径，避免重复加载

### 3.3 console 桥接 (`engine/console_bridge`)

将 JS 的 `console` 对象方法映射到 Qt 日志系统。

**映射关系**：

| JS 方法 | Qt 输出 | 说明 |
|---------|---------|------|
| `console.log(...)` | `qDebug()` | 普通日志 |
| `console.info(...)` | `qInfo()` | 信息日志 |
| `console.warn(...)` | `qWarning()` | 警告日志 |
| `console.error(...)` | `qCritical()` | 错误日志 |

**实现要点**：
- 支持多参数拼接：`console.log("a=", 1, "b=", {x:1})` → `"a= 1 b= {"x":1}"`
- 对象/数组参数使用 `JSON.stringify` 格式化输出
- 遵循 stdiolink 输出规范：日志输出到 stderr 或 `--log=<file>`，不污染 stdout

### 3.4 Driver 类绑定 (`bindings/js_driver`)

将 C++ `stdiolink::Driver` 类暴露为 JS 的 `Driver` 构造函数。

**JS API**：

```js
class Driver {
    constructor()

    // 启动 Driver 进程
    // @param program - 可执行文件路径
    // @param args - 可选启动参数数组
    // @returns boolean
    start(program, args = [])

    // 发送命令请求，返回 Task 对象
    // @param cmd - 命令名称
    // @param data - 命令参数对象
    // @returns Task
    request(cmd, data = {})

    // 查询 Driver 元数据
    // @param timeoutMs - 超时毫秒数，默认 5000
    // @returns object | null
    queryMeta(timeoutMs = 5000)

    // 终止 Driver 进程
    terminate()

    // 只读属性
    get running()    // boolean - 进程是否运行中
    get hasMeta()    // boolean - 是否已缓存元数据
}
```

**C++ 绑定实现要点**：
- 使用 QuickJS 的 `JS_NewClassID` + `JS_NewClass` 注册自定义类
- C++ `Driver` 实例存储在 JS 对象的 opaque 指针中
- GC finalizer 中调用 `terminate()` 并释放 C++ 对象
- `queryMeta()` 返回值：将 `DriverMeta` 序列化为 `QJsonObject`，再转为 JS 对象
- `start()` 的 args 参数：JS Array → `QStringList` 转换

### 3.5 Task 类绑定 (`bindings/js_task`)

将 C++ `stdiolink::Task` 暴露为 JS 的 `Task` 对象。

**JS API**：

```js
class Task {
    // 非阻塞获取下一条消息
    // @returns { status, code, data } | null
    tryNext()

    // 阻塞等待下一条消息
    // @param timeoutMs - 超时毫秒数，默认无限等待
    // @returns { status, code, data } | null (超时返回 null)
    waitNext(timeoutMs = -1)

    // 只读属性
    get done()          // boolean - 是否已完成
    get exitCode()      // number
    get errorText()     // string
    get finalPayload()  // any - 最终响应数据
}
```

**消息对象格式**：

```js
{
    status: "event" | "done" | "error",
    code: 0,
    data: { /* payload */ }
}
```

**实现要点**：
- Task 由 `Driver.request()` 创建返回，不支持用户直接构造
- `waitNext()` 内部调用 C++ `Task::waitNext()`，会阻塞 JS 执行线程
- 消息的 `QJsonValue` payload 转为 JS 对象时，递归转换所有嵌套类型

### 3.6 进程调用绑定 (`bindings/js_process`)

提供在 JS 中执行外部进程的能力，基于 `QProcess` 实现。

**JS API**：

```js
import { exec } from "stdiolink";

// 同步执行，等待进程结束，返回结果对象
const result = exec("git", ["status"]);
// result = { exitCode: 0, stdout: "...", stderr: "..." }

// 带选项执行
const result2 = exec("myapp", ["--config", "a.json"], {
    cwd: "/path/to/dir",       // 工作目录
    env: { KEY: "value" },     // 额外环境变量
    timeout: 10000,            // 超时毫秒数
    input: "stdin data"        // 写入 stdin 的数据
});
```

**返回值**：

```js
{
    exitCode: number,    // 进程退出码
    stdout: string,      // 标准输出内容
    stderr: string       // 标准错误内容
}
```

**实现要点**：
- 基于 `QProcess::start()` + `QProcess::waitForFinished()`
- timeout 默认 30 秒，超时后 kill 进程并返回错误
- env 选项与当前进程环境合并，不替换
- stdout/stderr 以 UTF-8 解码

### 3.7 Proxy 代理调用 (`proxy/driver_proxy.js`)

核心亮点功能：利用 JS `Proxy` 机制，将 Driver 的命令映射为对象方法，实现自然的函数调用风格。

**实现原理**：

1. `openDriver(program, args?)` 启动 Driver 进程
2. 调用 `queryMeta()` 获取元数据，得到所有命令列表
3. 返回一个 `Proxy` 对象，拦截属性访问：
   - 命令名 → 返回一个函数，调用时自动发送 `request` 并等待结果
   - `$driver` → 返回底层 Driver 实例
   - `$meta` → 返回元数据对象
   - `$rawRequest(cmd, data)` → 底层请求，返回 Task 对象（逃生口）
   - `$close()` → 终止 Driver 进程

**JS 层实现**（内嵌到内置模块中）：

```js
export async function openDriver(program, args = []) {
    const driver = new Driver();
    if (!driver.start(program, args)) {
        throw new Error(`Failed to start driver: ${program}`);
    }

    const meta = driver.queryMeta();
    if (!meta) {
        driver.terminate();
        throw new Error(`Failed to query metadata from: ${program}`);
    }

    const commands = new Set(meta.commands.map(c => c.name));
    let busy = false; // 单实例并发保护

    return new Proxy(driver, {
        get(target, prop) {
            // 保留字段
            if (prop === "$driver") return target;
            if (prop === "$meta") return meta;
            if (prop === "$rawRequest") return (cmd, data) => target.request(cmd, data);
            if (prop === "$close") return () => target.terminate();

            // 命令代理（异步，返回 Promise）
            if (typeof prop === "string" && commands.has(prop)) {
                return (params = {}) => {
                    if (busy) throw new Error("DriverBusyError: request already in flight");
                    busy = true;
                    let task;
                    try {
                        task = target.request(prop, params);
                    } catch (e) { busy = false; throw e; }
                    return globalThis.__scheduleTask(task).then(
                        msg => {
                            busy = false;
                            if (!msg) throw new Error(`No response for command: ${prop}`);
                            if (msg.status === "error") {
                                const err = new Error(msg.data?.message || `Command failed: ${prop}`);
                                err.code = msg.code;
                                err.data = msg.data;
                                throw err;
                            }
                            return msg.data;
                        },
                        err => { busy = false; throw err; }
                    );
                };
            }

            return undefined;
        }
    });
}
```

**使用效果**：

```js
import { openDriver } from "stdiolink";

const calc = await openDriver("./calculator_driver.exe");

// 直接以函数调用方式使用 Driver 命令
const sum = await calc.add({ a: 1, b: 2 });         // → { result: 3 }
const diff = await calc.subtract({ a: 10, b: 3 });  // → { result: 7 }

// 访问元数据
console.log(calc.$meta.info.name);  // → "Calculator"

// 关闭
calc.$close();
```

### 3.8 stdiolink 内置模块 (`bindings/js_stdiolink_module`)

将所有绑定统一注册为 `"stdiolink"` 内置模块，供 `import` 使用。

**模块导出清单**：

```js
// import { Driver, Task, openDriver, exec } from "stdiolink";
export {
    Driver,        // Driver 构造函数
    openDriver,    // Proxy 代理工厂函数
    exec,          // 同步进程执行
}
// Task 不直接导出，由 Driver.request() 返回
```

**C++ 注册流程**：

```cpp
static JSModuleDef* jsInitStdiolinkModule(JSContext* ctx, const char* name)
{
    JSModuleDef* m = JS_NewCModule(ctx, name, jsModuleInit);

    JS_AddModuleExport(ctx, m, "Driver");
    JS_AddModuleExport(ctx, m, "openDriver");
    JS_AddModuleExport(ctx, m, "exec");

    return m;
}

// 在引擎初始化时注册
engine.registerModule("stdiolink", jsInitStdiolinkModule);
```

### 3.9 并发调度器 (`bindings/js_task_scheduler`)

基于现有 `waitAnyNext` 实现单线程多任务调度，支持不同 Driver 实例的并行调用。

**约束**：

- 单个 `Driver` 实例是单请求在途模型，不能在同一实例上并发发送多个请求
- 不同 `Driver` 实例之间可以并行调用

**方案：多实例 + 单线程调度**：

1. 每次 `openDriver()` 生成独立 `Driver` 实例
2. 每个实例提交请求后得到 `Task`
3. `JsTaskScheduler` 维护活动 `Task` 列表
4. 调度循环调用 `waitAnyNext(tasks, out, timeout)`，将完成消息分发给对应 Promise
5. 不设置固定实例上限，按需创建与释放

**并发语义**：

| 场景 | 行为 |
|------|------|
| 不同实例并行调用 | 正常，由调度器统一调度 |
| 同一实例并发调用 | 抛 `DriverBusyError`，Proxy 层拦截 |

**C++ 侧实现要点**：

```cpp
class JsTaskScheduler {
public:
    // 注册一个活动 Task，关联到 JS Promise resolve/reject
    void addTask(Task task, JSValue resolve, JSValue reject);

    // 驱动一轮调度：调用 waitAnyNext，完成的 Task 触发对应 Promise
    // 返回是否还有活动 Task
    bool poll(int timeoutMs = 50);

    // 注册 __scheduleTask 全局函数到 JS context（供 Proxy 层调用）
    static void installGlobal(JSContext* ctx, JsTaskScheduler* scheduler);

private:
    struct PendingTask {
        Task task;
        JSValue resolve;
        JSValue reject;
    };
    QVector<PendingTask> m_pending;
};
```

`installGlobal` 向 JS 全局注册 `__scheduleTask(task)` 函数。该函数创建 Promise 并将 C++ Task 与 resolve/reject 注册到调度器，返回 Promise 给 Proxy 层。

**与 Promise job pump 的协作**：

```
evalFile() 执行脚本
  → 脚本中 Promise.all([drvA.scan(), drvB.scan()])
  → 两个请求注册到 JsTaskScheduler
  → 主循环：
      while (scheduler.hasPending() || engine.hasPendingJobs()) {
          scheduler.poll();           // 驱动 I/O，完成的 Task resolve Promise
          engine.executePendingJobs(); // 驱动 Promise 链
      }
```

---

## 4. TypeScript 声明文件生成

### 4.1 设计目标

集成到现有 `--export-doc` 体系，新增 `ts` 格式。Driver 可通过以下命令导出 `.d.ts` 类型声明：

```bash
calculator_driver.exe --export-doc=ts
calculator_driver.exe --export-doc=ts=calculator.d.ts
```

### 4.2 类型映射规则

stdiolink `FieldType` 到 TypeScript 类型的映射：

| FieldType | TypeScript 类型 | 说明 |
|-----------|----------------|------|
| `String` | `string` | |
| `Int` | `number` | |
| `Int64` | `number` | JS 无 int64，用 number |
| `Double` | `number` | |
| `Bool` | `boolean` | |
| `Object` | `{ [field]: type }` | 递归生成嵌套接口 |
| `Array` | `type[]` | 元素类型由 items 决定 |
| `Enum` | `"val1" \| "val2"` | 字符串字面量联合类型 |
| `Any` | `any` | |

### 4.3 生成输出示例

以 calculator_driver 为例，生成的 `.d.ts` 文件：

```typescript
/**
 * Calculator - 计算器 Driver
 * @version 1.0.0
 * @vendor demo
 */

/** add 命令参数 */
export interface AddParams {
    /** 第一个操作数 */
    a: number;
    /** 第二个操作数 */
    b: number;
}

/** add 命令返回值 */
export interface AddResult {
    result: number;
}

/** batch 命令参数 */
export interface BatchParams {
    /** 运算列表 */
    operations: Array<{
        op: "add" | "subtract" | "multiply" | "divide";
        a: number;
        b: number;
    }>;
}

/** Driver 代理接口 */
export interface CalculatorDriver {
    /** 加法运算 */
    add(params: AddParams): AddResult;
    /** 减法运算 */
    subtract(params: SubtractParams): SubtractResult;
    /** 乘法运算 */
    multiply(params: MultiplyParams): MultiplyResult;
    /** 除法运算 */
    divide(params: DivideParams): DivideResult;
    /** 批量运算 */
    batch(params: BatchParams): BatchResult;

    /** 底层 Driver 实例 */
    readonly $driver: Driver;
    /** Driver 元数据 */
    readonly $meta: object;
    /** 关闭 Driver */
    $close(): void;
}
```

### 4.4 实现方案

在现有 `DocGenerator` 类中新增 `toTypeScript()` 静态方法：

```cpp
// src/stdiolink/doc/doc_generator.h
class DocGenerator {
public:
    static QString toMarkdown(const meta::DriverMeta& meta);
    static QJsonObject toOpenAPI(const meta::DriverMeta& meta);
    static QString toHtml(const meta::DriverMeta& meta);
    static QString toTypeScript(const meta::DriverMeta& meta);  // 新增
};
```

**生成逻辑**：

1. 遍历 `DriverMeta.commands`，为每个命令生成：
   - `XxxParams` 接口（从 `CommandMeta.params` 递归生成）
   - `XxxResult` 接口（从 `CommandMeta.returns` 递归生成）
2. 生成 `DriverProxy` 接口，包含所有命令方法签名
3. 处理嵌套 Object 字段：生成独立接口或内联类型
4. 处理 Enum 字段：生成字符串字面量联合类型
5. 处理 Array 字段：根据 items schema 生成 `Type[]`
6. 添加 JSDoc 注释（从 `description` 字段提取）

**在 ConsoleArgs 中注册**：

```cpp
// --export-doc=ts[=path]
// 在现有 format 判断中新增 "ts" 分支
if (format == "ts") {
    output = DocGenerator::toTypeScript(meta);
}
```

---

## 5. 主程序入口 (`stdiolink_service`)

### 5.1 命令行接口

```bash
# 执行 JS 脚本
stdiolink_service <script.js>

# 显示帮助
stdiolink_service --help

# 显示版本
stdiolink_service --version
```

### 5.2 执行流程

```
main(argc, argv)
  │
  ├─ 解析命令行参数
  │   ├─ --help    → 输出帮助到 stderr，退出
  │   ├─ --version → 输出版本到 stderr，退出
  │   └─ <file.js> → 继续执行
  │
  ├─ 初始化 QCoreApplication（用于 QProcess 事件循环）
  │
  ├─ 创建 JsEngine
  │   ├─ 初始化 QuickJS Runtime/Context
  │   ├─ 安装 ModuleLoader
  │   ├─ 注册 console 桥接
  │   └─ 注册 "stdiolink" 内置模块
  │
  ├─ engine.evalFile(scriptPath)
  │   ├─ 读取 JS 文件
  │   ├─ 以 ES Module 模式编译执行
  │   ├─ 循环调用 executePendingJobs() 驱动 Promise 队列
  │   └─ 所有 Promise 完成或异常退出
  │
  └─ 返回退出码
      ├─ 0: 正常完成
      ├─ 1: JS 运行时错误
      └─ 2: 参数错误 / 文件不存在
```

### 5.3 main.cpp 伪代码

```cpp
#include "engine/js_engine.h"
#include "engine/module_loader.h"
#include "engine/console_bridge.h"
#include "bindings/js_stdiolink_module.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    // 解析参数
    if (argc < 2) {
        qCritical() << "Usage: stdiolink_service <script.js>";
        return 2;
    }
    QString scriptPath = QString::fromLocal8Bit(argv[1]);

    if (scriptPath == "--help") {
        printHelp();
        return 0;
    }

    if (!QFile::exists(scriptPath)) {
        qCritical() << "File not found:" << scriptPath;
        return 2;
    }

    // 初始化引擎
    JsEngine engine;
    ConsoleBridge::install(engine.context());
    ModuleLoader::install(engine.context());
    engine.registerModule("stdiolink", jsInitStdiolinkModule);

    // 执行脚本
    int ret = engine.evalFile(scriptPath);

    // 驱动 Promise microtask 队列直到全部完成
    while (engine.executePendingJobs()) {}

    return ret;
}
```

---

## 6. 数据类型转换

### 6.1 QJsonValue ↔ JSValue 转换

这是绑定层的核心基础设施，所有 C++ ↔ JS 数据交换都依赖此转换。

**QJsonValue → JSValue**：

| QJsonValue 类型 | JSValue 类型 |
|-----------------|-------------|
| `Null` | `JS_NULL` |
| `Bool` | `JS_NewBool` |
| `Double` | `JS_NewFloat64` |
| `String` | `JS_NewString` |
| `Array` | `JS_NewArray` + 递归填充 |
| `Object` | `JS_NewObject` + 递归填充 |

**JSValue → QJsonValue**：

| JSValue 类型 | QJsonValue 类型 |
|-------------|-----------------|
| `undefined` / `null` | `QJsonValue()` |
| `bool` | `QJsonValue(bool)` |
| `number` (整数) | `QJsonValue(int)` |
| `number` (浮点) | `QJsonValue(double)` |
| `string` | `QJsonValue(QString)` |
| `Array` | `QJsonArray` + 递归 |
| `Object` | `QJsonObject` + 递归 |

**工具函数**：

```cpp
// 放在独立的 utils/js_convert.h 中，供所有绑定复用
JSValue qjsonToJsValue(JSContext* ctx, const QJsonValue& val);
QJsonValue jsValueToQJson(JSContext* ctx, JSValue val);
JSValue qjsonObjectToJsValue(JSContext* ctx, const QJsonObject& obj);
QJsonObject jsValueToQJsonObject(JSContext* ctx, JSValue val);
```

---

## 7. 错误处理策略

### 7.1 JS 异常传播

所有 C++ 绑定函数在遇到错误时，通过 `JS_ThrowTypeError` / `JS_ThrowRangeError` 抛出 JS 异常，而非返回错误码。

**错误场景与处理**：

| 场景 | 处理方式 |
|------|---------|
| `Driver.start()` 失败 | 返回 `false`，不抛异常（允许用户判断） |
| `Driver.request()` 参数类型错误 | `JS_ThrowTypeError` |
| `Task.waitNext()` 超时 | 返回 `null`，不抛异常 |
| Driver 返回 `error` 状态 | Proxy 模式下抛 `Error`，底层 API 返回消息对象 |
| 模块文件不存在 | `JS_ThrowReferenceError` |
| `exec()` 进程超时 | 抛 `Error("Process timed out")` |

### 7.2 脚本级错误处理

引擎顶层捕获未处理异常，输出错误信息到 stderr 并返回退出码 1：

```
Error: Command failed: add
    code: 400
    at calc.add (proxy)
    at main.js:5
```

---

## 8. 开发阶段规划

### 阶段一：基础引擎搭建

**目标**：可执行文件能加载并运行简单 JS 脚本。

**任务清单**：

1. CMake 项目搭建
   - 创建 `src/stdiolink_service/CMakeLists.txt`
   - vcpkg 引入 quickjs-ng（`find_package(qjs CONFIG REQUIRED)`）
   - 链接 stdiolink 核心库和 Qt::Core
2. JsEngine 封装
   - QuickJS Runtime/Context 的 RAII 管理
   - `evalFile()` 以 ES Module 模式执行脚本
   - 顶层异常捕获与 stderr 输出
3. console 桥接
   - `console.log/info/warn/error` → `qDebug/qInfo/qWarning/qCritical`
   - 多参数拼接、对象 JSON 序列化
4. main.cpp 入口
   - 命令行参数解析（`--help`、`--version`、`<script.js>`）
   - QCoreApplication 初始化

**验收标准**：

```js
// test_basic.js
console.log("Hello from stdiolink_service!");
console.warn("This is a warning");
```

```bash
stdiolink_service test_basic.js
# stderr 输出: Hello from stdiolink_service!
# stderr 输出: This is a warning
```

### 阶段二：ES Module 加载器

**目标**：支持 `import`/`export` 模块系统，可加载本地 `.js` 文件模块。

**任务清单**：

1. ModuleLoader 实现
   - 注册 QuickJS 的 `module_normalize` 和 `module_loader` 回调
   - 相对路径解析（基于当前文件目录）
   - 绝对路径直接加载
2. 内置模块注册机制
   - `addBuiltin(name, initFunc)` 注册表
   - 内置模块名称匹配拦截（不走文件系统）
3. 文件读取
   - 使用 `QFile` 读取 `.js` 文件内容（遵循项目规范）
   - UTF-8 编码处理

**验收标准**：

```js
// lib/math.js
export function square(x) { return x * x; }

// main.js
import { square } from "./lib/math.js";
console.log("4^2 =", square(4));  // → 16
```

### 阶段三：Driver/Task 绑定与数据转换

**目标**：在 JS 中可以通过底层 API 启动 Driver、发送请求、接收响应。

**任务清单**：

1. QJsonValue ↔ JSValue 转换工具
   - `qjsonToJsValue()` / `jsValueToQJson()` 递归转换
   - 处理所有 JSON 类型（null、bool、number、string、array、object）
2. Driver 类绑定
   - `JS_NewClassID` + `JS_NewClass` 注册
   - 构造函数、`start()`、`request()`、`queryMeta()`、`terminate()`
   - `running`、`hasMeta` 只读属性
   - GC finalizer 释放 C++ 对象
3. Task 类绑定
   - `tryNext()`、`waitNext()`
   - `done`、`exitCode`、`errorText`、`finalPayload` 只读属性
   - 消息对象 `{ status, code, data }` 构造
4. 注册到 `"stdiolink"` 内置模块
   - 导出 `Driver` 构造函数

**验收标准**：

```js
import { Driver } from "stdiolink";

const d = new Driver();
d.start("./calculator_driver.exe");
const task = d.request("add", { a: 10, b: 20 });
const msg = task.waitNext();
console.log("status:", msg.status);  // → "done"
console.log("result:", msg.data);    // → { result: 30 }
d.terminate();
```

### 阶段四：进程调用绑定

**目标**：在 JS 中可以执行外部进程并获取输出。

**任务清单**：

1. `exec()` 函数绑定
   - 参数解析：`program`、`args`、`options`
   - 基于 `QProcess` 同步执行
   - 返回 `{ exitCode, stdout, stderr }` 对象
2. options 支持
   - `cwd`：设置工作目录
   - `env`：额外环境变量（与当前环境合并）
   - `timeout`：超时控制（默认 30s）
   - `input`：写入 stdin 的数据
3. 注册到 `"stdiolink"` 模块导出

**验收标准**：

```js
import { exec } from "stdiolink";

const r = exec("echo", ["hello"]);
console.log(r.exitCode);  // → 0
console.log(r.stdout);    // → "hello\n"
```

### 阶段五：Proxy 代理调用

**目标**：实现 `openDriver()` 工厂函数，通过 JS Proxy 将 Driver 命令映射为对象方法。

**任务清单**：

1. `openDriver()` C++ 辅助
   - 启动 Driver 进程
   - 调用 `queryMeta()` 获取元数据
   - 将元数据转为 JS 对象传递给 JS 层
2. driver_proxy.js 实现
   - Proxy handler 的 `get` 拦截器
   - 命令名匹配 → 返回调用函数
   - 内置属性（`$driver`、`$meta`、`$close`）
   - 错误状态自动抛异常
3. 内嵌 JS 代码到 C++ 二进制
   - 将 `driver_proxy.js` 通过 `QResource` 或编译期字符串嵌入
   - 引擎初始化时预加载
4. 注册到 `"stdiolink"` 模块导出

**验收标准**：

```js
import { openDriver } from "stdiolink";

const calc = await openDriver("./calculator_driver.exe");
const r = await calc.add({ a: 5, b: 3 });
console.log(r);  // → { result: 8 }
calc.$close();
```

### 阶段六：TypeScript 声明文件生成

**目标**：Driver 可通过 `--export-doc=ts` 导出 `.d.ts` 类型声明文件。

**任务清单**：

1. `DocGenerator::toTypeScript()` 实现
   - FieldType → TS 类型映射
   - 递归生成嵌套 Object 的接口定义
   - Enum 字段生成字符串字面量联合类型
   - Array 字段根据 items 生成 `Type[]`
   - JSDoc 注释生成（description、constraints）
2. 命令参数/返回值接口生成
   - 每个命令生成 `XxxParams` 和 `XxxResult` 接口
   - 可选字段标记 `?`，必填字段无 `?`
   - 默认值写入 JSDoc `@default`
3. Driver 代理接口生成
   - 汇总所有命令方法签名
   - 包含 `$driver`、`$meta`、`$close()` 内置成员
4. 集成到 ConsoleArgs
   - `--export-doc=ts[=path]` 参数支持
   - 无 path 时输出到 stdout

**验收标准**：

```bash
calculator_driver.exe --export-doc=ts
# 输出完整的 .d.ts 内容到 stdout

calculator_driver.exe --export-doc=ts=calculator.d.ts
# 写入文件 calculator.d.ts
```

### 阶段七：集成测试与完善

**目标**：端到端验证所有功能，编写示例脚本。

**任务清单**：

1. 端到端测试脚本
   - 使用 calculator_driver 验证完整流程
   - 使用 device_simulator_driver 验证复杂参数类型
   - 多 Driver 协同调用测试
2. 错误场景测试
   - Driver 启动失败
   - 命令执行超时
   - 无效参数传递
   - 模块加载失败
3. 示例脚本编写
   - `examples/basic_usage.js` — 底层 API 使用
   - `examples/proxy_usage.js` — Proxy 代理调用
   - `examples/multi_driver.js` — 多 Driver 协同
   - `examples/process_exec.js` — 进程调用

**验收标准**：

```bash
# 完整端到端流程可运行
stdiolink_service examples/proxy_usage.js
# 正常输出计算结果，无异常退出
```

---

## 9. 关键技术决策

### 9.1 执行模型：单线程同步 + waitAnyNext 并发调度

**决策：底层 API 同步阻塞，Proxy 层支持多实例并发**

两层执行模型并存：

- **底层 API**（`Driver` / `Task`）：同步阻塞，`Task.waitNext()` 直接等待响应，与 `demo_host` 使用模式一致
- **Proxy 层**（`openDriver`）：命令调用返回 Promise，支持 `Promise.all` 多实例并发，由 `JsTaskScheduler` 基于 `waitAnyNext` 在单线程内调度多个 Task 的 I/O

并发单位是"不同 Driver 实例"，同一实例仍为单请求在途。这与 stdiolink 协议的设计约束一致——单个 Driver 进程的 stdin/stdout 是串行管道。

### 9.2 Proxy 实现层级

**决策：JS 层实现 Proxy，C++ 层提供基础绑定**

Proxy 逻辑用 JS 编写（`driver_proxy.js`），而非在 C++ 中实现 QuickJS 的 exotic object handler。原因：

- JS Proxy API 天然适合此场景，代码简洁易维护
- C++ 层只需暴露 `Driver`、`Task` 基础类
- JS 层可灵活扩展（如添加事件流迭代器、批量调用等）
- 调试和修改更方便

### 9.3 内嵌 JS 代码策略

**决策：编译期嵌入为 C 字符串常量**

`driver_proxy.js` 等内部 JS 代码在构建时转为 C 字符串常量，编译进二进制：

```cpp
// 由构建脚本自动生成
static const char s_driverProxyJs[] = R"JS(
    // driver_proxy.js 内容
    export function openDriver(program, args) { ... }
)JS";
```

优点：单文件部署，无需附带 JS 运行时文件。

### 9.4 QEventLoop 与 QuickJS 协作

**决策：在阻塞调用中驱动 Qt 事件循环**

`Task::waitNext()` 内部使用 `QEventLoop` 等待 QProcess 的 `readyReadStandardOutput` 信号。由于 QuickJS 是单线程的，阻塞调用期间 JS 执行暂停，Qt 事件循环正常运转，不存在冲突：

```
JS 调用 task.waitNext()
  → C++ Task::waitNext()
    → QEventLoop::exec()  // 处理 QProcess I/O 事件
    → 收到响应，退出事件循环
  → 返回消息给 JS
```

---

## 10. 完整 API 参考

### 10.1 `"stdiolink"` 模块导出

```typescript
// 类
export class Driver {
    constructor();
    start(program: string, args?: string[]): boolean;
    request(cmd: string, data?: object): Task;
    queryMeta(timeoutMs?: number): DriverMeta | null;
    terminate(): void;
    readonly running: boolean;
    readonly hasMeta: boolean;
}

// Task 由 Driver.request() 返回，不可直接构造
interface Task {
    tryNext(): Message | null;
    waitNext(timeoutMs?: number): Message | null;
    readonly done: boolean;
    readonly exitCode: number;
    readonly errorText: string;
    readonly finalPayload: any;
}

interface Message {
    status: "event" | "done" | "error";
    code: number;
    data: any;
}

// 函数
export function openDriver(program: string, args?: string[]): DriverProxy;
export function exec(program: string, args?: string[], options?: ExecOptions): ExecResult;

interface ExecOptions {
    cwd?: string;
    env?: Record<string, string>;
    timeout?: number;
    input?: string;
}

interface ExecResult {
    exitCode: number;
    stdout: string;
    stderr: string;
}
```

---

## 11. 构建与依赖

### 11.1 新增依赖

| 依赖 | 版本 | 引入方式 | 说明 |
|------|------|---------|------|
| quickjs-ng | latest stable | vcpkg (`quickjs-ng`) | JS 引擎，静态链接 |
| Qt::Core | 现有 | 已有依赖 | QProcess、QFile、QJsonDocument 等 |
| stdiolink | 现有 | 已有依赖 | 核心库（host/Driver、host/Task、doc/DocGenerator） |

### 11.2 CMakeLists.txt 结构

```cmake
# src/stdiolink_service/CMakeLists.txt

add_executable(stdiolink_service
    main.cpp
    engine/js_engine.cpp
    engine/module_loader.cpp
    engine/console_bridge.cpp
    bindings/js_driver.cpp
    bindings/js_task.cpp
    bindings/js_process.cpp
    bindings/js_stdiolink_module.cpp
    proxy/driver_proxy_gen.cpp
)

target_link_libraries(stdiolink_service PRIVATE
    stdiolink          # 核心库
    Qt::Core           # Qt 基础
    qjs                # QuickJS-NG 引擎（vcpkg: quickjs-ng）
)
```

### 11.3 构建命令

```bash
# 增量构建（在现有构建体系中自动包含）
cmake --build build_ninja --parallel 8

# 产出
./build_ninja/bin/stdiolink_service.exe
```
