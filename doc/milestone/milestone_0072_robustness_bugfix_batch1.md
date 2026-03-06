# 里程碑 72：健壮性缺陷修复批次一（P0/P1/P2，TDD）

> **前置条件**: 里程碑 71（ProcessGuard 全链路集成）已完成；`doc/robustness_improvement_report.md` 的问题清单已评审并完成取舍  
> **目标**: 以 TDD 方式一次性落地 8 个已确认修复项（P0-1/2/3/4、P1-2/3/4、P2-1），并确保新增路径单元测试可达分支覆盖率为 100%

---

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| `stdiolink_server` | P0-1 PATH 分隔符统一；P0-2 原子写盘；P0-3 日志 tail 有界读取；P1-2 非支持平台 ProcessMonitor 返回 501；P1-4 SSE 断开回收；P2-1 HTTP body 大小上限 |
| `stdiolink` 核心库 | P0-4 Host `Driver` 输出缓冲硬上限 |
| `stdiolink_service` | P0-4 `execAsync` stdout/stderr 缓冲硬上限；P2-1 `ServiceArgs::loadConfigFile` 配置大小上限 |
| `guard` 工具层 | P1-3 ProcessGuard 残留 socket 清理 + 重试 |
| `src/tests` | 为 8 个缺陷补齐 Red/Green 回归测试；新增大输出桩进程，覆盖超限行为 |

- 所有修复点先补失败测试（Red），再做最小实现（Green），最后统一清理重复代码（Refactor）
- 对外行为变化仅限：
  - 非支持平台的进程监控接口返回 `501 Not Implemented`
  - 请求体/配置文件超限返回明确错误（`413` 或参数错误）
  - 输出缓冲超限时主动终止并返回结构化错误

---

## 2. 背景与问题

- 当前实现存在 8 个已确认问题：跨平台 PATH 拼接、非原子写盘、日志 tail OOM 风险、输出缓冲无上限、平台能力误报、guard 残留恢复不足、SSE 连接回收缺口、输入大小无上限。
- 这些问题覆盖 Host/Service/Server 三层，且都属于“稳定性与安全边界”而非新功能，适合集中以问题修复里程碑推进。
- 本批次已明确跳过：
  - `P1-1`（同步阻塞去除）
  - `P2-2`（terminate + kill 两段式终止策略）

**范围**：
- 覆盖 8 个选定缺陷的修复与回归测试
- 补齐对应文档中的 API 行为与错误语义
- 不改变已有协议字段语义，仅新增错误分支和保护阈值

**非目标**：
- 不重构扫描流程为异步任务（P1-1）
- 不调整当前“直接 kill”终止策略（P2-2）
- 不涉及 WebUI 交互改造，仅保证 API 可判定性增强

---

## 3. 技术要点

### 3.1 P0-1 PATH 分隔符统一（跨平台）

- 新增公共函数（建议：`appendDirToPath(const QString& dir, QProcessEnvironment& env)`）。
- 内部使用 `QDir::listSeparator()`，禁止硬编码 `";"`。
- 工具函数放在中立目录，避免 `scanner/http` 反向依赖 `manager`：
  - `src/stdiolink_server/utils/process_env_utils.h`
  - `src/stdiolink_server/utils/process_env_utils.cpp`
- 应用到三处：
  - `src/stdiolink_server/manager/instance_manager.cpp`
  - `src/stdiolink_server/scanner/driver_manager_scanner.cpp`
  - `src/stdiolink_server/http/driverlab_ws_connection.cpp`

### 3.2 P0-2 原子写盘（防半写文件）

- `ProjectManager::saveProject()` 改为 `QSaveFile + commit()`，并校验 `write()` 返回字节数与目标字节数一致。
- `server_manager.cpp` 内 `writeTextFile()` 改为 `QSaveFile + commit()`，同样严格校验写入字节数。
- 失败时返回 false，保证目标文件保持旧版本。

### 3.3 P0-3 日志 tail 有界读取方案（主方案：窗口正向）

- 替换 `readTailLines()` 的整文件 `readAll()`，采用“有界窗口正向读取”作为主实现：
  1. 计算窗口起点：`start = max(0, fileSize - maxReadBytes)`（建议 `maxReadBytes = 4MB`）
  2. `QFile::seek(start)` 后正向读取窗口内容
  3. 若 `start > 0`，丢弃首个不完整行
  4. 仅保留最后 `maxLines`
- 方案对比：
  - 主方案（窗口正向）：实现简单、边界更稳定、满足内存上限要求
  - 备选（逆向分块）：可更早停，但实现复杂度高，UTF-8/CRLF 边界更易出错
- 本里程碑默认采用主方案；仅在主方案无法满足性能指标时再评估逆向分块。

### 3.4 P0-4 输出缓冲硬上限（8MB）

- 统一阈值：`8 * 1024 * 1024`（每通道）
- 影响点：
  - `stdiolink::Driver::m_buf`
  - `DriverLabWsConnection::m_stdoutBuffer`
  - `js_process_async.cpp` 的 `capturedStdout` / `capturedStderr`
- 超限行为：
  - Host Driver：推送结构化错误并结束当前任务
  - DriverLab：向 WS 返回错误并终止驱动进程
  - execAsync：Promise reject，错误信息包含超限通道与阈值

### 3.5 P1-2 ProcessMonitor 平台能力显式化

- 对以下接口增加平台支持判断：
  - `GET /api/instances/<id>/process-tree`
  - `GET /api/instances/<id>/resources`
- 若平台不支持：
  - HTTP `501 Not Implemented`
  - 响应体统一为：
    ```json
    {
      "error": "process monitor not supported on this platform",
      "code": "PROCESS_MONITOR_UNSUPPORTED",
      "supported": false,
      "platform": "<platform>"
    }
    ```
- 支持平台保持原有 `200` 响应结构。

### 3.6 P1-3 ProcessGuard 残留 socket 清理

- 与现有 pre-listen probe 关系：
  - 保留当前实现中的 pre-listen probe（用于快速识别活跃 server）
  - 不新增第二次冗余 probe
- `ProcessGuardServer::start(name)` 在 pre-listen probe 已确认“不可连”后，若 `listen()` 仍失败且错误为 `AddressInUseError`：
  1. 调用 `QLocalServer::removeServer(name)`
  2. 重试一次 `listen(name)`
- 仅重试一次，避免无限循环。

### 3.7 P1-4 SSE 断开回收接入

- 约束说明：`QHttpServerResponder` 不是 `QObject`，且 `writeChunk()` 无返回值，Qt 当前不提供直接“客户端断开”回调。
- 可实现策略（本里程碑采用）：
  1. 保留心跳（30s）作为死连接探测窗口
  2. 为每个连接维护租约时间戳（`createdAt` / `lastSendAt`）
  3. 心跳回调中扫描超时连接（例如 `now - lastSendAt > 2 * kHeartbeatIntervalMs`）并执行 `close() + emit disconnected()`
  4. 显式关闭路径（`evictOldestConnection()`、`closeAllConnections()`）直接回收连接，不经由 `disconnected()` 信号
  5. `EventStreamHandler::addConnection()` 连接 `disconnected()` 到 `removeConnection()`
  6. 回收一致性修复：`removeConnection()` 使用 `deleteLater()`；`closeAllConnections()`（析构路径）使用直接 `delete`，因为事件循环可能已停止，`deleteLater()` 无法保证执行
- 保持现有驱逐策略（`kMaxSseConnections`）不变；断开回收为增强路径。

### 3.8 P2-1 输入大小上限（统一防护）

- HTTP JSON body 增加上限（建议 1MB）：
  - 超限返回 `413 Payload Too Large`
- `ServiceArgs::loadConfigFile()` 增加文件大小前置校验（建议 1MB）：
  - 超限返回明确错误字符串
- stdin 配置读取同样执行上限判断。

---

## 4. 实现步骤（TDD Red-Green-Refactor）

### 4.1 Red — 编写失败测试

- 缺陷复现条件：按 8 个问题分别构造最小输入（跨平台 PATH、写盘失败、超大日志、长输出无换行、非支持平台、guard 残留、SSE 断开、超大 body/配置）。
- Red 阶段测试清单（修复前必须失败）：

| 测试 ID | 缺陷编号 | 测试文件 | 失败断言（修复前） |
|--------|----------|---------|--------------------|
| `M72_R01` | P0-1 | `src/tests/test_instance_manager.cpp` | PATH 使用系统分隔符（非 Windows 为 `:`） |
| `M72_R02` | P0-1 | `src/tests/test_driver_manager_scanner.cpp` | 导出 meta 子进程 PATH 使用系统分隔符 |
| `M72_R03` | P0-1 | `src/tests/test_driverlab_ws_handler.cpp` | DriverLab 子进程 PATH 使用系统分隔符 |
| `M72_R04` | P0-2 | `src/tests/test_project_manager.cpp` | 保存失败后旧文件内容不变 |
| `M72_R05` | P0-2 | `src/tests/test_server_manager.cpp` | createService 任一文件写失败不留下半成品 |
| `M72_R06` | P0-3 | `src/tests/test_api_router.cpp` | 大日志 tail 请求不触发整文件内存增长 |
| `M72_R07` | P0-3 | `src/tests/test_api_router.cpp` | 超长单行时返回行数正确且内存受限 |
| `M72_R08` | P0-4 | `src/tests/test_host_driver.cpp` | Host Driver 遇到超大无换行输出返回结构化超限错误 |
| `M72_R09` | P0-4 | `src/tests/test_driverlab_ws_handler.cpp` | DriverLab 超限后发错误消息并关闭驱动 |
| `M72_R10` | P0-4 | `src/tests/test_process_async_binding.cpp` | execAsync 超限时 Promise reject（而非 OOM 风险累积） |
| `M72_R11` | P1-2 | `src/tests/test_api_router.cpp` | 非支持平台 process-tree 返回 501 + `code=PROCESS_MONITOR_UNSUPPORTED` + `supported=false` |
| `M72_R12` | P1-2 | `src/tests/test_api_router.cpp` | 非支持平台 resources 返回 501 + `code=PROCESS_MONITOR_UNSUPPORTED` + `supported=false` |
| `M72_R13` | P1-3 | `src/tests/test_process_guard.cpp` | 残留 socket 情况下可 remove + 重试恢复 |
| `M72_R14` | P1-4 | `src/tests/test_event_bus.cpp` | 触发连接断开后 activeConnectionCount 递减 |
| `M72_R15` | P2-1 | `src/tests/test_api_router.cpp` | 超过 body 上限返回 413 |
| `M72_R16` | P2-1 | `src/tests/test_service_args.cpp` | 超大 config 文件返回明确错误 |
| `M72_R17` | P2-1 | `src/tests/test_service_args.cpp` | stdin 超大输入返回明确错误 |

#### gtest 用例框架（示意，具体断言写在实现阶段）

```cpp
// src/tests/test_instance_manager.cpp
TEST(InstanceManagerTest, M72_R01_PathUsesPlatformListSeparator) {
    // 构造 instance 启动场景，读取子进程环境中的 PATH
    // 断言 appDir 与原 PATH 之间使用 QDir::listSeparator()
}
```

```cpp
// src/tests/test_driver_manager_scanner.cpp
TEST_F(DriverManagerScannerTest, M72_R02_ScanExportUsesPlatformPathSeparator) {
    // 触发 tryExportMeta 路径，记录导出子进程 PATH
    // 断言分隔符与平台一致
}
```

```cpp
// src/tests/test_project_manager.cpp
TEST(ProjectManagerIoTest, M72_R04_SaveProjectIsAtomicOnWriteFailure) {
    // 预先写入旧文件；注入写失败
    // 调用 saveProject 后断言旧文件内容未变化
}

// src/tests/test_server_manager.cpp
TEST(ServerManagerTest, M72_R05_CreateServiceAtomicWrites_NoPartialFiles) {
    // 注入 writeTextFile 失败（例如目标不可写或 fake 失败）
    // 断言 manifest/index/config 不出现半写或空文件
}
```

```cpp
// src/tests/test_api_router.cpp
TEST(ApiRouterTest, M72_R06_ProjectLogsTailReadIsBoundedForLargeFile) {
    // 构造大日志文件，调用 /api/instances/<id>/logs
    // 断言返回末尾行正确；并验证仅读取 fileSize 尾部 maxReadBytes 窗口
}

TEST(ApiRouterTest, M72_R11_ProcessTreeReturns501WhenUnsupportedPlatform) {
    // 在非支持平台分支验证 501
    // 断言 body.supported == false 且包含明确错误文本
}

TEST(ApiRouterTest, M72_R15_ParseJsonBodyRejectsOversizedPayload) {
    // 构造 > maxBodySize 的 JSON body 调用 POST 路由
    // 断言返回 413
}
```

```cpp
// src/tests/test_host_driver.cpp
TEST_F(DriverIntegrationTest, M72_R08_DriverBufferOverflowReturnsStructuredError) {
    // 启动大输出桩进程（长行/无换行）
    // 断言 task 最终为 error，错误码和 message 可判定
}

// src/tests/test_process_async_binding.cpp
TEST_F(JsProcessAsyncTest, M72_R10_ExecAsyncRejectsWhenStdoutOverLimit) {
    // execAsync 执行大输出桩进程
    // 断言 Promise 被 reject，错误包含 channel 与 limit
}
```

```cpp
// src/tests/test_process_guard.cpp
TEST(ProcessGuard, M72_R13_RecoverFromStaleSocketName) {
    // 制造残留 guard 名称占用场景
    // 断言 removeServer + retry 后 start 成功
}

// src/tests/test_event_bus.cpp
TEST(EventBusTest, M72_R14_SseDisconnectedRemovesConnection) {
    // 建立 SSE 连接，触发回收路径（心跳探测/显式 close）
    // 断言 activeConnectionCount 递减，且不会出现悬空连接
}
```

```cpp
// src/tests/test_service_args.cpp
TEST_F(ServiceArgsTest, M72_R16_LoadConfigFileRejectsOversizedFile) {
    // 构造超大 JSON 文件
    // 断言 loadConfigFile 返回空对象和明确 error
}

TEST_F(ServiceArgsTest, M72_R17_LoadConfigFromStdinRejectsOversizedInput) {
    // stdin 注入超大 JSON
    // 断言返回超限错误
}
```

- Red 执行记录要求：
  - 每个 `M72_Rxx` 在修复前均提供一次失败日志摘要（命令 + 失败断言）
  - 失败日志附到开发记录（PR 描述或 `doc/milestone/` 附录）

### 4.2 Green — 最小修复实现

1. P0-1：提取 PATH 拼接工具函数并替换三处硬编码。
2. P0-2：写盘入口统一切换到 `QSaveFile`，校验 `write()` 长度与 `commit()`。
3. P0-3：重写 `readTailLines()` 为“尾部窗口正向读取”实现，引入总字节上限常量。
4. P0-4：三处缓冲加 8MB 硬上限，超限时执行“结构化错误 + 终止”。
5. P1-2：在两个进程监控 API handler 中接入平台支持判断，不支持返回 501。
6. P1-3：在保留 pre-listen probe 前提下，`AddressInUseError` 分支补 `removeServer + 一次重试`。
7. P1-4：接入 `disconnected()` 回收链路，心跳探测下清理死连接，并统一回收析构策略。
8. P2-1：统一配置 `kMaxBodySize` / `kMaxConfigBytes`，并完成错误码映射。

- Green 验证：
  - `M72_R01 ~ M72_R17` 全部由 Red 变为 Green
  - 新增 Green 成功路径用例：
    - `M72_G04`：`saveProject` 正常写入并 `commit()` 成功
    - `M72_G05`：`writeTextFile` 正常写入并 `commit()` 成功
    - `M72_G11`：支持平台下 `process-tree/resources` 保持 `200` 与原响应结构
  - 既有相关测试套件无回归（`test_api_router.cpp`、`test_host_driver.cpp`、`test_process_async_binding.cpp`、`test_process_guard.cpp` 等）

#### Green 用例断言补充

- `M72_G04` 断言：
  - 返回 `true`，`error` 为空
  - 目标文件存在且 JSON 可解析
  - 文件内容包含本次提交字段（如 `name/serviceId/config`）
- `M72_G05` 断言：
  - 返回 `true`
  - 目标文本与输入 `content` 完全一致（字节级）
  - 不产生临时残留文件
- `M72_G11` 断言：
  - `process-tree` / `resources` 返回码为 `200`
  - 响应体含既有关键字段：`tree/summary`（process-tree）与 `processes/summary`（resources）
  - 响应体不包含 `code=PROCESS_MONITOR_UNSUPPORTED`

### 4.3 Refactor — 重构（按需）

- 抽取公共常量与错误构造函数，减少重复字符串。
- 整理“大小上限”与“平台支持判断”到可复用 helper，避免后续分叉。
- 保持外部接口不变；重构后全量测试仍需通过。

---

## 5. 文件变更清单

### 5.1 新增文件

- `src/stdiolink_server/utils/process_env_utils.h` - PATH 拼接工具声明（P0-1）
- `src/stdiolink_server/utils/process_env_utils.cpp` - PATH 拼接工具实现（P0-1）
- `src/tests/test_output_flood_stub_main.cpp` - 大输出/长行桩进程（P0-4 测试）

### 5.2 修改文件

- `src/stdiolink_server/manager/instance_manager.cpp` - 使用统一 PATH 拼接
- `src/stdiolink_server/scanner/driver_manager_scanner.cpp` - 使用统一 PATH 拼接
- `src/stdiolink_server/http/driverlab_ws_connection.cpp` - 使用统一 PATH 拼接 + 输出上限
- `src/stdiolink_server/http/driverlab_ws_connection.h` - 输出上限常量与错误辅助
- `src/stdiolink_server/manager/project_manager.cpp` - `saveProject()` 改原子写
- `src/stdiolink_server/server_manager.cpp` - `writeTextFile()` 改原子写
- `src/stdiolink_server/http/api_router.cpp` - body 上限、tail 窗口正向读取、ProcessMonitor 501 分支
- `src/stdiolink_server/http/event_stream_handler.h` - 断开信号接入声明
- `src/stdiolink_server/http/event_stream_handler.cpp` - 断开回收逻辑
- `src/stdiolink_server/manager/process_monitor.h` - 平台支持判断接口（如 `isSupported()`）
- `src/stdiolink_server/manager/process_monitor.cpp` - `isSupported()` 实现
- `src/stdiolink/guard/process_guard_server.cpp` - 残留 socket 清理 + 重试
- `src/stdiolink/host/driver.cpp` - stdout 缓冲上限与超限错误
- `src/stdiolink_service/bindings/js_process_async.cpp` - execAsync 缓冲上限与 reject
- `src/stdiolink_service/config/service_args.h` - 配置文件大小限制常量/声明
- `src/stdiolink_service/config/service_args.cpp` - 配置文件大小前置校验
- `src/stdiolink_server/CMakeLists.txt` - 新增 `utils/process_env_utils.cpp`
- `src/tests/CMakeLists.txt` - 新增桩目标 `test_output_flood_stub`，并 `add_dependencies(stdiolink_tests test_output_flood_stub)`

### 5.3 测试文件

- `src/tests/test_instance_manager.cpp` - P0-1 路径分隔符回归
- `src/tests/test_driver_manager_scanner.cpp` - P0-1 路径分隔符回归
- `src/tests/test_driverlab_ws_handler.cpp` - P0-1 / P0-4 集成回归
- `src/tests/test_project_manager.cpp` - P0-2 原子写回归
- `src/tests/test_server_manager.cpp` - P0-2 服务创建原子写回归
- `src/tests/test_api_router.cpp` - P0-3 / P1-2 / P2-1 API 行为回归
- `src/tests/test_host_driver.cpp` - P0-4 Host 缓冲上限
- `src/tests/test_process_async_binding.cpp` - P0-4 execAsync 缓冲上限
- `src/tests/test_process_guard.cpp` - P1-3 残留 socket 恢复
- `src/tests/test_event_bus.cpp` - P1-4 SSE 断开回收
- `src/tests/test_service_args.cpp` - P2-1 config 大小上限

---

## 6. 测试与验收

### 6.1 单元测试（必填，重点）

- 测试对象：
  - PATH 组装函数、原子写盘函数、tail 读取函数、输出缓冲路径、API 错误分支、guard 恢复、SSE 回收、配置大小校验。
- 用例分层：
  - 正常路径：现有行为不变（兼容）
  - 边界值：`maxBodySize-1`、`==`、`+1`；输出缓冲阈值前后
  - 异常输入：损坏 JSON、超大输入、残留 socket
  - 回归路径：8 个缺陷均有唯一 `M72_Rxx`
- 桩替身策略：
  - 进程相关使用真实 `QProcess` + 测试桩可执行程序
  - 网络相关使用 `QHttpServer + QNetworkAccessManager`
  - 文件 IO 使用 `QTemporaryDir` 与可控失败注入点

#### 路径矩阵（可达分支全覆盖）

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| PATH 拼接 | 原 PATH 为空 | `M72_R01` |
| PATH 拼接 | 原 PATH 非空，使用平台分隔符 | `M72_R01` `M72_R02` `M72_R03` |
| saveProject | 写成功 + commit 成功 | `M72_G04` |
| saveProject | 写失败/commit 失败，旧文件保留 | `M72_R04` |
| writeTextFile | 正常写入 | `M72_G05` |
| writeTextFile | 写失败不落半文件 | `M72_R05` |
| readTailLines | 小文件正常 | 既有 `ApiRouterTest.ProjectLogsEndpoint` |
| readTailLines | 大文件 + 尾部窗口截断 + maxLines | `M72_R06` |
| readTailLines | 超长行 + 首行裁剪 + 读取上限生效 | `M72_R07` |
| Driver 缓冲 | 未超限正常解析 | 既有 `DriverIntegrationTest.EchoCommand` |
| Driver 缓冲 | 超限 -> 结构化错误终止 | `M72_R08` |
| DriverLab 缓冲 | 未超限正常透传 | 既有 `DriverLabWsHandlerTest.DriverStartedMessageReceived` |
| DriverLab 缓冲 | 超限 -> error + close | `M72_R09` |
| execAsync 缓冲 | 未超限 resolve | 既有 `ExecAsyncResolvesOnExitCodeZero` |
| execAsync 缓冲 | 超限 reject | `M72_R10` |
| ProcessMonitor API | 支持平台 200 | 既有/新增 `M72_G11` |
| ProcessMonitor API | 不支持平台 501 | `M72_R11` `M72_R12` |
| guard.start | 正常 listen | 既有 `T01_ServerListenSuccess` |
| guard.start | pre-probe 不可连 + AddressInUse -> remove + retry 成功 | `M72_R13` |
| SSE 回收 | 主动 closeAll/evict 回收 | 既有路径 |
| SSE 回收 | 心跳探测窗口内触发 disconnected 回收 | `M72_R14` |
| SSE 回收 | closeAll/removeConnection 析构策略一致 | `M72_R14` |
| parseJsonObjectBody | 空 body 正常 | 既有路径 |
| parseJsonObjectBody | body 超限 413 | `M72_R15` |
| loadConfigFile | 文件 <= 上限 | 既有 `LoadConfigFileValid` |
| loadConfigFile | 文件 > 上限报错 | `M72_R16` |
| loadConfigFile(stdin) | 输入 > 上限报错 | `M72_R17` |

- 不可达路径说明：
  - 无（本里程碑声明的分支均可通过单元/集成测试触达）

### 6.2 集成测试（按需）

- HTTP 端到端：
  - 启动最小 `QHttpServer`，验证 `413/501` 状态码和响应体字段。
- 进程链路：
  - 使用 `test_output_flood_stub` 分别驱动 Host/DriverLab/execAsync 三条缓冲上限分支。
- guard 恢复：
  - 通过同名 socket 冲突 + 残留清理复现 `AddressInUseError`。

### 6.3 验收标准

- [ ] 8 个缺陷均有 Red 失败记录与对应 Green 通过记录
- [ ] 所有 `M72_Rxx` 测试在修复后通过
- [ ] 相关既有测试无回归
- [ ] `process-tree/resources` 在非支持平台返回 501，且含 `code=PROCESS_MONITOR_UNSUPPORTED` 与 `supported=false`
- [ ] 超大 HTTP body 返回 413；超大 config 文件返回明确错误
- [ ] 输出缓冲超限时返回结构化错误且子进程被回收

---

## 7. 风险与控制

- 风险：tail 窗口读取在首行裁剪时处理不当，可能出现行丢失/重复  
  控制：先写边界测试（空文件、单行无换行、CRLF、多字节 UTF-8、窗口起点在多字节中间），再实现。

- 风险：统一上限后可能影响已有超大输出合法场景  
  控制：错误信息明确提示阈值；阈值集中定义，后续可配置化。

- 风险：ProcessMonitor 501 可能影响调用方兼容  
  控制：响应体显式返回 `supported=false`，前端按能力降级。

- 风险：Qt 未暴露直接断开回调，SSE 死连接检测不是实时的  
  控制：以心跳窗口为探测周期，确保最坏情况下连接泄漏时间有上界；同时保留连接上限驱逐。

- 风险：QSaveFile 在不同文件系统行为差异  
  控制：写入后统一检查 `write` 字节数与 `commit` 返回值，失败立即上抛。

---

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] 文档同步完成（含 API 行为变化）
- [ ] 向后兼容与行为变更评审完成
- [ ] 【问题修复类】Red 阶段失败测试有执行记录
- [ ] 【问题修复类】Green 阶段全量测试通过（新增 + 既有无回归）
- [ ] 【问题修复类】Refactor 阶段（若有）全量测试仍通过
