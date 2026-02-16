# stdiolink_server 阻塞问题优化方案

## 1. 背景

当前 `stdiolink_server` 采用单事件循环模型，HTTP 路由、调度器、WebSocket 连接管理都在主线程执行。  
这带来一个直接问题：部分请求路径存在阻塞等待（`waitForStarted` / `waitForFinished` / 同步扫描），会拉高尾延迟并影响其他请求响应。

目标是在不破坏现有主线程状态一致性的前提下，消除主线程可避免的阻塞，并把重任务异步化。

## 2. 已识别阻塞点

### 2.1 WebSocket Driver 启动/关闭阻塞
- `src/stdiolink_server/http/driverlab_ws_connection.cpp:68`
  - `waitForStarted(5000)`
- `src/stdiolink_server/http/driverlab_ws_connection.cpp:112`
  - `waitForFinished(1000)`

### 2.2 Instance 启动/终止阻塞
- `src/stdiolink_server/manager/instance_manager.cpp:169`
  - `waitForStarted(5000)`
- `src/stdiolink_server/manager/instance_manager.cpp:195`
  - `waitForFinished(1000)`

### 2.3 扫描流程同步阻塞
- `/api/drivers/scan` 调用链：
  - `src/stdiolink_server/http/api_router.cpp:1506`
  - `src/stdiolink_server/server_manager.cpp:255`
  - `src/stdiolink_server/scanner/driver_manager_scanner.cpp:97`（`waitForFinished(kExportTimeoutMs)`）
- `/api/services/scan` 调用链：
  - `src/stdiolink_server/http/api_router.cpp:430`
  - `src/stdiolink_server/server_manager.cpp:268`

## 3. 设计原则

1. 保持“主线程单写状态”原则：`m_services / m_projects / m_driverCatalog / 调度状态` 继续由主线程更新。
2. 后台线程仅做纯计算/扫描，不直接改主线程对象。
3. 先做低风险去阻塞，再做任务后台化，分阶段落地。
4. 每一步都有可回归的状态语义和测试用例。

## 4. 分阶段方案

## 4.1 第一阶段：消除可直接去掉的阻塞等待（低风险）

### A. DriverLabWsConnection 启动改信号驱动
- 目标文件：
  - `src/stdiolink_server/http/driverlab_ws_connection.cpp`
- 方案：
  - 去掉 `waitForStarted(5000)`。
  - 增加启动状态机：
    - 调用 `start()` 后进入 `starting`。
    - 监听 `started`：发送 `driver.started`。
    - 监听 `errorOccurred(FailedToStart)`：发送错误并关闭 WS。
    - 通过 `QTimer::singleShot(5000, ...)` 实现启动超时。

### B. InstanceManager 启动改异步确认
- 目标文件：
  - `src/stdiolink_server/manager/instance_manager.cpp`
  - `src/stdiolink_server/http/api_router.cpp`
- 方案：
  - 去掉 `waitForStarted(5000)`。
  - `startInstance()` 返回后实例先置 `starting`，并尽快插入实例表。
  - 通过 `QProcess::started/errorOccurred` 完成状态迁移：
    - started -> `running`，填充 pid，发 `instanceStarted`。
    - error/timeout -> 清理并发失败事件。
  - `/api/projects/<id>/start` 可返回：
    - `202 Accepted` + `{instanceId, status: "starting"}`
    - 或保持 `200` 但语义明确为“已提交启动”。

### C. 终止改“terminate + 超时 kill”
- 目标文件：
  - `src/stdiolink_server/manager/instance_manager.cpp`
  - `src/stdiolink_server/http/driverlab_ws_connection.cpp`
- 方案：
  - 去掉 `waitForFinished(...)`。
  - 改为：
    - 先 `terminate()`
    - 启动短超时计时器
    - 超时后若仍存活再 `kill()`
  - 整个过程不阻塞主线程。

## 4.2 第二阶段：扫描 API 后台任务化（中风险，高收益）

### A. 引入 Job 模块
- 新增建议：
  - `src/stdiolink_server/manager/job_manager.h/.cpp`
  - `src/stdiolink_server/model/job_info.h`
- 职责：
  - 接收重任务（driver scan / service scan）。
  - 在后台线程执行。
  - 维护任务状态（queued/running/succeeded/failed/canceled）。
  - 通过主线程回调提交结果。

### B. API 形态调整
- `POST /api/drivers/scan`：
  - 立即返回 `202` + `jobId`
- `POST /api/services/scan`：
  - 立即返回 `202` + `jobId`
- 新增：
  - `GET /api/jobs/<id>` 查询状态
  - 可选 `POST /api/jobs/<id>/cancel`

### C. 结果提交约束
- 后台线程只产出“扫描结果数据结构”，不直接写 `ServerManager` 内存状态。
- 在主线程统一提交：
  - `m_driverCatalog.replaceAll(...)`
  - `m_services = ...`
  - 必要时重启调度、终止无效实例。

## 4.3 第三阶段：可观测性与回归治理（中风险）

### A. 统一状态与错误码
- 启动相关状态：`starting/running/failed/stopped`
- 关键错误：`start_timeout`, `start_failed`, `terminate_timeout`, `scan_timeout`

### B. SSE 进度推送（可选）
- 复用现有 `/api/events/stream`
- 推送 job 进度事件：
  - `job.started`
  - `job.progress`
  - `job.finished`
  - `job.failed`

### C. 并发策略
- 同类扫描任务并发上限 1（后续再评估扩展）
- 任务去重策略：
  - 运行中重复提交可返回同一 `jobId` 或 `409`

## 5. 线程模型约束（必须遵守）

1. `QObject` 归属线程不跨线程直接调用。
2. `QProcess` 由所属线程创建并管理，不在其他线程直接读写其状态。
3. `ServerManager` 全局状态只在主线程改动。
4. 跨线程结果通过 `invokeMethod(..., Qt::QueuedConnection)` 或信号回主线程。

## 6. 兼容性策略

### 6.1 HTTP 响应兼容
- 扫描接口从“同步返回统计结果”改为“异步 job”，属于行为变化。
- 兼容窗口建议：
  - 保留旧参数含义
  - 在文档与前端同时切换后再移除同步路径

### 6.2 WebSocket 兼容
- 消息类型保持不变（`driver.started`, `driver.exited`, `error`）
- 仅调整触发时机为事件驱动，不影响消费者协议。

## 7. 测试计划

## 7.1 单元测试
- `InstanceManager`：
  - 启动成功异步转 `running`
  - 启动失败/超时路径
  - terminate->kill 回退路径
- `DriverLabWsConnection`：
  - 启动成功/失败/超时
  - 断开时非阻塞停止
- `JobManager`：
  - 状态流转
  - 取消
  - 超时

## 7.2 集成测试
- 并发请求下 API 响应时延验证（启动/扫描/日志接口混合）。
- 扫描任务执行期间其他 API 不被明显拖慢。
- shutdown 期间任务与进程的有序收敛。

## 8. 验收标准（DoD）

- [ ] 主线程路径不再出现 `waitForStarted`/`waitForFinished` 阻塞等待。
- [ ] `/api/drivers/scan` 与 `/api/services/scan` 支持 `202 + jobId` 异步模式。
- [ ] 全局状态写入仅发生在主线程，新增并发测试通过。
- [ ] 关键场景测试通过：启动成功/失败/超时、终止超时回退、扫描任务成功/失败/取消。
- [ ] 文档（HTTP API + 使用说明）同步更新。

## 9. 实施建议顺序

1. 第一阶段（去 wait 阻塞）  
2. 第二阶段（扫描 job 化）  
3. 第三阶段（观测性、并发治理、协议完善）  

此顺序可在最小风险下快速降低主线程阻塞，并为后续高并发能力扩展打基础。
