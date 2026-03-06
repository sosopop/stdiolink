# 里程碑 40：Service 手动重扫与 Project 运行态 API

> **前置条件**: 里程碑 34–39 已完成
> **目标**: 补齐两个高价值运维接口：`POST /api/services/scan` 与 `GET /api/projects/{id}/runtime`

---

## 1. 目标

- 新增 Service 手动扫描接口，支持运行时刷新 `services/` 目录
- 在 Service 重扫后，按选项触发 Project 重新校验和调度重建
- 新增 Project 运行态接口，暴露实例运行、调度器状态与 daemon 相关运行信息
- 保持现有 API 行为兼容，不破坏 M38 已交付接口

---

## 2. 背景与问题

当前 `stdiolink_server` 在启动阶段自动扫描 Service，但缺少运行时手动触发入口；而 Driver 已有 `POST /api/drivers/scan`。这会导致：

- 新增/修改 Service 后必须重启 server 才能生效
- Service schema 变化后，Project 的有效性状态与调度状态缺少统一刷新路径
- 缺少项目级运行态观测接口（尤其 daemon 的 restart 抑制状态）

---

## 3. 技术要点

### 3.1 `POST /api/services/scan`

请求体（可选）：

```json
{
  "revalidateProjects": true,
  "restartScheduling": true,
  "stopInvalidProjects": false
}
```

字段语义：

- `revalidateProjects`：是否用最新 Service 集重新校验全部 Project（默认 `true`）
- `restartScheduling`：是否重建调度器（默认 `true`）
- `stopInvalidProjects`：项目被重验为无效时是否立即停止实例（默认 `false`）

响应体：

```json
{
  "scannedDirs": 2,
  "loadedServices": 2,
  "failedServices": 0,
  "added": 1,
  "removed": 0,
  "updated": 0,
  "unchanged": 1,
  "revalidatedProjects": 3,
  "becameValid": 0,
  "becameInvalid": 1,
  "remainedInvalid": 0,
  "schedulingRestarted": true,
  "invalidProjects": ["p2"]
}
```

### 3.2 `GET /api/projects/{id}/runtime`

响应体示例：

```json
{
  "id": "p1",
  "enabled": true,
  "valid": true,
  "status": "running",
  "runningInstances": 1,
  "instances": [
    {
      "id": "inst_xxx",
      "projectId": "p1",
      "serviceId": "demo",
      "pid": 12345,
      "startedAt": "2026-02-11T10:00:00Z",
      "status": "running"
    }
  ],
  "schedule": {
    "type": "daemon",
    "timerActive": false,
    "restartSuppressed": false,
    "consecutiveFailures": 0,
    "shuttingDown": false,
    "autoRestarting": true
  }
}
```

说明：

- `timerActive`：仅 fixed_rate 常见为 `true`
- `restartSuppressed/consecutiveFailures`：主要用于 daemon 运行诊断
- `autoRestarting`：是否处于“daemon 可自动拉起”状态

---

## 4. 实现方案

### 4.1 `ScheduleEngine` 暴露运行态快照

新增 `ProjectRuntimeState` 与只读方法：

- `projectRuntimeState(projectId)`

返回字段：

- `shuttingDown`
- `restartSuppressed`
- `timerActive`
- `consecutiveFailures`

### 4.2 `ServerManager` 新增 Service 重扫编排

新增：

- `ServiceRescanStats`
- `rescanServices(revalidateProjects, restartScheduling, stopInvalidProjects)`

执行流程：

1. 扫描 `services/` 并替换内存服务目录
2. 计算 added/removed/updated/unchanged
3. 可选：重验全部 Project（更新 `valid/error/config`）
4. 可选：对无效项目停止调度并终止实例
5. 可选：重建调度器（`startAll`）

### 4.3 `ApiRouter` 增路由与处理器

新增：

- `POST /api/services/scan` → `handleServiceScan`
- `GET /api/projects/<arg>/runtime` → `handleProjectRuntime`

错误处理约定：

- 请求体字段类型错误：`400 Bad Request`
- 不存在的项目：`404 Not Found`

---

## 5. 文件变更清单

### 5.1 核心实现

- 修改 `src/stdiolink_server/manager/schedule_engine.h`
- 修改 `src/stdiolink_server/manager/schedule_engine.cpp`
- 修改 `src/stdiolink_server/server_manager.h`
- 修改 `src/stdiolink_server/server_manager.cpp`
- 修改 `src/stdiolink_server/http/api_router.h`
- 修改 `src/stdiolink_server/http/api_router.cpp`

### 5.2 测试

- 修改 `src/tests/test_api_router.cpp`
- 修改 `src/tests/test_server_manager.cpp`

---

## 6. 测试与验收

### 6.1 单元测试场景

- Service 扫描后可发现启动后新增的 Service
- Service 扫描返回统计字段与项目重验结果
- Project runtime 返回 schedule 与 instance 运行态
- runtime 查询不存在项目返回 404
- Service 被移除后，Project 重验变为 invalid

### 6.2 验收标准

- 两个新接口可用且响应字段完整
- 现有 API 行为无破坏（兼容 M38）
- 相关单测通过，且无编译回归

---

## 7. 风险与控制

- **风险 1**：服务重扫后调度状态与运行实例不一致
  - 控制：重扫接口提供 `restartScheduling/stopInvalidProjects` 两个开关，默认保守策略
- **风险 2**：运行态接口暴露不足导致排障信息缺失
  - 控制：返回实例列表 + 调度状态快照，覆盖手动/fixed_rate/daemon 三类诊断
- **风险 3**：接口与已有路由冲突
  - 控制：路径与方法均为增量扩展，不复用已有 handler

---

## 8. 里程碑完成定义（DoD）

- `POST /api/services/scan` 实现并可通过测试验证
- `GET /api/projects/{id}/runtime` 实现并可通过测试验证
- 对应单元测试完成并通过
- 本里程碑文档入库
