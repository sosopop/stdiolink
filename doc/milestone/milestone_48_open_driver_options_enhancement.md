# 里程碑 48：openDriver 可配置策略增强

> **前置条件**: 里程碑 47 已完成
> **目标**: 扩展 `openDriver(program, args?, options?)`，在保持兼容前提下增强 profile 注入与 meta 查询超时控制

---

## 1. 目标

- 保持旧签名完全兼容：
  - `openDriver(program)`
  - `openDriver(program, args)`
- 新增第三参数 `options`：
  - `profilePolicy: "auto" | "force-keepalive" | "preserve"`
  - `metaTimeoutMs: number`
- 选项解析严格校验，错误尽早暴露
- 保留当前 Proxy 行为：`$driver/$meta/$rawRequest/$close` 与命令方法映射不变

---

## 2. 设计原则（强约束）

- **简约**: 仅扩展必要策略，不改 `Driver/Task/waitAny` 主模型
- **可靠**: 参数非法直接失败，禁止隐式容错导致行为漂移
- **稳定**: 默认策略继续对齐当前行为（`auto`）
- **避免过度设计**: 不引入自动重连、驱动健康探测、生命周期编排 DSL

---

## 3. 范围与非目标

### 3.1 范围（M48 内）

- `openDriver` options 解析与行为分支
- `profilePolicy` 三种策略
- `metaTimeoutMs` 可配置化
- 完整测试覆盖与 manual 文档更新

### 3.2 非目标（M48 外）

- 不改造 `host::Driver::start()` 的超时参数模型
- 不新增 `startTimeoutMs`（涉及 `host::Driver` 接口变更，延后里程碑）
- 不改造 Proxy 命令调度（busy 互斥逻辑保持原样）

---

## 4. 技术方案

### 4.1 新签名与类型约束

```js
await openDriver(program, args?, options?)
```

```ts
type OpenDriverOptions = {
  profilePolicy?: "auto" | "force-keepalive" | "preserve";
  metaTimeoutMs?: number;
};
```

约束：

- `program`: 必须为非空字符串
- `args`: 可省略；若提供必须是 `string[]`
- `options`: 可省略；若提供必须是对象
- `options` 只允许 `profilePolicy/metaTimeoutMs`，其余键一律 `TypeError`

### 4.2 `profilePolicy` 行为矩阵

#### 4.2.1 默认值

- `profilePolicy` 默认 `auto`
- `metaTimeoutMs` 默认 `5000`（与当前 `driver.queryMeta(5000)` 一致）

#### 4.2.2 规则定义

- `auto`：
  - 若 `args` 中已存在 `--profile=` 参数，则保持不变
  - 否则追加 `--profile=keepalive`
- `force-keepalive`：
  - 先移除 `args` 中所有 `--profile=` 参数
  - 再追加 `--profile=keepalive`
- `preserve`：
  - 完全不改动 `args`

#### 4.2.3 示例

```js
// auto（默认）
await openDriver(program);                       // 自动补 keepalive
await openDriver(program, ["--profile=oneshot"]); // 保持 oneshot

// force-keepalive
await openDriver(program, ["--profile=oneshot"], {
  profilePolicy: "force-keepalive"
}); // 最终为 keepalive

// preserve
await openDriver(program, [], {
  profilePolicy: "preserve"
}); // 不注入任何 profile
```

### 4.3 `metaTimeoutMs` 行为

- 作用点：`driver.queryMeta(metaTimeoutMs)`
- 合法值：正整数（`> 0`）
- 非法值：
  - 非数字：`TypeError`
  - 非有限数、小于等于 0、或非整数：`RangeError`
- 超时失败时：
  - 主动调用 `driver.terminate()`
  - 抛出含 program 与 timeout 信息的错误

### 4.4 修改 `proxy/driver_proxy.cpp`

`createOpenDriverFunction` 的工厂脚本扩展为三段：

1. 参数标准化与校验
2. `startArgs` 按策略生成
3. 启动 Driver + 查询 meta + 构建 Proxy

关键伪代码（保留当前架构：C++ 内嵌 JS factory）：

```cpp
static const char kFactorySource[] =
  "(function(DriverCtor){\n"
  "  function normalizeOptions(options) {\n"
  "    if (options == null) return { profilePolicy: 'auto', metaTimeoutMs: 5000 };\n"
  "    if (typeof options !== 'object' || Array.isArray(options)) {\n"
  "      throw new TypeError('openDriver: options must be an object');\n"
  "    }\n"
  "    const allowed = new Set(['profilePolicy', 'metaTimeoutMs']);\n"
  "    for (const k of Object.keys(options)) {\n"
  "      if (!allowed.has(k)) {\n"
  "        throw new TypeError('openDriver: unknown option: ' + k);\n"
  "      }\n"
  "    }\n"
  "    const profilePolicy = options.profilePolicy ?? 'auto';\n"
  "    if (!['auto', 'force-keepalive', 'preserve'].includes(profilePolicy)) {\n"
  "      throw new TypeError('openDriver: invalid profilePolicy');\n"
  "    }\n"
  "    let metaTimeoutMs = 5000;\n"
  "    if (options.metaTimeoutMs !== undefined) {\n"
  "      if (typeof options.metaTimeoutMs !== 'number') {\n"
  "        throw new TypeError('openDriver: metaTimeoutMs must be a number');\n"
  "      }\n"
  "      if (!Number.isFinite(options.metaTimeoutMs) || options.metaTimeoutMs <= 0 ||\n"
  "          !Number.isInteger(options.metaTimeoutMs)) {\n"
  "        throw new RangeError('openDriver: metaTimeoutMs must be a positive integer');\n"
  "      }\n"
  "      metaTimeoutMs = options.metaTimeoutMs;\n"
  "    }\n"
  "    return { profilePolicy, metaTimeoutMs };\n"
  "  }\n"
  "\n"
  "  function buildStartArgs(args, profilePolicy) {\n"
  "    if (args !== undefined && !Array.isArray(args)) {\n"
  "      throw new TypeError('openDriver: args must be an array');\n"
  "    }\n"
  "    const src = Array.isArray(args) ? args : [];\n"
  "    for (const a of src) {\n"
  "      if (typeof a !== 'string') throw new TypeError('openDriver: args item must be string');\n"
  "    }\n"
  "    const out = src.slice();\n"
  "    const hasProfile = out.some(a => a.startsWith('--profile='));\n"
  "\n"
  "    if (profilePolicy === 'preserve') return out;\n"
  "    if (profilePolicy === 'auto') {\n"
  "      if (!hasProfile) out.push('--profile=keepalive');\n"
  "      return out;\n"
  "    }\n"
  "\n"
  "    // force-keepalive\n"
  "    const filtered = out.filter(a => !a.startsWith('--profile='));\n"
  "    filtered.push('--profile=keepalive');\n"
  "    return filtered;\n"
  "  }\n"
  "\n"
  "  return async function openDriver(program, args, options) {\n"
  "    if (typeof program !== 'string' || program.length === 0) {\n"
  "      throw new TypeError('openDriver: program must be a non-empty string');\n"
  "    }\n"
  "\n"
  "    const opts = normalizeOptions(options);\n"
  "    const startArgs = buildStartArgs(args, opts.profilePolicy);\n"
  "\n"
  "    const driver = new DriverCtor();\n"
  "    if (!driver.start(program, startArgs)) {\n"
  "      throw new Error('Failed to start driver: ' + program);\n"
  "    }\n"
  "\n"
  "    const meta = driver.queryMeta(opts.metaTimeoutMs);\n"
  "    if (!meta) {\n"
  "      driver.terminate();\n"
  "      throw new Error('Failed to query metadata from: ' + program +\n"
  "        ' (timeoutMs=' + opts.metaTimeoutMs + ')');\n"
  "    }\n"
  "\n"
  "    // 后续 Proxy 逻辑维持现有实现\n"
  "  };\n"
  "})";
```

### 4.5 兼容性与保留行为

必须保持不变的语义：

- Proxy 保留字段：`$driver/$meta/$rawRequest/$close`
- 命令动态映射：`proxy.<commandName>(params)`
- 同一 Proxy 单实例 busy 互斥（并发命令抛 `DriverBusyError`）
- driver 返回 `status === 'error'` 时抛出 JS `Error`，并透传 `code/data`

### 4.6 JS 使用示例

```js
import { openDriver } from "stdiolink";

// 兼容旧用法
const a = await openDriver("calculator_driver");

// 新 options：保留传入 profile，不自动注入
const b = await openDriver("calculator_driver", ["--profile=oneshot"], {
  profilePolicy: "preserve",
  metaTimeoutMs: 3000
});

// 强制 keepalive
const c = await openDriver("calculator_driver", ["--profile=oneshot"], {
  profilePolicy: "force-keepalive"
});
```

---

## 5. 实现步骤

1. 在 `driver_proxy.cpp` 中重写 openDriver 工厂参数解析逻辑
2. 增加 `options` 严格键校验和类型校验
3. 接入 `profilePolicy` 三策略
4. 将 `queryMeta(5000)` 替换为可配置 `metaTimeoutMs`
5. 错误信息增强（包含 program / timeout）
6. 在 `test_proxy_and_scheduler.cpp` 增加行为矩阵测试
7. 新增慢元数据测试驱动，稳定覆盖 `metaTimeoutMs` 失败路径
8. 更新 manual 文档并完成回归

---

## 6. 文件改动清单

### 6.1 新增文件

- `src/tests/test_slow_meta_driver_main.cpp`

### 6.2 修改文件

- `src/stdiolink_service/proxy/driver_proxy.cpp`
- `src/tests/test_proxy_and_scheduler.cpp`
- `src/tests/CMakeLists.txt`（新增 `test_slow_meta_driver` 可执行）
- `doc/manual/10-js-service/proxy-and-scheduler.md`
- `doc/manual/10-js-service/getting-started.md`（若示例涉及）

---

## 7. 单元测试计划（全面覆盖）

以 `src/tests/test_proxy_and_scheduler.cpp` 为主，新增用例按功能分组。

### 7.1 测试 Fixture 与基础

- 复用现有 `JsProxyTest`（`JsEngine + JsTaskScheduler + WaitAnyScheduler`）
- 复用现有 `calculator_driver` 作为功能驱动
- `metaTimeoutMs` 失败路径优先通过 `test_slow_meta_driver` 稳定覆盖

### 7.2 兼容性回归

```cpp
TEST_F(JsProxyTest, OpenDriverLegacySignatureProgramOnlyWorks) {
    // openDriver(program)
}

TEST_F(JsProxyTest, OpenDriverLegacySignatureProgramAndArgsWorks) {
    // openDriver(program, args)
}
```

### 7.3 `profilePolicy` 行为矩阵

```cpp
TEST_F(JsProxyTest, ProfilePolicyAutoInjectsKeepaliveWhenMissing) {
    // 无 profile 参数，连续两次命令可成功（证明 keepalive 生效）
}

TEST_F(JsProxyTest, ProfilePolicyAutoPreservesExplicitProfile) {
    // args 指定 oneshot 时，不应被 auto 覆盖
}

TEST_F(JsProxyTest, ProfilePolicyForceKeepaliveOverridesExistingProfile) {
    // args 给 oneshot，也应被强制替换为 keepalive
}

TEST_F(JsProxyTest, ProfilePolicyPreserveDoesNotInjectProfile) {
    // preserve 下不注入 profile，行为与原始 args 保持一致
}
```

### 7.4 `metaTimeoutMs` 行为

```cpp
TEST_F(JsProxyTest, MetaTimeoutMsDefaultStillWorks) {
    // 不传 metaTimeoutMs，默认 5000
}

TEST_F(JsProxyTest, MetaTimeoutMsCustomValueWorks) {
    // 自定义超时成功路径
}

TEST_F(JsProxyTest, MetaTimeoutMsTooSmallCausesOpenDriverReject) {
    // 慢 meta 驱动 + 小超时，触发 reject
}
```

### 7.5 参数校验与错误路径

```cpp
TEST_F(JsProxyTest, OpenDriverArgsNotArrayThrowsTypeError) {
    // args 非数组
}

TEST_F(JsProxyTest, OpenDriverArgsItemNotStringThrowsTypeError) {
    // args 中出现非字符串
}

TEST_F(JsProxyTest, OpenDriverOptionsNotObjectThrowsTypeError) {
    // options 非对象
}

TEST_F(JsProxyTest, OpenDriverOptionsUnknownKeyThrowsTypeError) {
    // options 含未知键
}

TEST_F(JsProxyTest, OpenDriverInvalidProfilePolicyThrowsTypeError) {
    // profilePolicy 非法值
}

TEST_F(JsProxyTest, OpenDriverInvalidMetaTimeoutThrowsRangeError) {
    // metaTimeoutMs <= 0、NaN、或非整数
}
```

### 7.6 保留行为与回归

```cpp
TEST_F(JsProxyTest, ReservedFieldsStillAvailable) {
    // $driver/$meta/$rawRequest/$close
}

TEST_F(JsProxyTest, BusyGuardBehaviorUnchanged) {
    // 同实例并发请求仍抛 DriverBusyError
}

TEST_F(JsProxyTest, DriverErrorMappingUnchanged) {
    // status=error -> throw Error(code,data)
}
```

需要全量通过的回归测试：

- `src/tests/test_driver_task_binding.cpp`
- `src/tests/test_proxy_and_scheduler.cpp`
- `src/tests/test_js_integration.cpp`
- `src/tests/test_js_stress.cpp`

---

## 8. 验收标准（DoD）

- `openDriver` 新旧签名均可用
- `profilePolicy` 三策略行为与文档一致
- `metaTimeoutMs` 可配置且错误路径可测
- Proxy 保留字段、busy 互斥、错误映射无回归
- 新增与回归测试全部通过

---

## 9. 风险与控制

- **风险 1**：`profilePolicy` 分支增多引入隐性回归
  - 控制：行为矩阵逐项单测，默认分支保持现有 `auto`
- **风险 2**：`metaTimeoutMs` 失败路径测试不稳定
  - 控制：引入慢 meta 测试驱动，避免依赖系统负载与时序偶发
- **风险 3**：内嵌 JS 工厂脚本增长导致可维护性下降
  - 控制：拆分 `normalizeOptions/buildStartArgs` 纯函数，减少主流程复杂度
