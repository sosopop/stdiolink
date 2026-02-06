# stdiolink JS Runtime 设计与开发计划（Host-only, quickjs-ng）

## 1. 目标与范围

### 1.1 目标

基于 `quickjs-ng` 为现有 `stdiolink` 增加 JS Host 运行时能力，并提供可执行程序 `stdiolink_service`：

1. 现有 Driver 仍是独立 exe，可直接复用。
2. JS 侧通过 `connectDriver(...)` 自动拿到 meta 并生成代理，支持 `await drv.scan(...)`。
3. `autoProxy` 对 `oneshot/keepalive` 透明，上层无需关心底层运行模式。
4. 支持并发调用，但并发单位是“不同 Driver 实例”。
5. 并发调度采用单线程 `waitAnyNext` 事件循环，不引入固定大小连接池上限。
6. 支持 ES Module `import`、`console.log` 到 Qt 日志、进程调用绑定。
7. 支持导出 TS 声明（`.d.ts`），用于 JS/TS 开发提示。

### 1.2 范围边界

本期优先覆盖可用 MVP，不包含：

1. 用 JS 实现 Driver（不提供 `defineDriver`）。
2. 单个 Driver 实例内的多请求并发复用。
3. npm 生态兼容（`node_modules`、`package.json`）。
4. 浏览器 API polyfill（`fetch`、DOM）。
5. 多线程 JS runtime。

## 2. 现状基线与复用点

当前仓库可直接复用：

1. Host 侧进程与请求模型：
`src/stdiolink/host/driver.h`、`src/stdiolink/host/task.h`
2. 多任务等待能力：
`src/stdiolink/host/wait_any.h`、`src/stdiolink/host/wait_any.cpp`
3. Driver 元数据结构与解析：
`src/stdiolink/protocol/meta_types.h`
4. 协议序列化与解析：
`src/stdiolink/protocol/jsonl_serializer.h`
5. Driver 文档导出链路（可扩展 ts）：
`src/stdiolink/driver/driver_core.cpp`、`src/stdiolink/doc/doc_generator.h`

结论：JS runtime 只做 Host 桥接；并发调度复用 `waitAnyNext`；TS 导出能力在 Driver 侧 `--export-doc` 扩展。

## 3. 总体架构

### 3.1 模块结构

建议新增：

1. `src/stdiolink_js/`：JS runtime 核心。
2. `src/stdiolink_js_service/`：`stdiolink_service` 可执行程序。
3. `src/stdiolink/doc/ts_decl_generator.*`：TS 声明生成器（供 Driver 导出调用）。

### 3.2 运行形态

`stdiolink_service` 启动流程：

1. 初始化 `QCoreApplication`。
2. 初始化 `quickjs-ng` runtime/context。
3. 注册内建模块 `stdiolink` 与 `process`。
4. 执行入口 JS 模块。
5. 脚本完成后退出。

### 3.3 核心桥接组件

1. `JsRuntime`：quickjs 生命周期、Promise job pump、异常格式化。
2. `JsModuleLoader`：`import` 解析（相对路径、绝对路径、内建模块）。
3. `JsConsoleBridge`：`console.* -> qDebug/qInfo/qWarning/qCritical`。
4. `JsProcessBridge`：`QProcess` 到 JS API。
5. `JsDriverHostBridge`：`stdiolink::Driver` 到 JS API。
6. `JsTaskScheduler`：基于 `waitAnyNext` 的单线程多任务调度器。

## 4. JS API 设计（草案）

### 4.1 内建模块 `stdiolink`

```js
import { connectDriver } from "stdiolink";
```

单实例调用：

```js
const drv = await connectDriver("demo_driver.exe", {
  args: ["--profile=keepalive"],
  timeoutMs: 5000,
  autoProxy: true
});

const result = await drv.scan({ fps: 30 });
await drv.$close();
```

多实例并发调用：

```js
const drvA = await connectDriver("demo_driver.exe", { autoProxy: true });
const drvB = await connectDriver("demo_driver.exe", { autoProxy: true });

const [a, b] = await Promise.all([
  drvA.scan({ fps: 30 }),
  drvB.scan({ fps: 24 })
]);

await Promise.all([drvA.$close(), drvB.$close()]);
```

保留字段：

1. `drv.$meta`：原始 `DriverMeta`。
2. `drv.$rawRequest(cmd, data)`：底层请求。
3. `drv.$close()`：关闭进程。

### 4.2 `autoProxy` 模式透明性

1. `connectDriver` 先进行一次 meta 探测。
2. 同一调用形态适配 `oneshot/keepalive`。
3. 上层统一使用 `await drv.<cmd>(params)`。

### 4.3 并发语义

1. 同一 `drv` 实例只允许单请求在途。
2. 对同一实例并发调用时返回明确错误（如 `DriverBusyError`）。
3. 不同 `drv` 实例可并行调用，由 `JsTaskScheduler` 统一调度。

### 4.4 import / console / process

MVP：

1. `import`：ESM、相对路径、绝对路径、`.js/.mjs` 补全。
2. `console`：`log/info/warn/error` 分别映射 `qDebug/qInfo/qWarning/qCritical`。
3. `process`：`exec`、`spawn`、`wait`、`kill`。

## 5. 单线程并发调度设计（waitAnyNext）

### 5.1 约束

当前单个 `stdiolink::Driver` 实例是单请求在途模型，不能直接并行复用一个实例。

### 5.2 方案

采用“多实例 + 单线程调度”模式：

1. 每次 `connectDriver` 生成独立 `Driver` 实例。
2. 每个实例提交请求后得到 `Task`。
3. `JsTaskScheduler` 维护活动 `Task` 列表。
4. 调度循环调用 `waitAnyNext(tasks, out, timeout)`，将完成消息分发给对应 Promise。
5. 不设置固定实例上限，按需创建与释放（上限由系统资源决定）。

### 5.3 能力边界

1. 支持不同 Driver 实例并行调用不同方法。
2. 不支持单实例内并行；如需支持，后续需协议增加 `requestId` 并重构 `Driver` 状态机。

## 6. TS 声明导出（Driver 侧扩展）

### 6.1 原则

TS 导出由 Driver 的 `--export-doc` 扩展完成，新增格式：`typescript|ts|dts`。

示例：

```bash
demo_driver.exe --export-doc=ts
demo_driver.exe --export-doc=typescript=demo_driver.d.ts
```

### 6.2 导出结构

1. `interface <DriverName>Commands`
2. `<Cmd>Params` / `<Cmd>Result`
3. `type <DriverName>Client = { ... }`

### 6.3 类型与命名规则

1. 基础映射：
`String->string`，`Int/Int64/Double->number`，`Bool->boolean`，`Any->unknown`。
2. 枚举：
`Enum` 优先导出字面量联合类型。
3. 对象：
优先引用命名 interface，匿名对象按稳定命名规则提升为独立类型。
4. 类型命名冲突：
同名冲突自动追加后缀（`_2`、`_3`）。
5. `DriverMeta.types`：
优先作为共享类型池，命令参数/返回先引用再内联回退。

## 7. `stdiolink_service` 命令形态

仅保留脚本模式：

```bash
stdiolink_service <entry.js> [script_args...]
```

退出码：

1. `0`：成功。
2. `1`：参数错误、文件错误、JS 异常、Driver 启动或请求失败。

## 8. 分阶段开发计划

### Phase 0：脚手架与依赖接入

1. 接入 `quickjs-ng`（固定版本）。
2. 新增 `stdiolink_service` target。
3. 跑通最小脚本执行与退出。

验收：

1. `stdiolink_service hello.js` 返回 0。

### Phase 1：运行时基础能力

1. `import` 支持。
2. `console` 到 Qt 日志映射。
3. Promise job pump 与异常栈输出。

验收：

1. 多模块脚本可运行，日志可见。

### Phase 2：Driver Host 绑定与自动代理

1. `connectDriver` 对接 `stdiolink::Driver`。
2. 自动 `meta.describe`。
3. `Proxy` 直接方法调用：`drv.scan(...)`。
4. `autoProxy` 对 `oneshot/keepalive` 透明。

验收：

1. JS 可直接调用现有 Driver 命令。
2. 上层不需要处理底层 profile 差异。

### Phase 3：waitAnyNext 并发调度与 process 绑定

1. 实现 `JsTaskScheduler`。
2. 调度循环基于 `waitAnyNext`，支持多实例并发。
3. 明确同实例并发报错语义。
4. `process.exec/spawn/wait/kill`。

验收：

1. 不同 `drv` 实例可稳定并发调用。
2. 同实例并发返回明确错误。
3. JS 可稳定执行外部进程并读取输出。

### Phase 4：TS 声明导出

1. 实现 `TsDeclGenerator`。
2. 在 Driver 侧扩展 `--export-doc=typescript|ts|dts`。
3. 覆盖类型映射、命名规则、导出文本结构测试。

验收：

1. 生成的 `.d.ts` 可用于 TS 智能提示。

## 9. 测试计划

单元测试：

1. `src/tests/test_js_module_loader.cpp`
2. `src/tests/test_js_console_bridge.cpp`
3. `src/tests/test_js_driver_proxy.cpp`
4. `src/tests/test_js_task_scheduler.cpp`
5. `src/tests/test_ts_decl_generator.cpp`

集成测试：

1. `src/tests/test_js_host_call_cpp_driver.cpp`
2. `src/tests/test_js_multi_driver_parallel.cpp`
3. `src/tests/test_js_process_binding.cpp`
4. `src/tests/test_export_doc_typescript.cpp`

关键断言：

1. `connectDriver` 返回对象可直接 `drv.<cmd>()`。
2. `oneshot/keepalive` 下调用语义一致。
3. 不同实例并发正确、无串扰。
4. 同实例并发会返回确定性错误。
5. `.d.ts` 在 `tsc --noEmit` 下通过基础检查。

## 10. 风险与缓解

1. quickjs 与 Qt 事件循环整合复杂。
缓解：先单线程 Promise pump。
2. 活动 Task 数量增长带来调度压力。
缓解：调度器增加超时清理、取消机制和指标统计。
3. JS/C++ 类型转换边界较多。
缓解：集中 `ValueConverter` 层并做双向单测。

## 11. 里程碑交付物

1. `stdiolink_service` 可执行程序。
2. `stdiolink` JS 模块（Host-only：`connectDriver` + proxy + `waitAnyNext` 调度）。
3. `process` 与 `console` 绑定。
4. Driver 侧 `--export-doc=ts` 导出能力与测试文档。

## 12. 建议实施顺序

1. 先完成 `connectDriver` 自动代理（Phase 0~2）。
2. 再补 `waitAnyNext` 并发调度与 `process`（Phase 3）。
3. 最后完成 TS 导出（Phase 4）。
