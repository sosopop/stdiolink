# 里程碑 61：Dashboard 指挥中心页面

> **前置条件**: 里程碑 60 已完成（应用外壳、路由与设计系统已就绪）
> **目标**: 实现 Dashboard 页面，包含系统状态总览卡片（KPI HUD）、活跃实例列表、实时事件流面板，通过 SSE 实现实时数据更新

---

## 1. 目标

- 实现 Dashboard 页面整体布局（Bento Grid 风格）
- 实现 KPI 状态卡片组件（Services / Projects / Instances / Drivers 计数）
- 实现活跃实例列表组件（显示运行中 Instance 的状态、CPU、内存）
- 实现实时事件流面板（显示最近 N 条系统事件）
- 实现服务器状态指示器（uptime、版本、连接状态）
- 通过 `GET /api/server/status` 轮询获取状态数据
- 通过 SSE `GET /api/events/stream` 接收实时事件
- 实现 Zustand Store 管理 Dashboard 状态
- 数字变化时的滚动动画（Ticker Animation）
- 状态呼吸灯效果

---

## 2. 背景与问题

Dashboard 是用户进入 WebUI 后的首页，需要提供系统的"上帝视角"。设计文档 §5.1 定义了 Mission Control 风格的仪表盘，包含 KPI 卡片、实时遥测、活跃实例和事件流四个核心区域。

数据来源：
- `GET /api/server/status` — 系统状态总览（30s 轮询）
- `GET /api/instances` — 活跃实例列表
- `GET /api/events/stream` — SSE 实时事件流

---

## 3. 技术要点

### 3.1 页面布局（Bento Grid）

```
┌─────────────────────────────────────────────────────────┐
│  ┌─ KPI HUD ────────────────────────────────────────┐  │
│  │  [Services: 12]  [Projects: 08]  [Instances: 05]  │  │
│  │  [Drivers: 18]   [Uptime: 2d 5h]                  │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  ┌─ 活跃实例 ──────────────────┐  ┌─ 事件流 ──────────┐ │
│  │  ● proj-api     CPU: 12%    │  │  10:23 inst start  │ │
│  │  ● proj-worker  CPU: 45%    │  │  10:15 proj update │ │
│  │  ○ proj-sched   Stopped     │  │  09:58 svc scanned │ │
│  └──────────────────────────────┘  └────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

使用 CSS Grid 实现 Bento 布局：

```scss
.dashboard-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  grid-template-rows: auto 1fr;
  gap: 16px;

  .kpi-section { grid-column: 1 / -1; }
  .instances-section { grid-column: 1; }
  .events-section { grid-column: 2; }
}
```

### 3.2 KPI 状态卡片

每个卡片显示：
- 标题（如 "Services"）
- 数值（大号等宽字体）
- 状态指示（颜色编码）
- 数值变化动画

```typescript
interface KpiCardProps {
  title: string;
  value: number;
  icon: React.ReactNode;
  status?: 'normal' | 'warning' | 'error';
  subtitle?: string;
}
```

数值变化动画使用 CSS `counter` + `@keyframes` 或简单的 `requestAnimationFrame` 插值实现。

### 3.3 活跃实例列表

显示所有运行中的 Instance，每行包含：
- 状态指示灯（呼吸灯效果）
- Project 名称
- Service 名称
- CPU / 内存使用率
- 运行时长
- 快捷操作（终止）

数据来源：`GET /api/instances` + `GET /api/projects/runtime`

### 3.4 实时事件流

显示最近 50 条系统事件，通过 SSE 实时更新：

```typescript
interface EventItem {
  type: string;       // 'instance.started' | 'instance.finished' | ...
  data: Record<string, unknown>;
  timestamp: Date;
}
```

事件类型颜色编码：
- `instance.started` → 绿色
- `instance.finished` → 灰色（正常退出）/ 红色（崩溃）
- `schedule.triggered` → 蓝色
- `schedule.suppressed` → 橙色

### 3.5 Dashboard Store

```typescript
// src/webui/src/stores/useDashboardStore.ts
import { create } from 'zustand';
import type { ServerStatus, ServerEvent } from '@/types/server';
import type { Instance } from '@/types/instance';

interface DashboardState {
  serverStatus: ServerStatus | null;
  instances: Instance[];
  events: ServerEvent[];
  loading: boolean;
  error: string | null;
  connected: boolean;  // SSE 连接状态

  fetchServerStatus: () => Promise<void>;
  fetchInstances: () => Promise<void>;
  addEvent: (event: ServerEvent) => void;
  setConnected: (connected: boolean) => void;
}
```

### 3.6 SSE 集成 Hook

```typescript
// src/webui/src/hooks/useEventStream.ts
import { useEffect, useRef } from 'react';
import { EventStream } from '@/api/event-stream';
import type { ServerEvent } from '@/types/server';

export function useEventStream(
  filters: string[],
  onEvent: (event: ServerEvent) => void
) {
  const streamRef = useRef<EventStream | null>(null);

  useEffect(() => {
    const stream = new EventStream();
    streamRef.current = stream;

    stream.on('event', onEvent);
    stream.connect(filters);

    return () => {
      stream.close();
      streamRef.current = null;
    };
  }, [filters, onEvent]);
}
```

### 3.7 轮询 Hook

```typescript
// src/webui/src/hooks/usePolling.ts
import { useEffect, useRef } from 'react';

export function usePolling(callback: () => void, intervalMs: number) {
  const callbackRef = useRef(callback);
  callbackRef.current = callback;

  useEffect(() => {
    callbackRef.current();
    const timer = setInterval(() => callbackRef.current(), intervalMs);
    return () => clearInterval(timer);
  }, [intervalMs]);
}
```

### 3.8 呼吸灯 CSS

```scss
.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  display: inline-block;

  &.running {
    background: var(--color-success);
    animation: breathe 2s ease-in-out infinite;
  }

  &.stopped {
    background: rgba(255, 255, 255, 0.3);
  }

  &.error {
    background: var(--color-error);
    box-shadow: 0 0 8px rgba(255, 46, 84, 0.6);
  }
}

@keyframes breathe {
  0%, 100% { box-shadow: 0 0 4px rgba(0, 230, 118, 0.4); }
  50% { box-shadow: 0 0 12px rgba(0, 230, 118, 0.8); }
}
```

---

## 4. 文件变更清单

### 4.1 新增文件

- `src/webui/src/pages/Dashboard/index.tsx` — Dashboard 页面（替换占位）
- `src/webui/src/pages/Dashboard/components/KpiCards.tsx` — KPI 卡片组
- `src/webui/src/pages/Dashboard/components/KpiCard.tsx` — 单个 KPI 卡片
- `src/webui/src/pages/Dashboard/components/ActiveInstances.tsx` — 活跃实例列表
- `src/webui/src/pages/Dashboard/components/EventFeed.tsx` — 事件流面板
- `src/webui/src/pages/Dashboard/components/ServerIndicator.tsx` — 服务器状态指示
- `src/webui/src/pages/Dashboard/dashboard.module.scss` — Dashboard 样式
- `src/webui/src/stores/useDashboardStore.ts` — Dashboard 状态 Store
- `src/webui/src/hooks/useEventStream.ts` — SSE Hook
- `src/webui/src/hooks/usePolling.ts` — 轮询 Hook
- `src/webui/src/components/StatusDot/StatusDot.tsx` — 状态指示灯组件（可复用）
- `src/webui/src/components/StatusDot/status-dot.module.scss` — 状态灯样式

### 4.2 测试文件

- `src/webui/src/pages/Dashboard/__tests__/Dashboard.test.tsx`
- `src/webui/src/pages/Dashboard/__tests__/KpiCard.test.tsx`
- `src/webui/src/pages/Dashboard/__tests__/ActiveInstances.test.tsx`
- `src/webui/src/pages/Dashboard/__tests__/EventFeed.test.tsx`
- `src/webui/src/stores/__tests__/useDashboardStore.test.ts`
- `src/webui/src/hooks/__tests__/usePolling.test.ts`
- `src/webui/src/hooks/__tests__/useEventStream.test.ts`
- `src/webui/src/components/StatusDot/__tests__/StatusDot.test.tsx`

---

## 5. 测试与验收

### 5.1 单元测试场景

**Dashboard 页面（Dashboard.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 页面渲染 | KPI 卡片区、实例列表、事件流面板均存在 |
| 2 | 加载状态 | loading 时显示骨架屏 |
| 3 | 错误状态 | 请求失败时显示错误提示 |

**KpiCard（KpiCard.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 4 | 渲染标题和数值 | title 和 value 正确显示 |
| 5 | 状态颜色 | normal=绿色, warning=橙色, error=红色 |
| 6 | 数值为 0 | 正确显示 0 |

**ActiveInstances（ActiveInstances.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 7 | 渲染实例列表 | 每个实例显示 projectId、status、CPU |
| 8 | 空列表 | 显示"暂无运行中的实例" |
| 9 | 状态指示灯 | running 实例显示绿色呼吸灯 |
| 10 | 终止按钮 | 点击触发 terminate 回调 |

**EventFeed（EventFeed.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 11 | 渲染事件列表 | 每个事件显示类型、时间、数据摘要 |
| 12 | 空列表 | 显示"暂无事件" |
| 13 | 事件颜色编码 | instance.started=绿色, instance.finished(crash)=红色 |
| 14 | 最多显示 50 条 | 超出时移除最旧事件 |

**useDashboardStore（useDashboardStore.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 15 | fetchServerStatus 成功 | serverStatus 更新 |
| 16 | fetchServerStatus 失败 | error 设置 |
| 17 | fetchInstances 成功 | instances 更新 |
| 18 | addEvent | events 数组头部插入新事件 |
| 19 | addEvent 超过 50 条 | 截断为 50 条 |
| 20 | setConnected | connected 状态更新 |

**usePolling（usePolling.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 21 | 立即执行一次 | 挂载后立即调用 callback |
| 22 | 定时执行 | intervalMs 后再次调用 |
| 23 | 卸载时清理 | clearInterval 被调用 |

**useEventStream（useEventStream.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 24 | 挂载时连接 | EventStream.connect 被调用 |
| 25 | 卸载时断开 | EventStream.close 被调用 |
| 26 | 收到事件触发回调 | onEvent 被调用 |

**StatusDot（StatusDot.test.tsx）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 27 | status=running | 绿色 + breathe 动画类 |
| 28 | status=stopped | 灰色，无动画 |
| 29 | status=error | 红色 + glow 效果 |

### 5.2 验收标准

- Dashboard 页面正确渲染所有区域
- KPI 卡片显示正确的计数数据
- 活跃实例列表实时更新
- 事件流面板通过 SSE 接收实时事件
- 呼吸灯和数字动画效果正常
- 全部单元测试通过

---

## 6. 风险与控制

- **风险 1**：SSE 连接断开后未自动重连
  - 控制：EventSource 浏览器原生支持自动重连；在 Hook 中监听 error 事件并更新连接状态
- **风险 2**：高频事件导致渲染性能问题
  - 控制：事件列表限制 50 条；使用 `React.memo` 优化列表项渲染
- **风险 3**：后端未启动时 Dashboard 白屏
  - 控制：loading / error 状态处理；API 请求失败时显示降级 UI

---

## 7. 里程碑完成定义（DoD）

- Dashboard 页面实现（KPI + 实例列表 + 事件流）
- SSE 实时事件集成
- 轮询机制实现
- Dashboard Store 实现
- 呼吸灯和动画效果
- 对应单元测试完成并通过
- 本里程碑文档入库
