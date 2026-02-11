# stdiolink_service JS 绑定扩展（P0-P2）技术路线与技术栈方案

## 1. 背景与目标

当前 `stdiolink_service` 已提供核心绑定能力：

- `Driver` / `openDriver` / `waitAny`
- `exec`
- `getConfig`

但在服务脚本工程化场景下，基础能力仍偏薄，常见问题包括：

- 路径、文件、时间相关能力需要脚本手写或依赖外部命令，跨平台成本高
- 仅有同步 `exec`，长任务会阻塞脚本线程
- `getConfig()` 为浅冻结，嵌套对象仍可被误改写
- 缺少结构化日志与 HTTP 调用基础设施，影响服务编排扩展

本方案在“尽量不破坏现有行为、尽量复用现有运行时架构”前提下，规划 P0-P2 分阶段落地。

---

## 2. 现状基线（与代码对齐）

### 2.1 现有导出能力

`stdiolink` 内置模块导出：

- `Driver`
- `exec`
- `openDriver`
- `waitAny`
- `getConfig`

见 `src/stdiolink_service/bindings/js_stdiolink_module.cpp`。

### 2.2 关键运行时架构

- 内置模块注册：`JsEngine::registerModule(...)` → `ModuleLoader::addBuiltin(...)`
- 模块加载：`ModuleLoader` 统一处理内置模块和文件模块
- 主事件循环：`JsTaskScheduler` + `WaitAnyScheduler` + QuickJS job queue
- JS `console.*` 已桥接到 Qt 日志系统（`ConsoleBridge`）

相关文件：

- `src/stdiolink_service/engine/js_engine.cpp`
- `src/stdiolink_service/engine/module_loader.cpp`
- `src/stdiolink_service/main.cpp`
- `src/stdiolink_service/engine/console_bridge.cpp`

---

## 3. 设计原则

1. **增量扩展优先**：保持现有 `stdiolink` 兼容；新增能力优先独立子模块。
2. **跨平台一致性**：Windows/macOS/Linux 行为一致，路径与编码统一 UTF-8。
3. **同步+异步并行演进**：P0 先补同步最小集，P1/P2 再补异步与高级能力。
4. **可测试可回归**：每个模块有独立单测与集成用例，不依赖人工验证。
5. **低侵入接入**：复用 QuickJS + Qt 现有基础，不引入重量级第三方运行时。

---

## 4. 总体技术栈与模块分层

## 4.1 技术栈

- 语言：C++17 + JavaScript (ES Module)
- 运行时：QuickJS(-NG)（现有）
- 框架：Qt6 Core（`QFile/QDir/QProcess/QNetworkAccessManager/QElapsedTimer/QDateTime`）
- 测试：GoogleTest（现有 `src/tests`）
- 构建：CMake + Ninja（现有）

## 4.2 推荐模块拓扑

新增内置模块（独立命名，避免污染 `stdiolink` 主模块）：

- `stdiolink/constants`
- `stdiolink/path`
- `stdiolink/fs`
- `stdiolink/time`
- `stdiolink/http`
- `stdiolink/log`
- `stdiolink/process`（P2，异步进程）

`stdiolink` 主模块保留不变；可在文档中推荐：

```js
import { openDriver, waitAny, getConfig } from "stdiolink";
import * as path from "stdiolink/path";
import * as fs from "stdiolink/fs";
```

---

## 5. P0-P2 能力规划（范围与接口）

## 5.1 P0（高收益、低风险）

### P0-1 `stdiolink/constants`

目标：把运行时公共常量集中为只读约定，避免脚本硬编码“魔法值”。

API（只读对象）：

- `SYSTEM.os`: 当前系统标识（`windows`/`macos`/`linux`）
- `SYSTEM.arch`: CPU 架构（如 `x86_64` / `arm64`）
- `SYSTEM.isWindows/isMac/isLinux`: 平台布尔位

另外补充“当前应用相关路径”（均为绝对路径，规范化输出）：

- `APP_PATHS.appPath`: `stdiolink_service` 进程可执行文件路径
- `APP_PATHS.appDir`: 可执行文件所在目录
- `APP_PATHS.cwd`: 进程当前工作目录
- `APP_PATHS.serviceDir`: 当前 service 目录路径（来自 `argv` 解析结果）
- `APP_PATHS.serviceEntryPath`: 当前 service 的入口脚本路径（如 `index.js`）
- `APP_PATHS.serviceEntryDir`: 入口脚本所在目录
- `APP_PATHS.tempDir`: 系统临时目录
- `APP_PATHS.homeDir`: 用户主目录

实现建议：

- 由 C++ 构建常量对象并一次性注入，JS 侧 `deepFreeze`，保证运行时不可变。
- `serviceDir/serviceEntryPath/serviceEntryDir` 来源于现有 `ServiceDirectory` 解析结果，避免重复推导。

### P0-2 `stdiolink/path`

目标：统一路径操作，消除脚本拼字符串。

API（同步纯函数）：

- `join(...segments): string`
- `resolve(...segments): string`
- `dirname(p): string`
- `basename(p): string`
- `extname(p): string`
- `normalize(p): string`
- `isAbsolute(p): boolean`

实现建议：

- 以 `QDir` + `QFileInfo` 为底层，输出使用 `/` 分隔（Windows 内部可接受）。
- `resolve` 基于 `QDir::currentPath()`，与现有 ModuleLoader 语义一致。

### P0-3 `stdiolink/fs`

目标：提供服务脚本最常用文件系统操作，先同步版本。

API（同步）：

- `exists(path): boolean`
- `readText(path, encoding?="utf-8"): string`
- `writeText(path, text, options?): void`
- `readJson(path): any`
- `writeJson(path, value, options?): void`
- `mkdir(path, options?): void`
- `listDir(path, options?): string[]`
- `stat(path): { isFile, isDir, size, mtimeMs }`

选项建议：

- `writeText/writeJson`: `append?: boolean`, `ensureParent?: boolean`
- `mkdir`: `recursive?: boolean`
- `listDir`: `recursive?: boolean`, `filesOnly?: boolean`, `dirsOnly?: boolean`

实现建议：

- `QFile/QDir/QFileInfo/QJsonDocument`
- 统一异常模型：参数错误 `TypeError`；IO 错误 `Error("fs.xxx: ...")`

### P0-4 `stdiolink/time`

目标：补齐服务编排中的时间基础能力。

API：

- `nowMs(): number`（Unix epoch ms）
- `monotonicMs(): number`（进程单调时钟）
- `sleep(ms): Promise<void>`

实现建议：

- `nowMs`: `QDateTime::currentMSecsSinceEpoch()`
- `monotonicMs`: 基于 `QElapsedTimer` 全局实例
- `sleep`: 使用 `QTimer::singleShot` + QuickJS Promise capability；在主循环中按现有 job 机制执行

### P0-5 `getConfig` 深冻结增强（兼容升级）

目标：彻底消除配置对象被脚本误改写风险。

现状：仅 `Object.freeze(configRoot)`，非递归。

改造建议：

- 在 `js_config.cpp` 内部引入 `deepFreeze(value)`（JS 侧递归冻结）：
  - 遍历对象和数组
  - 循环引用保护（`Set`）
  - 冻结叶子对象
- `getConfig()` 返回缓存的深冻结对象副本引用（保持当前缓存策略）

兼容性：

- 对合法读取逻辑无影响
- 对“错误地修改配置”的旧脚本将更早暴露错误（符合预期）

## 5.2 P1（编排能力扩展）

### P1-1 `stdiolink/http`

目标：支持服务脚本调用外部 REST/HTTP 系统。

API（先同步后异步，建议直接上异步 Promise）：

- `request(options): Promise<{status, headers, bodyText, bodyJson?}>`
- `get(url, options?): Promise<...>`
- `post(url, body?, options?): Promise<...>`

`options`：

- `method`, `url`, `headers`, `query`, `timeoutMs`
- `body`（string/object/ArrayBuffer 简化版）
- `parseJson?: boolean`（默认按 `content-type` 自动推断）

实现建议：

- Qt Network：`QNetworkAccessManager` + `QNetworkRequest` + `QNetworkReply`
- 与 QuickJS Promise 对接：reply finished 时 resolve/reject
- 错误语义：
  - 网络错误 reject
  - HTTP 4xx/5xx 默认 resolve（附 status），避免与 transport error 混淆

### P1-2 `stdiolink/log`

目标：提供结构化日志，不再依赖纯字符串拼接。

API：

- `debug(message, fields?)`
- `info(message, fields?)`
- `warn(message, fields?)`
- `error(message, fields?)`
- `child(baseFields): Logger`

实现建议：

- 底层仍走 Qt logging（`qDebug/qInfo/qWarning/qCritical`）
- 序列化格式建议 JSON line：
  - `ts`, `level`, `msg`, `fields`, `service`, `projectId`（可选）
- 与 `console.*` 并存，不替代旧 API

## 5.3 P2（高级能力）

### P2-1 `stdiolink/process`（异步进程）

目标：解决同步 `exec` 阻塞问题，支持长时任务与流式输出。

API：

- `execAsync(program, args?, options?): Promise<{exitCode, stdout, stderr}>`
- `spawn(program, args?, options?): ProcessHandle`

`ProcessHandle`：

- `onStdout(cb)`, `onStderr(cb)`
- `onExit(cb)`
- `write(data)`, `closeStdin()`, `kill(signal?)`
- `pid`, `running`

实现建议：

- `QProcess` 信号桥接到 JS callback
- 回调管理需保存 `JSValue` 并在 finalizer 安全释放
- `exec` 保留原行为；新能力走 `stdiolink/process`，确保兼容

### P2-2 `openDriver` 可配置策略增强

目标：解决默认强插 `--profile=keepalive` 的刚性策略。

改造建议：

- 新增 `openDriver(program, args?, options?)`
- `options.profilePolicy`：
  - `"auto"`（默认，保留当前逻辑）
  - `"force-keepalive"`
  - `"preserve"`（不注入 profile）
- 可选 `metaTimeoutMs`、`startTimeoutMs`

兼容性：

- 旧签名保持可用
- 新参数仅增量扩展

---

## 6. 代码落位与目录建议

在 `src/stdiolink_service/` 新增：

- `bindings/js_constants.cpp/.h`
- `bindings/js_path.cpp/.h`
- `bindings/js_fs.cpp/.h`
- `bindings/js_time.cpp/.h`
- `bindings/js_http.cpp/.h`
- `bindings/js_log.cpp/.h`
- `bindings/js_process_async.cpp/.h`（可命名为 `js_process_ex.cpp/.h`）

并修改：

- `src/stdiolink_service/main.cpp`：注册新增内置模块
- `src/stdiolink_service/CMakeLists.txt`：纳入编译与 Qt Network 依赖
- `src/stdiolink_service/bindings/js_config.cpp`：深冻结
- `src/stdiolink_service/proxy/driver_proxy.cpp`：`openDriver` 选项增强（P2）

---

## 7. 分阶段实施路线

## 7.1 阶段 A（P0）

1. 引入 `stdiolink/constants`，完成 `SYSTEM` 与 `APP_PATHS` 常量注入
2. 引入 `stdiolink/path` 与 `stdiolink/fs` 同步最小 API
3. 引入 `stdiolink/time`，完成 `sleep()` Promise 桥接
4. 将 `getConfig` 从浅冻结升级为深冻结
5. 补充文档与 demo（最少一个服务脚本演示）

验收标准：

- `constants/path/fs/time` API 文档化并可在 demo 中运行
- `getConfig` 嵌套对象不可写（单测覆盖）
- 不影响现有 `Driver/openDriver/waitAny/exec` 测试

## 7.2 阶段 B（P1）

1. 引入 `stdiolink/http` 异步 API
2. 引入 `stdiolink/log` 结构化日志 API
3. 增加网络错误、超时、JSON 解析异常测试

验收标准：

- HTTP 正常/错误/超时分支全部可测
- log 输出结构稳定，可被脚本和外部工具解析

## 7.3 阶段 C（P2）

1. 引入 `stdiolink/process` 的 `execAsync/spawn`
2. 扩展 `openDriver` options 签名并保持兼容
3. 增加长任务、流式输出、取消与资源释放测试

验收标准：

- 长时进程不阻塞主循环
- 资源无泄漏（进程句柄与 JSValue 释放）
- `openDriver` 新旧签名均可用

---

## 8. 测试策略与用例矩阵

## 8.1 单元测试（`src/tests`）

建议新增：

- `test_constants_binding.cpp`
- `test_path_binding.cpp`
- `test_fs_binding.cpp`
- `test_time_binding.cpp`
- `test_http_binding.cpp`
- `test_log_binding.cpp`
- `test_process_async_binding.cpp`

关键场景：

- 参数类型错误（TypeError）
- 文件不存在、权限不足、JSON 非法
- sleep 定时精度（容差）
- HTTP 2xx/4xx/5xx/timeout/network error
- spawn 生命周期与回调释放

## 8.2 回归测试

必须回归现有：

- `test_driver_task_binding.cpp`
- `test_process_binding.cpp`
- `test_proxy_and_scheduler.cpp`
- `test_service_config_js.cpp`
- `test_js_stress.cpp`

目标：确保新增模块不改变现有模块行为。

---

## 9. 兼容性与风险控制

## 9.1 兼容策略

- `stdiolink` 主模块保持原导出不变
- 新能力用新模块名暴露
- `exec` 保持同步语义，不做破坏性改造
- `openDriver` 新参数采用可选 options，旧调用零修改

## 9.2 主要风险

- **事件循环耦合**：异步 API 需与 QuickJS job queue 协同，避免“Promise 不落地”
- **资源泄漏**：`QProcess/QNetworkReply` 与 `JSValue` 生命周期管理复杂
- **跨平台差异**：路径、编码、进程信号行为不一致

缓解措施：

- 统一封装 `JSValue` 持有/释放 helper
- 每个异步绑定提供 destructor/finalizer 测试
- Windows/macOS/Linux CI 全平台跑绑定测试

---

## 10. 文档与示例计划

建议新增/更新：

- `doc/manual/10-js-service/module-system.md`（新增内置子模块列表）
- `doc/manual/10-js-service/README.md`（导出能力矩阵更新）
- 新增：
  - `doc/manual/10-js-service/constants-binding.md`
  - `doc/manual/10-js-service/path-binding.md`
  - `doc/manual/10-js-service/fs-binding.md`
  - `doc/manual/10-js-service/time-binding.md`
  - `doc/manual/10-js-service/http-binding.md`
  - `doc/manual/10-js-service/log-binding.md`
  - `doc/manual/10-js-service/process-async-binding.md`

demo 建议：

- 在 `src/demo/js_runtime_demo/services/` 新增 `foundation_libs` 服务，集中演示 constants/path/fs/time/http/log/process。

---

## 11. 里程碑映射建议

- **M40（P0）**：`constants` + `path` + `fs` + `time` + `getConfig` 深冻结
- **M41（P1）**：`http` + `log`
- **M42（P2）**：`process` 异步 + `openDriver` options 增强

每个里程碑要求：

- 单测新增并全量通过
- demo 至少覆盖新增 API 的主路径
- manual 文档同步更新

---

## 12. 结论

该方案能在保持现有 JS 绑定兼容的前提下，分三步把 `stdiolink_service` 从“核心可用”提升到“可工程化落地”：

- P0 解决基础可用性与配置安全问题
- P1 解决服务编排中的外部交互与可观测性
- P2 解决长任务与高级进程控制，并补齐 `openDriver` 灵活性

整体技术栈延续 QuickJS + Qt，改动集中在 `stdiolink_service` 层，风险可控、可逐步交付。
