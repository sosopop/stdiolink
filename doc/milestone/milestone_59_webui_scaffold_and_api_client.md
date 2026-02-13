# 里程碑 59：WebUI 工程脚手架与 API 客户端层

> **前置条件**: 里程碑 58 已完成（后端静态文件服务已就绪）
> **目标**: 初始化 WebUI 前端工程，搭建 Vite + React + TypeScript 项目骨架，实现 API 客户端封装层和全部 TypeScript 类型定义

---

## 1. 目标

- 在 `src/webui/` 下初始化 Vite + React 18 + TypeScript 5 项目
- 配置开发工具链：ESLint、Prettier、Vitest
- 实现 Axios API 客户端封装（请求/响应拦截、错误处理、baseURL 配置）
- 实现全部 API 模块：`services`、`projects`、`instances`、`drivers`、`server`
- 实现 WebSocket 客户端封装（DriverLab）
- 实现 SSE EventSource 封装
- 定义全部 TypeScript 类型（与后端 API 响应对齐）
- 配置 Vite 开发代理（`/api` → `localhost:8080`）
- 单元测试覆盖 API 客户端层

---

## 2. 背景与问题

WebUI 需要一个稳定的前端工程基础和类型安全的 API 客户端层。API 客户端层是所有页面组件的基础依赖，需要先于 UI 组件开发完成。类型定义需与后端 API 响应严格对齐，确保前后端数据契约一致。

---

## 3. 技术要点

### 3.1 项目初始化

```bash
# 在 src/webui/ 下初始化
npm create vite@latest . -- --template react-ts
npm install
```

核心依赖：

| 包 | 版本 | 用途 |
|---|------|------|
| `react` | ^18.3 | UI 框架 |
| `react-dom` | ^18.3 | DOM 渲染 |
| `react-router-dom` | ^6.x | 客户端路由 |
| `axios` | ^1.x | HTTP 客户端 |
| `zustand` | ^4.x | 状态管理 |
| `typescript` | ^5.x | 类型系统 |

开发依赖：

| 包 | 用途 |
|---|------|
| `vitest` | 单元测试 |
| `@testing-library/react` | 组件测试 |
| `eslint` + `@typescript-eslint/*` | 代码检查 |
| `prettier` | 代码格式化 |
| `msw` | API Mock（测试用） |

### 3.2 Vite 配置

```typescript
// src/webui/vite.config.ts
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import path from 'path';

export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src')
    }
  },
  server: {
    port: 3000,
    proxy: {
      '/api': {
        target: 'http://localhost:8080',
        changeOrigin: true,
        ws: true  // WebSocket 代理
      }
    }
  },
  build: {
    outDir: 'dist',
    sourcemap: false
  },
  test: {
    globals: true,
    environment: 'jsdom',
    setupFiles: './src/test-setup.ts'
  }
});
```

### 3.3 TypeScript 类型定义

```typescript
// src/webui/src/types/api.ts
// 通用响应包装类型（与后端 API 响应结构对齐）
export interface PaginatedResponse<T> {
  [key: string]: T[] | number;  // 动态 key（如 projects/services）
  total: number;
  page: number;
  pageSize: number;
}

export interface ListResponse<T> {
  [key: string]: T[];  // 动态 key（如 services/instances/drivers）
}
```

```typescript
// src/webui/src/types/service.ts
export interface ServiceInfo {
  id: string;
  name: string;
  version: string;
  serviceDir: string;       // 后端字段名为 serviceDir（非 directory）
  hasSchema: boolean;        // 后端字段名为 hasSchema（非 hasConfigSchema）
  projectCount: number;
}

export interface ServiceDetail extends ServiceInfo {
  manifest: ServiceManifest;
  configSchema: Record<string, unknown>;
  configSchemaFields: FieldMeta[];
  projects: string[];        // 关联的 project ID 列表
}

export interface ServiceManifest {
  manifestVersion: string;
  id: string;
  name: string;
  version: string;
  description?: string;
  author?: string;
}

export interface ServiceFile {
  name: string;
  path: string;
  size: number;
  type: string;
  modifiedAt: string;
}

export interface CreateServiceRequest {
  id: string;
  name: string;
  version: string;
  description?: string;
  author?: string;
  template?: 'empty' | 'basic' | 'driver_demo';
  indexJs?: string;
  configSchema?: Record<string, unknown>;
}
```

```typescript
// src/webui/src/types/project.ts
export interface Project {
  id: string;
  name: string;
  serviceId: string;
  enabled: boolean;
  valid: boolean;
  error?: string;
  config: Record<string, unknown>;
  schedule: Schedule;
}

export type ScheduleType = 'manual' | 'fixed_rate' | 'daemon';

export interface Schedule {
  type: ScheduleType;
  intervalMs?: number;
  maxConcurrent?: number;
  restartDelayMs?: number;
  maxConsecutiveFailures?: number;
}

export interface ProjectRuntime {
  id: string;
  enabled: boolean;
  valid: boolean;
  error?: string;
  status: 'running' | 'stopped' | 'disabled' | 'invalid';
  runningInstances: number;
  instances: Instance[];
  schedule: {
    type: string;
    timerActive: boolean;
    restartSuppressed: boolean;
    consecutiveFailures: number;
    shuttingDown: boolean;
    autoRestarting: boolean;
  };
}

export interface CreateProjectRequest {
  id: string;
  name: string;
  serviceId: string;
  enabled?: boolean;
  config?: Record<string, unknown>;
  schedule?: Schedule;
}

export interface UpdateProjectRequest {
  name?: string;
  enabled?: boolean;
  config?: Record<string, unknown>;
  schedule?: Schedule;
}
```

```typescript
// src/webui/src/types/instance.ts
export interface Instance {
  id: string;
  projectId: string;
  serviceId: string;
  pid: number;
  startedAt: string;
  status: string;
  workingDirectory: string;
  logPath: string;
  commandLine: string[];
}

export interface ProcessTreeNode {
  pid: number;
  name: string;
  commandLine: string;
  status: string;
  startedAt?: string;
  resources: ProcessResources;
  children: ProcessTreeNode[];
}

export interface ProcessResources {
  cpuPercent: number;
  memoryRssBytes: number;
  memoryVmsBytes?: number;
  threadCount: number;
  uptimeSeconds?: number;
  ioReadBytes?: number;
  ioWriteBytes?: number;
}

export interface ProcessTreeResponse {
  instanceId: string;
  rootPid: number;
  tree: ProcessTreeNode;
  summary: ProcessTreeSummary;
}

export interface ProcessTreeSummary {
  totalProcesses: number;
  totalCpuPercent: number;
  totalMemoryRssBytes: number;
  totalThreads: number;
}

export interface ResourcesResponse {
  instanceId: string;
  timestamp: string;
  processes: ProcessInfo[];
  summary: ProcessTreeSummary;
}

export interface ProcessInfo {
  pid: number;
  name: string;
  cpuPercent: number;
  memoryRssBytes: number;
  threadCount: number;
  uptimeSeconds: number;
  ioReadBytes: number;
  ioWriteBytes: number;
}
```

```typescript
// src/webui/src/types/driver.ts
export interface DriverInfo {
  id: string;
  program: string;
  metaHash: string;
  meta?: DriverMeta;
}

export interface DriverMeta {
  schemaVersion: string;
  info: DriverMetaInfo;
  config?: FieldMeta;
  commands: CommandMeta[];
  types?: Record<string, unknown>;
  errors?: unknown[];
  examples?: unknown[];
}

export interface DriverMetaInfo {
  name: string;
  version: string;
  description?: string;
  vendor?: string;
  capabilities?: string[];
  profiles?: string[];
}

export interface CommandMeta {
  name: string;
  description?: string;
  params: FieldMeta[];
  result?: FieldMeta;
  events?: FieldMeta[];
}

export interface FieldMeta {
  name: string;
  type: string;
  description?: string;
  required?: boolean;
  defaultValue?: unknown;
  constraints?: Record<string, unknown>;
  ui?: Record<string, unknown>;
  fields?: FieldMeta[];
  items?: FieldMeta;
  enumValues?: string[];
}
```

```typescript
// src/webui/src/types/server.ts
export interface ServerStatus {
  status: string;
  version: string;
  uptimeMs: number;
  startedAt: string;
  host: string;
  port: number;
  dataRoot: string;
  serviceProgram: string;
  counts: {
    services: number;
    projects: {
      total: number;
      valid: number;
      invalid: number;
      enabled: number;
      disabled: number;
    };
    instances: {
      total: number;
      running: number;
    };
    drivers: number;
  };
  system: {
    platform: string;
    cpuCores: number;
    totalMemoryBytes?: number;
  };
}

export interface ServerEvent {
  type: string;
  data: Record<string, unknown>;
}
```

### 3.4 API 客户端封装

```typescript
// src/webui/src/api/client.ts
import axios, { AxiosError, AxiosInstance } from 'axios';

export interface ApiError {
  error: string;
  status: number;
}

const apiClient: AxiosInstance = axios.create({
  baseURL: '/api',
  timeout: 30000,
  headers: { 'Content-Type': 'application/json' }
});

// 响应拦截器：统一错误格式
apiClient.interceptors.response.use(
  (response) => response,
  (error: AxiosError<{ error?: string }>) => {
    const apiError: ApiError = {
      error: error.response?.data?.error || error.message || '请求失败',
      status: error.response?.status || 0
    };
    return Promise.reject(apiError);
  }
);

export default apiClient;
```

各 API 模块（`services.ts`、`projects.ts`、`instances.ts`、`drivers.ts`、`server.ts`）按设计文档 §8.2 实现，此处不重复。

### 3.5 WebSocket 客户端

```typescript
// src/webui/src/api/driverlab-ws.ts
export type WsMessageType = 'driver.started' | 'driver.restarted' | 'meta' |
                             'stdout' | 'driver.exited' | 'error';

export interface WsMessage {
  type: WsMessageType;
  [key: string]: unknown;
}

export class DriverLabWsClient {
  private ws: WebSocket | null = null;
  private listeners = new Map<string, Set<(data: unknown) => void>>();

  connect(driverId: string, runMode: 'oneshot' | 'keepalive', args: string[] = []): void {
    const proto = window.location.protocol === 'https:' ? 'wss' : 'ws';
    const host = window.location.host;
    const params = new URLSearchParams({ runMode });
    if (args.length > 0) params.set('args', args.join(','));

    this.ws = new WebSocket(
      `${proto}://${host}/api/driverlab/${encodeURIComponent(driverId)}?${params}`
    );

    this.ws.onopen = () => this.emit('connected', {});
    this.ws.onmessage = (e) => {
      try {
        const msg: WsMessage = JSON.parse(e.data);
        this.emit('message', msg);
        this.emit(msg.type, msg);
      } catch { /* ignore non-JSON */ }
    };
    this.ws.onerror = (e) => this.emit('error', e);
    this.ws.onclose = () => this.emit('disconnected', {});
  }

  send(message: Record<string, unknown>): void {
    if (this.ws?.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(message));
    }
  }

  exec(command: string, data: Record<string, unknown>): void {
    this.send({ type: 'exec', cmd: command, data });
  }

  cancel(): void {
    this.send({ type: 'cancel' });
  }

  disconnect(): void {
    this.ws?.close();
    this.ws = null;
  }

  on(event: string, callback: (data: unknown) => void): void {
    if (!this.listeners.has(event)) this.listeners.set(event, new Set());
    this.listeners.get(event)!.add(callback);
  }

  off(event: string, callback: (data: unknown) => void): void {
    this.listeners.get(event)?.delete(callback);
  }

  private emit(event: string, data: unknown): void {
    this.listeners.get(event)?.forEach(cb => cb(data));
  }
}
```

### 3.6 SSE EventStream 客户端

```typescript
// src/webui/src/api/event-stream.ts
import type { ServerEvent } from '@/types/server';

export class EventStream {
  private es: EventSource | null = null;
  private listeners = new Map<string, Set<(event: ServerEvent) => void>>();

  connect(filters: string[] = []): void {
    const params = new URLSearchParams();
    if (filters.length > 0) params.set('filter', filters.join(','));

    this.es = new EventSource(`/api/events/stream?${params}`);
    this.es.onopen = () => this.emit('connected', { type: 'connected', data: {} });
    this.es.onerror = () => this.emit('error', { type: 'error', data: {} });

    // 当前后端已实现的事件类型（见 server_manager.cpp EventBus::publish 调用）
    const eventTypes = [
      'instance.started', 'instance.finished',
      'schedule.triggered', 'schedule.suppressed'
    ];
    // 注意：project.status_changed / service.scanned / driver.scanned
    // 尚未在后端实现，待后续里程碑按需添加（见 M67 说明）

    for (const type of eventTypes) {
      this.es.addEventListener(type, (e: Event) => {
        const me = e as MessageEvent;
        const event: ServerEvent = { type, data: JSON.parse(me.data) };
        this.emit('event', event);
        this.emit(type, event);
      });
    }
  }

  close(): void {
    this.es?.close();
    this.es = null;
  }

  on(event: string, callback: (event: ServerEvent) => void): void {
    if (!this.listeners.has(event)) this.listeners.set(event, new Set());
    this.listeners.get(event)!.add(callback);
  }

  off(event: string, callback: (event: ServerEvent) => void): void {
    this.listeners.get(event)?.delete(callback);
  }

  private emit(event: string, data: ServerEvent): void {
    this.listeners.get(event)?.forEach(cb => cb(data));
  }
}
```

### 3.7 目录结构

```
src/webui/
├── index.html
├── package.json
├── tsconfig.json
├── vite.config.ts
├── .eslintrc.cjs
├── .prettierrc
└── src/
    ├── api/
    │   ├── client.ts
    │   ├── services.ts
    │   ├── projects.ts
    │   ├── instances.ts
    │   ├── drivers.ts
    │   ├── server.ts
    │   ├── driverlab-ws.ts
    │   └── event-stream.ts
    ├── types/
    │   ├── service.ts
    │   ├── project.ts
    │   ├── instance.ts
    │   ├── driver.ts
    │   ├── server.ts
    │   └── api.ts
    ├── test-setup.ts
    ├── App.tsx          (最小占位)
    ├── main.tsx
    └── router.tsx       (最小占位)
```

---

## 4. 文件变更清单

### 4.1 新增文件

- `src/webui/package.json` — 项目配置与依赖
- `src/webui/tsconfig.json` — TypeScript 配置
- `src/webui/vite.config.ts` — Vite 构建配置
- `src/webui/.eslintrc.cjs` — ESLint 配置
- `src/webui/.prettierrc` — Prettier 配置
- `src/webui/index.html` — HTML 入口
- `src/webui/src/main.tsx` — React 入口
- `src/webui/src/App.tsx` — 根组件占位
- `src/webui/src/router.tsx` — 路由占位
- `src/webui/src/test-setup.ts` — 测试环境配置
- `src/webui/src/api/client.ts` — Axios 客户端
- `src/webui/src/api/services.ts` — Services API
- `src/webui/src/api/projects.ts` — Projects API
- `src/webui/src/api/instances.ts` — Instances API
- `src/webui/src/api/drivers.ts` — Drivers API
- `src/webui/src/api/server.ts` — Server API
- `src/webui/src/api/driverlab-ws.ts` — WebSocket 客户端
- `src/webui/src/api/event-stream.ts` — SSE 客户端
- `src/webui/src/types/service.ts` — Service 类型
- `src/webui/src/types/project.ts` — Project 类型
- `src/webui/src/types/instance.ts` — Instance 类型
- `src/webui/src/types/driver.ts` — Driver 类型
- `src/webui/src/types/server.ts` — Server 类型
- `src/webui/src/types/api.ts` — 通用响应包装类型

### 4.2 测试文件

- `src/webui/src/api/__tests__/client.test.ts`
- `src/webui/src/api/__tests__/services.test.ts`
- `src/webui/src/api/__tests__/projects.test.ts`
- `src/webui/src/api/__tests__/instances.test.ts`
- `src/webui/src/api/__tests__/drivers.test.ts`
- `src/webui/src/api/__tests__/server.test.ts`
- `src/webui/src/api/__tests__/driverlab-ws.test.ts`
- `src/webui/src/api/__tests__/event-stream.test.ts`

---

## 5. 测试与验收

### 5.1 单元测试场景

**API Client（client.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 1 | 成功请求 | 返回 response.data |
| 2 | 400 错误 | 拦截器转换为 ApiError，含 error 消息 |
| 3 | 404 错误 | ApiError.status === 404 |
| 4 | 500 错误 | ApiError.error 包含服务端错误消息 |
| 5 | 网络错误 | ApiError.status === 0 |
| 6 | 超时 | 30s 超时触发错误 |

**Services API（services.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 7 | `list()` | GET /api/services |
| 8 | `detail(id)` | GET /api/services/{id} |
| 9 | `create(data)` | POST /api/services + body |
| 10 | `delete(id)` | DELETE /api/services/{id} |
| 11 | `scan()` | POST /api/services/scan |
| 12 | `files(id)` | GET /api/services/{id}/files |
| 13 | `fileRead(id, path)` | GET /api/services/{id}/files/content?path=... |
| 14 | `fileWrite(id, path, content)` | PUT /api/services/{id}/files/content |
| 15 | `fileCreate(id, path, content)` | POST /api/services/{id}/files/content |
| 16 | `fileDelete(id, path)` | DELETE /api/services/{id}/files/content |
| 17 | `validateSchema(id, schema)` | POST /api/services/{id}/validate-schema |
| 18 | `generateDefaults(id)` | POST /api/services/{id}/generate-defaults |
| 19 | `validateConfig(id, config)` | POST /api/services/{id}/validate-config |

**Projects API（projects.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 20 | `list()` | GET /api/projects |
| 21 | `detail(id)` | GET /api/projects/{id} |
| 22 | `create(data)` | POST /api/projects + body |
| 23 | `update(id, data)` | PUT /api/projects/{id} + body |
| 24 | `delete(id)` | DELETE /api/projects/{id} |
| 25 | `start(id)` | POST /api/projects/{id}/start |
| 26 | `stop(id)` | POST /api/projects/{id}/stop |
| 27 | `reload(id)` | POST /api/projects/{id}/reload |
| 28 | `runtime(id)` | GET /api/projects/{id}/runtime |
| 29 | `runtimeBatch(ids)` | GET /api/projects/runtime?ids=... |
| 30 | `setEnabled(id, enabled)` | PATCH /api/projects/{id}/enabled |
| 31 | `logs(id, params)` | GET /api/projects/{id}/logs |

**Instances API（instances.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 32 | `list()` | GET /api/instances |
| 33 | `detail(id)` | GET /api/instances/{id} |
| 34 | `terminate(id)` | POST /api/instances/{id}/terminate |
| 35 | `processTree(id)` | GET /api/instances/{id}/process-tree |
| 36 | `resources(id)` | GET /api/instances/{id}/resources |
| 37 | `logs(id, params)` | GET /api/instances/{id}/logs |

**Drivers API（drivers.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 38 | `list()` | GET /api/drivers |
| 39 | `detail(id)` | GET /api/drivers/{id} |
| 40 | `scan()` | POST /api/drivers/scan |

**Server API（server.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 41 | `status()` | GET /api/server/status |

**DriverLab WebSocket（driverlab-ws.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 42 | `connect()` 构造正确 URL | 包含 driverId、runMode、args |
| 43 | 收到 JSON 消息触发 listener | on('message') 回调被调用 |
| 44 | 收到 typed 消息触发对应 listener | on('meta') 回调被调用 |
| 45 | `exec()` 发送正确格式 | `{type:'exec', cmd, data}` |
| 46 | `cancel()` 发送 cancel | `{type:'cancel'}` |
| 47 | `disconnect()` 关闭连接 | ws.close() 被调用 |
| 48 | `off()` 移除 listener | 回调不再被调用 |

**EventStream（event-stream.test.ts）**：

| # | 场景 | 验证点 |
|---|------|--------|
| 49 | `connect()` 构造正确 URL | 包含 filter 参数 |
| 50 | 收到事件触发 listener | on('event') 回调被调用 |
| 51 | 收到 typed 事件触发对应 listener | on('instance.started') 回调被调用 |
| 52 | `close()` 关闭连接 | es.close() 被调用 |

### 5.2 验收标准

- Vite 开发服务器可正常启动（`npm run dev`）
- TypeScript 编译无错误（`npm run type-check`）
- ESLint 检查通过（`npm run lint`）
- 全部 API 客户端单元测试通过（`npm run test`）
- 开发代理正确转发 `/api` 请求到后端
- 类型定义与后端 API 响应一致

---

## 6. 风险与控制

- **风险 1**：后端 API 响应格式与类型定义不一致
  - 控制：类型定义基于已实现的后端代码（M49-M57）编写，非推测；开发时通过实际请求验证
- **风险 2**：WebSocket Mock 测试复杂度
  - 控制：使用 `vitest` 的 mock 能力模拟 WebSocket 对象，不依赖真实连接
- **风险 3**：依赖版本冲突
  - 控制：使用 `npm ci` 锁定版本；`package-lock.json` 入库

---

## 7. 里程碑完成定义（DoD）

- WebUI 工程初始化完成，`npm run dev` 可启动
- TypeScript 编译通过
- 全部 API 模块实现
- WebSocket / SSE 客户端实现
- 全部类型定义完成
- 单元测试覆盖 API 客户端层
- 本里程碑文档入库
