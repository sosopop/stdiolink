# 里程碑 67：SSE 事件流集成与全局实时更新

> **前置条件**: 里程碑 61-66 已完成（各模块页面已就绪），里程碑 59 已完成（EventStream SSE 客户端已实现）
> **目标**: 将 SSE 事件流集成到全局状态管理中，实现跨页面实时数据更新：Instance 启停事件驱动列表刷新、调度事件驱动 Dashboard 更新，并提供连接状态指示器；对后端暂未发布的事件类型保留扩展处理分支

---

## 1. 目标

- 实现全局 SSE 连接管理：应用启动时建立 SSE 连接，全生命周期维持
- 实现事件分发机制：SSE 事件按类型分发到对应的 Zustand Store
- 实现连接状态指示器：Header 区域显示 SSE 连接状态（Live / Offline / Reconnecting）
- 实现自动重连：断开后指数退避重连（1s → 2s → 4s → 8s → 16s → 30s 上限）
- 实现各模块的事件响应：
  - `instance.started` / `instance.finished` → 刷新 Instances 列表、更新 Dashboard 计数
  - `schedule.triggered` / `schedule.suppressed` → 更新 Dashboard 事件流
- 预留可选扩展分支（后端补充事件后启用）：
  - `project.status_changed` → 刷新 Projects 状态
  - `service.scanned` / `driver.scanned` → 刷新对应列表
- 实现 Zustand Store：`useEventStreamStore`
- 降低各页面轮询频率（SSE 可用时减少主动轮询）

---

## 2. 背景与问题

当前各模块页面依赖定时轮询获取最新数据（Dashboard 30s、Instance 详情 5s 等）。SSE 事件流可以实现服务端主动推送，减少不必要的轮询请求，提升实时性。M59 已实现 `EventStream` SSE 客户端类，本里程碑将其集成到全局状态管理中。

**范围**：SSE 全局连接管理 + 事件分发 + 各模块响应 + 连接状态 UI。

**非目标**：替换所有轮询（资源监控等高频数据仍使用轮询）。SSE 仅用于离散事件通知。

---

## 3. 技术要点

### 3.1 SSE 事件类型与响应

当前后端已实现的事件类型（见 `server_manager.cpp` 中 `EventBus::publish` 调用）：

| 事件类型 | 触发场景 | data 字段 | 前端响应 |
|---------|---------|----------|---------|
| `instance.started` | Instance 启动成功 | `{instanceId, projectId, pid}` | 刷新 Instances 列表；更新 Dashboard 计数；更新对应 Project 运行时状态 |
| `instance.finished` | Instance 退出 | `{instanceId, projectId, exitCode, status}` | 刷新 Instances 列表；更新 Dashboard 计数；更新对应 Project 运行时状态 |
| `schedule.triggered` | 调度引擎触发执行 | `{projectId, scheduleType}` | 追加到 Dashboard 事件流面板 |
| `schedule.suppressed` | 调度被抑制（连续失败等） | `{projectId, reason, consecutiveFailures}` | 追加到 Dashboard 事件流面板 |

**后端待补充的事件类型**（本里程碑前端预留处理逻辑，但需后端配合添加 `EventBus::publish` 调用）：

| 事件类型 | 触发场景 | 建议 data 字段 | 前端响应 |
|---------|---------|---------------|---------|
| `project.status_changed` | Project valid/invalid/enabled 状态变更 | `{projectId, status, enabled, valid}` | 刷新 Projects 列表中对应项的状态 |
| `service.scanned` | Service 目录扫描完成 | `{added, removed, updated}` | 刷新 Services 列表 |
| `driver.scanned` | Driver 目录扫描完成 | `{scanned, updated}` | 刷新 Drivers 列表 |

> **说明**：`project.status_changed`、`service.scanned`、`driver.scanned` 三个事件类型当前后端未发布。前端代码中预留这三个事件的处理分支，但在后端补充 `EventBus::publish` 调用之前，这些分支不会被触发。对应的列表刷新在 SSE 不可用时仍通过轮询保障。后端补充这些事件的工作量较小（在 `ServerManager::rescanServices`、`ServerManager::rescanDrivers` 和项目状态变更处各加一行 `publish` 调用即可），建议在本里程碑实施时一并完成。

### 3.2 全局连接管理

```typescript
// src/hooks/useGlobalEventStream.ts
// 在 App 根组件中调用，建立全局 SSE 连接

function useGlobalEventStream(): void {
  const { connect, disconnect, status } = useEventStreamStore();

  useEffect(() => {
    connect();
    return () => disconnect();
  }, []);

  // 页面可见性管理：不可见时断开，可见时重连
  useEffect(() => {
    const handleVisibility = () => {
      if (document.hidden) {
        disconnect();
      } else {
        connect();
      }
    };
    document.addEventListener('visibilitychange', handleVisibility);
    return () => document.removeEventListener('visibilitychange', handleVisibility);
  }, []);
}
```

### 3.3 自动重连策略

```typescript
interface ReconnectConfig {
  initialDelayMs: 1000;
  maxDelayMs: 30000;
  backoffMultiplier: 2;
}

// 重连逻辑
// 第 1 次：1s 后重连
// 第 2 次：2s 后重连
// 第 3 次：4s 后重连
// ...
// 上限：30s 后重连
// 成功连接后重置计数器
```

### 3.4 事件分发机制

```typescript
// src/stores/useEventStreamStore.ts 中的事件分发
function dispatchEvent(event: ServerEvent): void {
  switch (event.type) {
    case 'instance.started':
    case 'instance.finished':
      // 通知 Instances Store 刷新
      useInstancesStore.getState().fetchInstances();
      // 通知 Dashboard Store 更新计数
      useDashboardStore.getState().fetchServerStatus();
      // 通知 Projects Store 更新运行时
      if (event.data.projectId) {
        useProjectsStore.getState().fetchRuntimes();
      }
      break;

    case 'project.status_changed':
      // 后端待补充此事件类型（见 §3.1 说明）
      useProjectsStore.getState().fetchProjects();
      break;

    case 'service.scanned':
      // 后端待补充此事件类型（见 §3.1 说明）
      useServicesStore.getState().fetchServices();
      break;

    case 'driver.scanned':
      // 后端待补充此事件类型（见 §3.1 说明）
      useDriversStore.getState().fetchDrivers();
      break;

    case 'schedule.triggered':
    case 'schedule.suppressed':
      useDashboardStore.getState().addEvent(event);
      break;
  }
}
```

### 3.5 连接状态指示器

Header 右侧显示 SSE 连接状态：

| 状态 | 显示 | 样式 |
|------|------|------|
| `connected` | 🟢 Live | 绿色呼吸灯 + "Live" 文字 |
| `disconnected` | 🔴 Offline | 红色静态点 + "Offline" 文字 |
| `reconnecting` | 🟡 Reconnecting | 橙色闪烁 + "Reconnecting" 文字 |
| `error` | 🔴 Error | 红色 + 错误提示 tooltip |

### 3.6 轮询频率优化

SSE 连接成功后，各页面降低轮询频率：

| 页面 | SSE 可用时 | SSE 不可用时 |
|------|-----------|-------------|
| Dashboard 统计 | 仅 SSE 事件驱动刷新 | 30s 轮询 |
| Instances 列表 | 仅 SSE 事件驱动刷新 | 30s 轮询 |
| Projects 列表 | 仅 SSE 事件驱动刷新 | 30s 轮询 |
| Instance 详情资源 | 5s 轮询（不变） | 5s 轮询 |

### 3.7 Zustand Store

```typescript
// src/stores/useEventStreamStore.ts
type SseStatus = 'disconnected' | 'connecting' | 'connected' | 'reconnecting' | 'error';

interface EventStreamState {
  status: SseStatus;
  reconnectAttempts: number;
  lastEventTime: number | null;
  recentEvents: ServerEvent[];  // 最近 50 条事件（调试用）
  error: string | null;

  connect: () => void;
  disconnect: () => void;
  getStatus: () => SseStatus;
}
```

---

## 4. 实现方案

### 4.1 组件树

```
App (根组件)
├── useGlobalEventStream() (Hook，建立全局 SSE)
├── AppLayout
│   ├── Header
│   │   └── SseStatusIndicator (连接状态指示器)
│   ├── Sidebar
│   └── Content (各页面)
```

### 4.2 SseStatusIndicator 组件

```typescript
// src/components/Common/SseStatusIndicator.tsx
interface SseStatusIndicatorProps {
  status: SseStatus;
  lastEventTime: number | null;
}
```

显示逻辑：
- `connected`：绿色圆点 + "Live"，hover 显示最后事件时间
- `disconnected`：红色圆点 + "Offline"
- `reconnecting`：橙色闪烁圆点 + "Reconnecting..."，hover 显示重连次数
- `error`：红色圆点 + "Error"，hover 显示错误信息

### 4.3 useSmartPolling Hook

```typescript
// src/hooks/useSmartPolling.ts
// 智能轮询：SSE 可用时降低频率或停止轮询

interface UseSmartPollingOptions {
  fetchFn: () => Promise<void>;
  intervalMs: number;              // SSE 不可用时的轮询间隔
  sseIntervalMs?: number;          // SSE 可用时的轮询间隔（undefined = 不轮询）
  enabled?: boolean;
}

function useSmartPolling(options: UseSmartPollingOptions): void {
  const sseStatus = useEventStreamStore(s => s.status);
  const interval = sseStatus === 'connected'
    ? options.sseIntervalMs
    : options.intervalMs;

  useEffect(() => {
    if (!options.enabled || interval === undefined) return;
    options.fetchFn();
    const timer = setInterval(options.fetchFn, interval);
    return () => clearInterval(timer);
  }, [interval, options.enabled]);
}
```

### 4.4 各模块 Store 适配

各模块 Store 无需修改接口，事件分发直接调用已有的 fetch 方法。仅需确保 fetch 方法支持被外部调用（已满足）。

Dashboard Store 需新增 `addEvent` 方法（如 M61 未包含）：

```typescript
// useDashboardStore 补充
addEvent: (event: ServerEvent) => void;
```

### 4.5 重连后数据同步策略

SSE 断开期间可能错过事件，重连成功后需要主动同步数据以保证一致性：

```typescript
// 重连成功后的数据同步
function onReconnected(): void {
  // 重新拉取所有关键数据，弥补断开期间可能错过的事件
  useDashboardStore.getState().fetchServerStatus();
  useDashboardStore.getState().fetchInstances();
  useInstancesStore.getState().fetchInstances();
  useProjectsStore.getState().fetchProjects();
}
```

同步规则：
- 重连成功（`reconnecting` → `connected`）后，立即触发一次全量数据拉取
- 仅拉取当前活跃页面相关的数据（通过检查路由或 Store 的 loading 状态判断）
- Dashboard 数据始终拉取（因为 KPI 卡片需要全局准确）
- 使用防抖避免重连抖动导致的重复请求

---

## 5. 文件变更清单

### 5.1 新增文件

**Hooks**：
- `src/webui/src/hooks/useGlobalEventStream.ts` — 全局 SSE 连接 Hook
- `src/webui/src/hooks/useSmartPolling.ts` — 智能轮询 Hook

**组件**：
- `src/webui/src/components/Common/SseStatusIndicator.tsx` — SSE 连接状态指示器

**Store**：
- `src/webui/src/stores/useEventStreamStore.ts` — SSE 事件流 Store

**测试**：
- `src/webui/src/__tests__/hooks/useGlobalEventStream.test.ts`
- `src/webui/src/__tests__/hooks/useSmartPolling.test.ts`
- `src/webui/src/__tests__/components/SseStatusIndicator.test.tsx`
- `src/webui/src/__tests__/stores/useEventStreamStore.test.ts`

### 5.2 修改文件

- `src/webui/src/App.tsx` — 添加 `useGlobalEventStream()` 调用
- `src/webui/src/components/Layout/AppHeader.tsx` — 添加 `SseStatusIndicator`
- `src/webui/src/pages/Dashboard/index.tsx` — 使用 `useSmartPolling` 替换固定轮询
- `src/webui/src/pages/Instances/index.tsx` — 使用 `useSmartPolling` 替换固定轮询
- `src/webui/src/pages/Projects/index.tsx` — 使用 `useSmartPolling` 替换固定轮询
- `src/webui/src/stores/useDashboardStore.ts` — 添加 `addEvent` 方法（如未包含）

---

## 6. 测试与验收

### 6.1 单元测试场景

**useEventStreamStore（useEventStreamStore.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | `connect()` | 创建 EventSource，状态变为 connecting |
| 2 | SSE open | 状态变为 connected，reconnectAttempts 重置为 0 |
| 3 | 收到 `instance.started` 事件 | 调用 instancesStore.fetchInstances() 和 dashboardStore.fetchServerStatus() |
| 4 | 收到 `instance.finished` 事件 | 调用 instancesStore.fetchInstances() 和 projectsStore.fetchRuntimes() |
| 5 | 收到 `project.status_changed` 事件（可选扩展） | 调用 projectsStore.fetchProjects() |
| 6 | 收到 `service.scanned` 事件（可选扩展） | 调用 servicesStore.fetchServices() |
| 7 | 收到 `driver.scanned` 事件（可选扩展） | 调用 driversStore.fetchDrivers() |
| 8 | 收到 `schedule.triggered` 事件 | 调用 dashboardStore.addEvent() |
| 9 | SSE error | 状态变为 reconnecting |
| 10 | 自动重连成功 | 状态恢复为 connected |
| 11 | 重连退避 | 第 1 次 1s、第 2 次 2s、第 3 次 4s |
| 12 | 重连上限 | 延迟不超过 30s |
| 13 | `disconnect()` | 关闭 EventSource，状态变为 disconnected |
| 14 | recentEvents 上限 | 超过 50 条后旧事件被移除 |
| 15 | lastEventTime 更新 | 每次收到事件后更新时间戳 |

**SseStatusIndicator（SseStatusIndicator.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 16 | connected 状态 | 显示绿色圆点 + "Live" |
| 17 | disconnected 状态 | 显示红色圆点 + "Offline" |
| 18 | reconnecting 状态 | 显示橙色闪烁圆点 + "Reconnecting" |
| 19 | error 状态 | 显示红色圆点 + "Error" |
| 20 | hover 显示详情 | tooltip 显示最后事件时间或错误信息 |

**useSmartPolling（useSmartPolling.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 21 | SSE 不可用时 | 使用 intervalMs 轮询 |
| 22 | SSE 可用时（sseIntervalMs 设置） | 使用 sseIntervalMs 轮询 |
| 23 | SSE 可用时（sseIntervalMs 未设置） | 停止轮询 |
| 24 | SSE 状态切换 | 轮询间隔动态调整 |
| 25 | enabled=false | 不执行轮询 |
| 26 | 组件卸载 | 清除定时器 |

**useGlobalEventStream（useGlobalEventStream.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 27 | 组件挂载 | 调用 connect() |
| 28 | 组件卸载 | 调用 disconnect() |
| 29 | 页面不可见 | 调用 disconnect() |
| 30 | 页面重新可见 | 调用 connect() |

### 6.2 验收标准

- 应用启动后自动建立 SSE 连接
- Header 区域正确显示连接状态
- SSE 事件正确分发到对应 Store
- Instance 启停事件触发列表和 Dashboard 刷新
- Service/Driver 扫描事件分支可用（后端补充事件后可触发列表刷新）
- 断开后自动重连，退避策略正确
- 页面不可见时断开，可见时重连
- SSE 可用时轮询频率降低
- 全部单元测试通过

---

## 7. 风险与控制

- **风险 1**：SSE 事件触发大量并发 API 请求
  - 控制：事件分发使用防抖（同类事件 500ms 内仅触发一次 fetch）；fetch 方法内部有 loading 状态防止重复请求
- **风险 2**：SSE 连接频繁断开重连
  - 控制：指数退避策略限制重连频率；页面不可见时主动断开减少无效连接
- **风险 3**：跨 Store 调用导致循环依赖
  - 控制：事件分发函数在 useEventStreamStore 中集中管理，通过 `getState()` 直接调用其他 Store 的方法，避免 import 循环

---

## 8. 里程碑完成定义（DoD）

- SSE 全局连接管理完整实现
- 事件分发到各模块 Store 正常
- 连接状态指示器正确显示
- 自动重连与退避策略正常
- 智能轮询 Hook 正常工作
- 对应单元测试完成并通过
- 本里程碑文档入库
