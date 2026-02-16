# 里程碑 73：stdiolink_server 主线程阻塞消除与异步化改造（TDD）

> **前置条件**: 里程碑 72 已完成；`doc/server_async_refactor_plan.md` 已评审通过并作为本里程碑实现基线  
> **目标**: 在不破坏既有调度与 API 兼容性的前提下，消除 `stdiolink_server` 主线程可避免阻塞点，并以 TDD 方式实现可达路径单元测试覆盖 100%

---

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| `stdiolink_server/manager` | `InstanceManager` 启停异步化（移除 `waitForStarted/waitForFinished`）、`instanceStartFailed` 事件与 `instanceFinished` 语义闭环 |
| `stdiolink_server/http` | `handleProjectStart` 返回 `status:"starting"`，`handleDriverScan` 异步化，DriverLab WebSocket 启停改为信号驱动 |
| `stdiolink_server/scanner` | Driver 扫描迁移到 `QtConcurrent + QFuture`，主线程只做结果回填 |
| `stdiolink/guard` | `ProcessGuardServer` 区分默认 UUID 启动路径和 override 路径，保留 stale socket 恢复 |
| `ScheduleEngine/EventBus` | 保持 daemon 重启链路连续；新增 `instance.startFailed` 事件发布 |
| `src/tests` | 新增/扩展 GTest：覆盖正常、边界、异常、并发、回归路径，形成可追溯路径矩阵 |

- 所有阻塞等待点从主线程移除，保持行为可观测与可回归。
- 对外行为变化仅限异步语义增强，不引入破坏性 API 变更。
- 单元测试作为交付主件，不接受“实现完成但测试待补”。

---

## 2. 背景与问题

- 当前 `stdiolink_server` 采用单线程事件循环，HTTP/SSE/WebSocket/调度/进程管理共享主线程。
- 关键路径中存在 `waitForStarted()` / `waitForFinished()` / 串行扫描等待，导致请求级别抖动甚至全局冻结。
- 阻塞点不仅影响响应时延，还会干扰调度器的心跳与失败计数行为，形成级联风险。

**范围**:
- `InstanceManager`、`DriverLabWsConnection`、`DriverManagerScanner`、`ApiRouter`、`ServerManager`、`ProcessGuardServer` 的异步化改造。
- 事件语义统一：`instance.startFailed`（去重）+ `instance.finished`（始终发射，含 `FailedToStart` 手动补发）。
- 补齐覆盖上述变更点的单元/集成回归测试。

**非目标**:
- 不改动调度策略参数（如 `restartDelayMs/maxConsecutiveFailures`）本身。
- 不改动 WebUI 功能范围，仅验证兼容性（展示和统计正确）。
- 不引入额外线程模型重构（如全量 handler 线程池化）。

---

## 3. 技术要点

### 3.1 Instance 启动/停止契约

- `startInstance()` 改为“先入表、后启动、信号驱动状态迁移”。
- 启动状态机：`starting -> running/failed/stopped`。
- `FailedToStart` 特殊约束：Qt 不保证发 `finished`，需手动补发 `instanceFinished(instanceId, projectId, -1, QProcess::CrashExit)`。
- `startFailedEmitted` 去重标记：避免 `errorOccurred/timeout/onProcessFinished` 多路径重复发 `instanceStartFailed`。

### 3.2 HTTP/SSE/WebSocket 契约

- `POST /api/projects/{id}/start`：返回 `200` + `{"instanceId", "status":"starting"}`。
- `instance.started`：在 `QProcess::started` 后发射，携带 PID。
- `instance.startFailed`：启动失败/超时/starting 阶段退出时发射，保证仅一次。
- `instance.finished`：始终发射；`FailedToStart` 场景使用 `exitCode=-1, CrashExit`。
- DriverLab WS：`driver.started` 改为 `QProcess::started` 后发送；停机去掉阻塞等待。

### 3.3 Driver 扫描异步化契约

- `ServerManager::rescanDriversAsync()` 使用 `QtConcurrent::run`。
- 工作线程只扫描与导出，主线程通过 `.then(this, ...)` 回填 `m_driverCatalog`。
- `ApiRouter::handleDriverScan()` 返回 `QFuture<QHttpServerResponse>`。
- Qt 版本下限统一为 `6.6+`（依赖 `QtFuture::makeReadyValueFuture`）。

### 3.4 ProcessGuard 启动路径约束

- `start()`（默认 UUID）跳过 pre-probe，直接 listen。
- `start(nameOverride)` 保留 pre-probe，防止名称冲突。
- `listen()` 失败且 `AddressInUseError` 时保留 `removeServer + retry`，确保 stale socket 可恢复。

### 3.5 线程模型硬约束

- 主线程单写：实例表、项目表、服务表、调度状态、事件总线。
- 工作线程只做纯 IO/计算，不直接操作主线程 QObject。
- 结果回传必须通过 `QFuture` 续体或 queued 语义。

---

## 4. 实现步骤（TDD Red-Green-Refactor）

### 4.1 Red — 编写失败测试

- 缺陷复现条件：启动失败、启动超时、starting 阶段退出、并发扫描、WebSocket 启停、guard 残留、API 异步响应。
- Red 阶段测试（修复前必须失败）:

| 测试 ID | 目标路径 | 测试文件 | 修复前失败点 |
|--------|----------|---------|-------------|
| `M73_R01` | `startInstance` 同步阻塞 | `src/tests/test_instance_manager.cpp` | 调用耗时超阈值（存在 5s wait） |
| `M73_R02` | `QProcess::started` 状态迁移 | `src/tests/test_instance_manager.cpp` | 无 `starting->running` 异步迁移 |
| `M73_R03` | `FailedToStart` 事件完整性 | `src/tests/test_instance_manager.cpp` | `instance.startFailed` / `instance.finished(-1)` 语义不完整 |
| `M73_R04` | 启动超时路径 | `src/tests/test_instance_manager.cpp` | 超时后未统一转失败并清理 |
| `M73_R05` | starting 阶段提前退出 | `src/tests/test_instance_manager.cpp` | `startFailed` 去重或补发逻辑缺失 |
| `M73_R06` | `terminateInstance` 非阻塞 | `src/tests/test_instance_manager.cpp` | stop 路径存在阻塞等待 |
| `M73_R07` | `terminateByProject/terminateAll` | `src/tests/test_instance_manager.cpp` | 批量终止耗时线性阻塞 |
| `M73_R09` | `startFailedEmitted` 去重 | `src/tests/test_instance_manager.cpp` | 多路径重复发 `instance.startFailed` |
| `M73_R10` | EventBus `instance.startFailed` | `src/tests/test_server_manager.cpp` | 无对应 SSE 事件推送 |
| `M73_R11` | ScheduleEngine daemon 链路 | `src/tests/test_schedule_engine.cpp` | `FailedToStart` 不触发失败计数/重启 |
| `M73_R12` | daemon 抑制阈值 | `src/tests/test_schedule_engine.cpp` | 连续失败抑制逻辑失效 |
| `M73_R13` | `handleProjectStart` 响应契约 | `src/tests/test_api_router.cpp` | 未返回 `status:"starting"` |
| `M73_R15` | `handleDriverScan` 异步响应 | `src/tests/test_api_router.cpp` | 扫描请求阻塞主线程 |
| `M73_R16` | 扫描期间并发 API 可用性 | `src/tests/test_api_router.cpp` | 并发请求被串行阻塞 |
| `M73_R17` | DriverLab 启动异步通知 | `src/tests/test_driverlab_ws_handler.cpp` | `driver.started` 非信号驱动 |
| `M73_R18` | DriverLab 启动失败处理 | `src/tests/test_driverlab_ws_handler.cpp` | 错误消息/连接关闭不稳定 |
| `M73_R19` | DriverLab stop 非阻塞 | `src/tests/test_driverlab_ws_handler.cpp` | `stopDriver` 仍有等待 |
| `M73_R20` | DriverLab meta 超时 | `src/tests/test_driverlab_ws_handler.cpp` | meta timeout 行为不确定 |
| `M73_R21` | `rescanDriversAsync` 空目录路径 | `src/tests/test_server_manager.cpp` | future 非 ready 或 catalog 未清空 |
| `M73_R22` | `rescanDriversAsync` 成功路径 | `src/tests/test_server_manager.cpp` | 扫描结果未回填主线程 catalog |
| `M73_R23` | ProcessGuard 默认启动路径 | `src/tests/test_process_guard.cpp` | UUID 路径仍走 probe |
| `M73_R24` | ProcessGuard override 冲突路径 | `src/tests/test_process_guard.cpp` | override 冲突识别失效 |
| `M73_R25` | ProcessGuard stale socket 恢复 | `src/tests/test_process_guard.cpp` | `AddressInUseError` 不可恢复 |
| `M73_R26` | 前端 `exitCode=-1` 兼容（后端契约） | `src/tests/test_api_router.cpp` | runtime/log/status 数据出现异常值 |

#### GTest 用例框架（骨架）

```cpp
// src/tests/test_instance_manager.cpp
TEST(InstanceManagerAsyncTest, M73_R01_StartInstanceReturnsImmediatelyWithoutBlocking) {
    // 构造可启动服务桩进程
    // 调用 startInstance() 并记录耗时
    // 断言：耗时小于阈值（例如 < 100ms），返回 instanceId 非空，初始状态为 starting
}

TEST(InstanceManagerAsyncTest, M73_R03_FailedToStartEmitsStartFailedAndManualFinishedExactlyOnce) {
    // 构造不可执行程序路径触发 FailedToStart
    // 监听 instanceStartFailed / instanceFinished
    // 断言：startFailed 收到一次；finished 收到一次且 exitCode=-1, CrashExit
}

TEST(InstanceManagerAsyncTest, M73_R09_StartFailedEventIsDeduplicatedAcrossAllPaths) {
    // 构造 timeout + finished 竞争窗口
    // 断言：instanceStartFailed 总次数为 1
}
```

```cpp
// src/tests/test_schedule_engine.cpp
TEST(ScheduleEngineAsyncTest, M73_R11_DaemonRestartIsTriggeredAfterFailedToStartManualFinished) {
    // 构造 daemon 项目 + 启动失败服务
    // 断言：失败计数递增，按 restartDelay 触发下一次启动
}

TEST(ScheduleEngineAsyncTest, M73_R12_DaemonSuppressedAfterMaxConsecutiveFailures) {
    // 连续制造失败直到达到阈值
    // 断言：触发 scheduleSuppressed，后续不再重启
}
```

```cpp
// src/tests/test_api_router.cpp
TEST(ApiRouterAsyncTest, M73_R13_ProjectStartReturnsStartingStatus) {
    // 调用 POST /api/projects/{id}/start
    // 断言：HTTP 200，body 含 instanceId 和 status="starting"
}

TEST(ApiRouterAsyncTest, M73_R16_DriverScanDoesNotBlockOtherApiRequests) {
    // 并发触发 /api/drivers/scan 与 /api/server/status
    // 断言：status 请求在短时内返回，不受 scan 阻塞
}

TEST(ApiRouterAsyncTest, M73_G27_DriverScanRejectsInvalidBodyWithBadRequest) {
    // 发送非法 JSON 或 refreshMeta 类型错误
    // 断言：HTTP 400，错误信息可判定
}

TEST(ApiRouterAsyncTest, M73_R26_RuntimePayloadHandlesExitCodeMinusOneWithoutInvalidValues) {
    // 构造 FailedToStart 后请求 runtime/logs/status
    // 断言：exitCode=-1 可序列化并可解析，不出现 NaN/空字段异常
}
```

```cpp
// src/tests/test_driverlab_ws_handler.cpp
TEST(DriverLabWsAsyncTest, M73_R17_DriverStartedMessageSentOnStartedSignal) {
    // 建立 WS，启动 driver
    // 断言：收到 driver.started 且包含 pid
}

TEST(DriverLabWsAsyncTest, M73_R19_StopDriverReturnsQuicklyWithoutWaitForFinished) {
    // 启动长运行进程后调用 stop
    // 断言：stop 路径快速返回，连接与资源正确回收
}
```

```cpp
// src/tests/test_server_manager.cpp
TEST(ServerManagerAsyncTest, M73_R21_RescanDriversAsyncReturnsReadyFutureWhenDirMissing) {
    // 删除 drivers 目录后调用 rescanDriversAsync
    // 断言：future 立即 ready，catalog 被清空
}

TEST(ServerManagerAsyncTest, M73_R22_RescanDriversAsyncUpdatesCatalogOnMainThread) {
    // 构造可扫描 driver 目录
    // 断言：future 完成后 catalog 更新且统计值正确
}
```

```cpp
// src/tests/test_process_guard.cpp
TEST(ProcessGuardAsyncTest, M73_R23_DefaultStartSkipsProbeAndCanListen) {
    // 调用 start()
    // 断言：可监听；不因 probe 路径导致额外失败
}

TEST(ProcessGuardAsyncTest, M73_R25_AddressInUseRecoversByRemoveServerAndRetry) {
    // 构造 stale socket 场景
    // 断言：第一次 listen 失败后可 remove+retry 成功
}
```

- Red 证据要求：每个 `M73_Rxx` 在修复前有失败执行记录（命令 + 关键断言摘要）。

### 4.2 Green — 最小修复实现

1. `InstanceManager`：移除同步等待，补齐 `started/errorOccurred/timeout/finished` 事件链，保持 `instanceFinished` 始终发射不变量。
2. `ServerManager/EventBus`：接入 `instanceStartFailed` 发布；保持 `instance.finished` 现有消费方兼容。
3. `ApiRouter`：`handleProjectStart` 返回 `status:"starting"`；`handleDriverScan` 改 `QFuture<QHttpServerResponse>`。
4. `DriverLabWsConnection`：改为信号驱动启动与停止；去除 `waitForStarted/waitForFinished`。
5. `DriverManagerScanner/ServerManager`：引入 `QtConcurrent` 异步扫描 + 主线程回填。
6. `ProcessGuardServer`：拆分默认/override 启动路径，保留 stale socket 恢复。
7. `CMake`：补齐 `Qt6::Concurrent` 依赖与 `Qt >= 6.6` 版本约束。
8. 执行 `M73_Rxx` 到 Green，并补全成功路径验证（`M73_Gxx`）。
9. 回归守卫（修复前应通过，修复后继续通过）：
   - `M73_G08`：`waitAllFinished` 收敛路径不回归。
   - `M73_G14`：`handleProjectStart` 冲突分支行为不回归。
   - `M73_G27`：`handleDriverScan` 非法请求体保持 `400` 错误语义。

### 4.3 Refactor — 重构（按需）

- 抽取重复信号处理与错误文案构造，避免多处复制。
- 统一异步测试辅助（等待信号、超时守卫、并发请求工具函数）。
- 清理临时状态字段命名，保证可读性与可追踪性。

---

## 5. 文件变更清单

### 5.1 新增文件

- `doc/milestone/milestone_73_server_async_nonblocking_refactor.md` - 本里程碑计划文档。
- `src/tests/test_server_async_refactor.cpp` - 跨模块异步语义与并发回归测试（建议新增）。

### 5.2 修改文件

- `src/stdiolink_server/manager/instance_manager.h` - 新增 `instanceStartFailed` 信号与状态字段约束。
- `src/stdiolink_server/manager/instance_manager.cpp` - 启停异步化、失败去重、manual finished 补发。
- `src/stdiolink_server/model/instance.h` - 新增 `startFailedEmitted` 去重标记字段。
- `src/stdiolink_server/server_manager.cpp` - EventBus 事件接入与扫描异步入口。
- `src/stdiolink_server/server_manager.h` - `rescanDriversAsync` 声明。
- `src/stdiolink_server/http/api_router.h` - `handleDriverScan` 返回类型更新。
- `src/stdiolink_server/http/api_router.cpp` - `project start/driver scan` 异步契约。
- `src/stdiolink_server/http/driverlab_ws_connection.h` - 启停异步状态辅助字段。
- `src/stdiolink_server/http/driverlab_ws_connection.cpp` - 启停信号驱动替换阻塞等待。
- `src/stdiolink_server/CMakeLists.txt` - `Qt6::Concurrent` 链接与版本约束。
- `src/stdiolink/guard/process_guard_server.h` - 公共 listen 路径声明（如 `listenInternal`）。
- `src/stdiolink/guard/process_guard_server.cpp` - 默认/override 启动路径拆分 + stale socket 恢复。

### 5.3 测试文件

- `src/tests/test_instance_manager.cpp` - 启停异步化、失败去重、manual finished 补发。
- `src/tests/test_schedule_engine.cpp` - daemon 失败计数与重启/抑制链路。
- `src/tests/test_api_router.cpp` - `project start` 返回契约、`driver scan` 异步与并发可用性。
- `src/tests/test_driverlab_ws_handler.cpp` - DriverLab 启停信号路径与超时行为。
- `src/tests/test_server_manager.cpp` - `rescanDriversAsync` future 与 catalog 回填。
- `src/tests/test_process_guard.cpp` - 默认启动、override 冲突、stale socket 恢复。
- `src/tests/test_server_async_refactor.cpp` - 跨模块时序/事件一致性回归（建议新增）。

---

## 6. 测试与验收

### 6.1 单元测试（必填，重点）

- 测试对象:
  - `InstanceManager` 状态机与信号语义。
  - `ApiRouter` 路由契约与状态码/响应体。
  - `ScheduleEngine` 对 `instanceFinished` 的失败计数依赖链路。
  - `DriverLabWsConnection` 启停和元数据超时路径。
  - `ServerManager::rescanDriversAsync` 线程回填语义。
  - `ProcessGuardServer` 两类启动路径与恢复逻辑。
- 用例分层:
  - 正常路径：成功启动、成功扫描、正常 stop。
  - 边界路径：空目录扫描、快速退出、并发请求。
  - 异常路径：`FailedToStart`、超时、冲突、stale socket。
  - 回归路径：`instance.finished` 始终发射、`startFailed` 去重、`exitCode=-1` 兼容。
- 断言要点:
  - 信号次数（精确到 once）、状态迁移、退出码和退出状态。
  - HTTP 状态码 + JSON 字段完整性。
  - 并发场景下响应延迟阈值和无阻塞证据。
- 桩替身策略:
  - 使用现有 `test_service_stub` / `test_process_async_stub_main` / `test_slow_meta_driver_main`。
  - 必要时新增轻量 fake 进程桩，仅用于复现 `FailedToStart`、超时、快速退出等可控路径。
- 测试文件:
  - `src/tests/test_instance_manager.cpp`
  - `src/tests/test_schedule_engine.cpp`
  - `src/tests/test_api_router.cpp`
  - `src/tests/test_driverlab_ws_handler.cpp`
  - `src/tests/test_server_manager.cpp`
  - `src/tests/test_process_guard.cpp`
  - `src/tests/test_server_async_refactor.cpp`（建议）

#### 路径矩阵（决策点 -> 路径 -> 用例 ID）

| 决策点 | 可达路径 | 用例 ID |
|--------|----------|---------|
| `startInstance` 启动结果 | `started` 成功 | `M73_R02` |
| `startInstance` 启动结果 | `FailedToStart` | `M73_R03` |
| `startInstance` 启动结果 | `timeout -> kill -> finished` | `M73_R04` |
| `startInstance` 启动结果 | `starting 阶段提前退出` | `M73_R05` |
| `instanceStartFailed` 去重 | `errorOccurred + onProcessFinished` 竞争 | `M73_R09` |
| `instanceFinished` 发射策略 | `FailedToStart` 手动补发 | `M73_R03` |
| `instanceFinished` 发射策略 | 普通 finished 信号路径 | `M73_R02`, `M73_R04`, `M73_R05` |
| 终止 API | `terminateInstance` 非阻塞返回 | `M73_R06` |
| 终止 API | `terminateByProject/terminateAll` 批量路径 | `M73_R07` |
| shutdown 收敛 | `waitAllFinished` 最终 drain 成功 | `M73_G08` |
| ScheduleEngine daemon | 失败后重启 | `M73_R11` |
| ScheduleEngine daemon | 达阈值后抑制 | `M73_R12` |
| `handleProjectStart` | 正常返回 `status:"starting"` | `M73_R13` |
| `handleProjectStart` | 冲突分支不回归 | `M73_G14` |
| `handleDriverScan` | 请求体非法 | `M73_G27` |
| `handleDriverScan` | 异步扫描成功响应 | `M73_R15` |
| `handleDriverScan` 并发 | 扫描期间其他 API 可用 | `M73_R16` |
| DriverLab 启动 | started 信号驱动消息 | `M73_R17` |
| DriverLab 启动 | 启动失败错误路径 | `M73_R18` |
| DriverLab 停止 | 非阻塞 stop + 资源回收 | `M73_R19` |
| DriverLab meta | 超时错误路径 | `M73_R20` |
| `rescanDriversAsync` | 目录不存在 ready future | `M73_R21` |
| `rescanDriversAsync` | 目录存在扫描回填 | `M73_R22` |
| ProcessGuard | 默认 `start()` UUID 路径 | `M73_R23` |
| ProcessGuard | override 冲突拒绝 | `M73_R24` |
| ProcessGuard | stale socket 恢复 | `M73_R25` |
| 前端兼容性（后端可测部分） | `exitCode=-1` 序列化/接口一致性 | `M73_R26` |

- 覆盖要求（硬性）:
  - 上表所有可达路径必须有且仅有对应测试。
  - 不可达路径需在测试文档附录给出设计级证明（状态机前置条件 + 触发条件不可满足说明）。

### 6.2 集成/端到端测试（按需）

- HTTP + SSE 联动:
  - 启动项目后立即返回 `starting`，随后通过 SSE 收到 `instance.started` 或 `instance.startFailed`。
- daemon 回归:
  - 构造连续失败项目，验证重启、失败计数、抑制事件完整。
- Driver 扫描并发:
  - 大量 driver 扫描期间并发请求 `server status`、`project list`，验证系统可响应。
- WebUI 兼容:
  - 验证 `exitCode=-1` 在实例列表、日志面板、失败计数、重启统计展示正常。

### 6.3 验收标准

- [ ] 所有 `waitForStarted/waitForFinished` 主线程阻塞点按方案移除或改为异步流程。
- [ ] `instance.finished` 在 `FailedToStart`、timeout、normal/crash 等路径均可观测且语义一致。
- [ ] `instance.startFailed` 去重生效，单次启动最多发一次。
- [ ] `POST /api/projects/{id}/start` 返回 `status:"starting"`，无兼容性回退问题。
- [ ] `POST /api/drivers/scan` 异步化后并发 API 不被阻塞。
- [ ] ProcessGuard 两条启动路径均稳定，stale socket 可恢复。
- [ ] `M73_Rxx/M73_Gxx` 全部通过，且现有相关测试套件无回归。

---

## 7. 风险与控制

- 风险: 启动/退出信号存在竞态，导致状态遗漏或重复事件。
  - 控制: 使用 `startFailedEmitted` 去重；测试覆盖竞争路径；关键状态迁移加日志。
- 风险: 异步扫描回填线程上下文错误，导致跨线程访问。
  - 控制: 强制 `.then(this, ...)` 回主线程；测试断言 catalog 更新发生在主线程上下文。
- 风险: API 异步改造影响老客户端对返回字段的依赖。
  - 控制: 保持状态码与主体字段兼容，仅新增 `status` 语义；回归测试覆盖旧字段。
- 风险: DriverLab stop 去阻塞后析构时序问题。
  - 控制: 保留 kill 先行策略；增加多次 stop/close 幂等测试。
- 风险: `FailedToStart` 手动补发 `instanceFinished` 与真实 finished 双发。
  - 控制: 仅在 `errorOccurred(FailedToStart)` 分支手动补发；路径测试断言事件次数严格为 1。

---

## 8. 里程碑完成定义（DoD）

- [ ] 代码实现与测试实现完成，关键路径行为与计划一致。
- [ ] `doc/server_async_refactor_plan.md` 与实现保持同步，无语义漂移。
- [ ] 对外 API 契约与事件语义变更已记录并通过兼容性检查。
- [ ] Red 阶段：`M73_Rxx` 失败测试有执行记录可追溯。
- [ ] Green 阶段：新增与既有测试全量通过，无回归。
- [ ] Refactor 阶段（如执行）：重构后全量测试仍通过。
