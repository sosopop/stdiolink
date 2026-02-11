# API Todo List

本文档记录 `stdiolink_server` 后续拟实现/增强的 API，按优先级与主题分类。  
原则：以当前代码架构为准，优先低侵入、可测试、可回滚的增量改动。

---

## P0（高优先级，近期实现）

### Project 级日志

- `GET /api/projects/{id}/logs?lines=N`
  - 明确项目级日志入口（日志文件本身按 `logs/{projectId}.log` 存储）
  - 解决 Instance 退出后从内存移除、无法通过 `GET /api/instances/{id}/logs` 查看历史日志的问题
  - 与现有 Instance 日志端点并存

### Instance 查询完善

- `GET /api/instances/{id}`
  - 返回单个 Instance 的详细信息（pid、status、startedAt、projectId、serviceId）
  - 当前 `start` 返回 `instanceId` 后只能通过 list 全量查询再过滤

### Project 列表查询能力

- `GET /api/projects?serviceId=&status=&enabled=&page=&pageSize=`
  - 支持后端过滤与分页，减少 dashboard 前端拉全量后本地筛选

### Project 启用开关

- `PATCH /api/projects/{id}/enabled`
  - 仅更新启停状态（`enabled`），避免前端做全量 PUT 覆盖
  - 与 `start/stop` 的“运行态操作”形成互补：一个管配置启用，一个管即时运行

### Server 状态

- `GET /api/server/status`
  - 合并健康检查与系统信息为单一端点
  - 返回：`ok`、`version`、`uptimeMs`、`host`、`port`、`dataRoot`、`serviceProgram`
  - 返回计数：`serviceCount`、`projectCount`（valid/invalid）、`instanceCount`、`driverCount`
  - 用于容器探针、监控接入、负载均衡探活

### Driver 进程提前退出的快速失败（M48 复盘项，范围已收敛）

- 已修复：
  - `js_driver.cpp:154` — `jsDriverRequest()` 发送前 `isRunning()` 检查（主路径快速失败）
  - `task.cpp` — `waitNext` 的 `quitIfReady` 增加进程退出检测 + `forceTerminal`；建立信号连接后增加 pre-check
  - `wait_any.cpp` — `waitAnyNext` 的 `quitIfReady` 增加同等逻辑 + 连接后 pre-check
- 已完成收口：
  - `forceTerminal` 错误信息已带上下文（`program`、`exitCode`、`exitStatus`）
  - 新增专用回归测试覆盖 `waitNext/waitAnyNext` 等待期间 driver 提前退出
  - `Driver::request()` 发送路径从固定 `1000ms` 阻塞改为短时 best-effort 刷新，并在发送失败/发送中退出时立即产出错误终态

---

## P1（中优先级，增强管理效率）

### Project 运行态批量查询

- `GET /api/projects/runtime`
  - 返回所有项目或指定项目集合的 runtime 信息（与单项目 runtime 同结构）
  - 解决 dashboard 列表页对 `/api/projects/{id}/runtime` 的 N+1 请求

### Project 批量重载

- `POST /api/projects/reload-all`
  - 批量从文件重新加载所有 Project 配置（可选按 `serviceId` 过滤）
  - 适用于批量修改 `projects/` 目录下文件后的场景

### Project 批量重验

- `POST /api/projects/revalidate`
  - 批量重验项目配置（可选按 `serviceId` 过滤）
  - 适用于 Service schema 更新后的集中校验

### Driver 详情

- `GET /api/drivers/{id}`
  - 返回 Driver 详情：`program`、`metaHash`、完整 meta 摘要等

### 实时事件流

- `GET /api/events/stream`（SSE）或 WebSocket
  - 推送 `instanceStarted`、`instanceFinished`、`projectInvalidated`、`serviceScanned` 等事件
  - 降低 dashboard 轮询频率，改善实时性

---

## P2（低优先级，运维与扩展）

### 系统信息

- `GET /api/system/info`
  - 返回运行路径、配置摘要、监听地址、serviceProgram 解析结果等

### 指标导出

- `GET /api/metrics`
  - 预留 Prometheus 风格指标
  - 包括实例数、项目有效性统计、调度触发统计等

---

## 暂缓项（当前不建议）

- 通过 HTTP API 直接创建/删除 Service 目录
  - 与文件系统作为配置源的当前模型冲突
  - 易引入权限、安全与一致性复杂度
