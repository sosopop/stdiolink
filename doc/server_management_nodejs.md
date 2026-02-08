# stdiolink 服务管理器 — Node.js 实现方案

> 版本: 0.1.0
> 日期: 2026-02-08
> 基于: `doc/server_management.md` 需求与设计文档
> 定位: 本文档是 Node.js 技术栈的**实现方案**，需求模型、概念术语、API 接口等沿用设计文档定义

---

## 目录

1. [技术栈选型](#1-技术栈选型)
2. [项目结构](#2-项目结构)
3. [核心依赖](#3-核心依赖)
4. [模块实现方案](#4-模块实现方案)
5. [数据库层](#5-数据库层)
6. [配置校验引擎](#6-配置校验引擎)
7. [调度引擎](#7-调度引擎)
8. [实例运行器](#8-实例运行器)
9. [Driver 元数据获取](#9-driver-元数据获取)
10. [日志系统](#10-日志系统)
11. [Web API 层](#11-web-api-层)
12. [前端集成](#12-前端集成)
13. [与现有 C++ 代码的关系](#13-与现有-c-代码的关系)
14. [分阶段实施路线](#14-分阶段实施路线)

---

## 1. 技术栈选型

### 1.1 选型理由

服务管理器的核心工作是 Web API + 进程管理 + SQLite + 定时调度，均为 Node.js 的强项。管理器与 `stdiolink_service` 子进程通过进程启动 + 文件系统交互，不需要链接 C++ 库。

### 1.2 技术约束

| 项目 | 选型 | 说明 |
|------|------|------|
| 运行时 | Node.js >= 20 LTS | 原生支持 ESM、稳定的 child_process API |
| 语言 | TypeScript 5.x | 类型安全，提升可维护性 |
| Web 框架 | Fastify | 高性能、插件体系完善、原生 TypeScript 支持 |
| 数据库 | SQLite (better-sqlite3) | 同步 API、零部署、与设计文档一致 |
| 进程管理 | Node.js child_process | spawn 管理 stdiolink_service 子进程 |
| 日志 | pino | Fastify 默认日志库，JSON 格式，支持文件轮转 |
| 前端 | Vue 3 + Vite | 轻量 SPA，开发体验好 |
| 包管理 | pnpm | 快速、磁盘高效 |

### 1.3 与现有代码的关系

```
现有代码（不修改）                    新增代码
─────────────────                    ────────────
build/bin/stdiolink_service  ◄────── 管理器作为父进程启动子进程
build/bin/*_driver           ◄────── 由 JS 脚本通过 openDriver() 启动

server_manager/              ◄────── 新增独立 Node.js 项目
  ├─ src/                             TypeScript 后端
  └─ web/                             Vue 3 前端
```

管理器是独立的 Node.js 项目，不修改现有 C++ 代码。两者通过文件系统和进程启动交互。

---

## 2. 项目结构

```
server_manager/
  ├── package.json
  ├── tsconfig.json
  ├── src/
  │   ├── index.ts                  # 入口：启动 Fastify + 调度引擎
  │   ├── config.ts                 # 管理器自身配置（命令行参数 + 默认值）
  │   ├── api/                      # Web API 路由
  │   │   ├── drivers.ts
  │   │   ├── services.ts
  │   │   ├── projects.ts
  │   │   ├── schedules.ts
  │   │   ├── instances.ts
  │   │   └── index.ts              # 路由注册
  │   ├── core/                     # 核心业务逻辑
  │   │   ├── driver-manager.ts
  │   │   ├── service-manager.ts
  │   │   ├── project-manager.ts
  │   │   └── config-validator.ts   # 配置校验引擎
  │   ├── scheduler/                # 调度引擎
  │   │   ├── scheduler.ts          # 主调度循环
  │   │   ├── triggers/             # 各调度类型的触发判断
  │   │   │   ├── once.ts
  │   │   │   ├── fixed-rate.ts
  │   │   │   ├── fixed-delay.ts
  │   │   │   ├── cron.ts
  │   │   │   └── daemon.ts
  │   │   └── types.ts
  │   ├── runner/                   # 实例运行器
  │   │   ├── instance-runner.ts    # 子进程管理
  │   │   └── driver-meta.ts        # Driver JSONL 元数据获取
  │   ├── storage/                  # 数据持久化
  │   │   ├── database.ts           # SQLite 初始化 + 迁移
  │   │   ├── driver-repo.ts
  │   │   ├── service-repo.ts
  │   │   ├── project-repo.ts
  │   │   ├── schedule-repo.ts
  │   │   └── instance-repo.ts
  │   ├── logger/                   # 日志系统
  │   │   └── instance-logger.ts    # 实例日志文件管理
  │   └── types/                    # 共享类型定义
  │       ├── driver.ts
  │       ├── service.ts
  │       ├── project.ts
  │       ├── schedule.ts
  │       └── instance.ts
  ├── web/                          # Vue 3 前端（独立子项目）
  │   ├── package.json
  │   ├── vite.config.ts
  │   ├── src/
  │   │   ├── views/
  │   │   ├── components/
  │   │   └── api/                  # API 客户端
  │   └── dist/                     # 构建产物，由后端静态托管
  └── tests/
      ├── core/
      ├── scheduler/
      └── api/
```

---

## 3. 核心依赖

### 3.1 后端依赖

| 包名 | 用途 |
|------|------|
| `fastify` | HTTP 框架，路由、请求校验、JSON 序列化 |
| `@fastify/static` | 静态文件托管（前端 SPA） |
| `@fastify/websocket` | WebSocket 支持（Phase 2） |
| `better-sqlite3` | SQLite 驱动，同步 API，无需连接池 |
| `pino` / `pino-roll` | 日志库 + 文件轮转 |
| `cron-parser` | Cron 表达式解析 |
| `ajv` | JSON Schema 校验（用于配置校验） |
| `nanoid` | 短 ID 生成（Project ID、Instance ID） |
| `commander` | 命令行参数解析 |

### 3.2 开发依赖

| 包名 | 用途 |
|------|------|
| `typescript` | TypeScript 编译器 |
| `tsx` | 开发时直接运行 TS |
| `vitest` | 单元测试框架 |
| `@types/better-sqlite3` | 类型定义 |

### 3.3 前端依赖

| 包名 | 用途 |
|------|------|
| `vue` | UI 框架 |
| `vue-router` | 路由 |
| `naive-ui` / `element-plus` | 组件库（动态表单渲染） |
| `vite` | 构建工具 |

---

## 4. 模块实现方案

### 4.1 模块总览

```
┌─────────────────────────────────────────────────────┐
│                    Fastify Server                     │
│  ┌─────────────────────────────────────────────────┐ │
│  │              API Routes (api/)                   │ │
│  └──────────────────────┬──────────────────────────┘ │
│                         │                             │
│  ┌──────────┐ ┌────────┴────────┐ ┌──────────────┐  │
│  │  Driver   │ │    Project      │ │   Service     │  │
│  │  Manager  │ │    Manager      │ │   Manager     │  │
│  └─────┬────┘ └───┬────────┬───┘ └──────────────┘  │
│        │          │        │                         │
│        │    ┌─────┴──┐  ┌──┴──────────┐             │
│        │    │Config  │  │  Scheduler   │             │
│        │    │Validator│  │  + Instance  │             │
│        │    └────────┘  │    Runner    │             │
│        │                └──────┬──────┘             │
│  ┌─────┴────────────────────────┴──────────────┐    │
│  │           Storage (better-sqlite3)           │    │
│  └──────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────┘
                         │
                    child_process.spawn
                         │
              ┌──────────┴──────────┐
              │  stdiolink_service   │
              │    子进程 (C++)       │
              └─────────────────────┘
```

### 4.2 Driver Manager

```typescript
// src/core/driver-manager.ts

export class DriverManager {
  constructor(private db: Database, private logger: Logger) {}

  /** 手动注册 Driver */
  register(program: string, opts?: { id?: string; description?: string; tags?: string[] }): Driver

  /** 扫描目录，自动发现可执行文件并注册 */
  scanDirectory(dirPath: string): Driver[]

  /** 获取元数据（优先读缓存，miss 时启动进程获取） */
  getMeta(id: string): DriverMeta | null

  /** 手动刷新元数据 */
  refreshMeta(id: string): DriverMeta

  /** CRUD */
  list(): Driver[]
  get(id: string): Driver | null
  update(id: string, patch: Partial<Driver>): Driver
  remove(id: string): void
}
```

**目录扫描逻辑**：遍历目录，过滤可执行文件（Unix 检查 `fs.accessSync(path, fs.constants.X_OK)`，Windows 检查 `.exe` 后缀），尝试读取同目录下的 `driver.meta.json` 静态文件，若不存在则标记为待获取元数据。

### 4.3 Service Manager

```typescript
// src/core/service-manager.ts

export class ServiceManager {
  constructor(private db: Database, private logger: Logger) {}

  /** 注册 Service：校验目录结构 → 解析 manifest → 缓存 Schema */
  register(serviceDir: string): Service

  /** CRUD */
  list(): Service[]
  get(id: string): Service | null
  getSchema(id: string): ConfigSchema | null
  update(id: string, patch: Partial<Service>): Service
  remove(id: string): void  // 需先检查无关联 Project
}
```

**目录校验**：检查 `manifest.json`、`index.js`、`config.schema.json` 三个文件是否存在。用 `JSON.parse` 解析 manifest 和 schema，失败则标记 `status: 'error'`。

### 4.4 Project Manager

```typescript
// src/core/project-manager.ts

export class ProjectManager {
  constructor(
    private db: Database,
    private serviceManager: ServiceManager,
    private configValidator: ConfigValidator,
    private dataRoot: string,
    private logger: Logger
  ) {}

  /** 创建 Project：校验 Service → 合并配置 → 写 config.json → 创建 workspace */
  create(opts: CreateProjectOpts): Project

  /** 更新配置：校验 → 合并默认值 → 覆盖写入 config.json */
  updateConfig(id: string, config: Record<string, unknown>): Record<string, unknown>

  /** 读取配置：直接从磁盘 config.json 读取 */
  getConfig(id: string): Record<string, unknown>

  /** 启用/禁用 */
  enable(id: string): void
  disable(id: string): void

  /** CRUD */
  list(): Project[]
  get(id: string): Project | null
  update(id: string, patch: Partial<Project>): Project
  remove(id: string): void  // 需先停止运行中实例
}
```

**关键实现细节**：

- `config.json` 是磁盘文件为唯一真相源，SQLite 不存储配置内容
- 创建时调用 `fs.mkdirSync` 创建 `projects/<id>/` 和 `projects/<id>/workspace/`
- 删除时先检查无运行中实例，再删除目录和数据库记录

---

## 5. 数据库层

### 5.1 初始化与迁移

```typescript
// src/storage/database.ts
import Database from 'better-sqlite3';

export function initDatabase(dbPath: string): Database.Database {
  const db = new Database(dbPath);
  db.pragma('journal_mode = WAL');
  db.pragma('foreign_keys = ON');
  db.exec(SCHEMA_SQL);
  return db;
}
```

`better-sqlite3` 的同步 API 天然避免了并发写入问题，无需额外的写入队列。WAL 模式允许读写并发。

### 5.2 表结构

表结构与设计文档 Section 14 一致，此处给出 Node.js 侧的建表 SQL：

```sql
-- drivers
CREATE TABLE IF NOT EXISTS drivers (
    id            TEXT PRIMARY KEY,
    program       TEXT NOT NULL,
    description   TEXT DEFAULT '',
    tags          TEXT DEFAULT '[]',
    meta_json     TEXT,
    registered_at TEXT NOT NULL
);

-- services
CREATE TABLE IF NOT EXISTS services (
    id            TEXT PRIMARY KEY,
    name          TEXT NOT NULL,
    version       TEXT NOT NULL,
    service_dir   TEXT NOT NULL,
    description   TEXT DEFAULT '',
    tags          TEXT DEFAULT '[]',
    status        TEXT DEFAULT 'available',
    schema_json   TEXT,
    registered_at TEXT NOT NULL,
    updated_at    TEXT NOT NULL
);
```

```sql
-- projects（config 不存 SQLite，磁盘文件为唯一真相源）
CREATE TABLE IF NOT EXISTS projects (
    id            TEXT PRIMARY KEY,
    name          TEXT NOT NULL,
    service_id    TEXT NOT NULL REFERENCES services(id),
    description   TEXT DEFAULT '',
    tags          TEXT DEFAULT '[]',
    status        TEXT DEFAULT 'enabled',
    created_at    TEXT NOT NULL,
    updated_at    TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_projects_service ON projects(service_id);
```

```sql
-- schedules
CREATE TABLE IF NOT EXISTS schedules (
    project_id      TEXT PRIMARY KEY REFERENCES projects(id),
    type            TEXT NOT NULL,
    interval_ms     INTEGER,
    cron_expr       TEXT,
    max_concurrent  INTEGER DEFAULT 1,
    overlap_policy  TEXT DEFAULT 'skip',
    timeout_ms      INTEGER DEFAULT 0,
    max_retries     INTEGER DEFAULT 0,
    retry_delay_ms  INTEGER DEFAULT 5000,
    last_start_at   TEXT,
    last_end_at     TEXT,
    updated_at      TEXT NOT NULL
);
```

```sql
-- instances
CREATE TABLE IF NOT EXISTS instances (
    id            TEXT PRIMARY KEY,
    project_id    TEXT NOT NULL REFERENCES projects(id),
    status        TEXT NOT NULL DEFAULT 'starting',
    exit_code     INTEGER,
    exit_reason   TEXT,
    retry_left    INTEGER DEFAULT 0,
    next_retry_at TEXT,
    started_at    TEXT NOT NULL,
    finished_at   TEXT,
    pid           INTEGER
);
```

### 5.3 Repository 模式

每张表对应一个 Repository 类，封装 SQL 操作：

```typescript
// src/storage/project-repo.ts
export class ProjectRepo {
  private stmts: {
    insert: Database.Statement;
    getById: Database.Statement;
    list: Database.Statement;
    updateStatus: Database.Statement;
  };

  constructor(private db: Database.Database) {
    this.stmts = {
      insert: db.prepare('INSERT INTO projects ...'),
      getById: db.prepare('SELECT * FROM projects WHERE id = ?'),
      list: db.prepare('SELECT * FROM projects'),
      updateStatus: db.prepare('UPDATE projects SET status = ? WHERE id = ?'),
    };
  }
}
```

使用 `db.prepare()` 预编译语句，避免重复解析 SQL。

---

## 6. 配置校验引擎

### 6.1 设计思路

stdiolink 的 `config.schema.json` 使用自定义 Schema 格式（基于 `FieldMeta` 类型系统），不是标准 JSON Schema。需要在 Node.js 侧实现一个轻量校验器来处理这套自定义格式。

### 6.2 支持的字段类型

| Schema type | JS 校验规则 |
|-------------|------------|
| `string` | `typeof value === 'string'` |
| `int` | `Number.isInteger(value)`，检查 min/max |
| `double` | `typeof value === 'number'`，检查 min/max |
| `bool` | `typeof value === 'boolean'` |
| `enum` | `enumValues.includes(value)` |
| `array` | `Array.isArray(value)`，递归校验 items |
| `object` | `typeof value === 'object'`，递归校验 fields |

### 6.3 实现

```typescript
// src/core/config-validator.ts

export interface ValidationError {
  path: string;    // 如 "device.host"
  message: string; // 如 "required field missing"
}

export class ConfigValidator {
  /** 校验配置是否符合 Schema */
  validate(schema: ConfigSchema, config: Record<string, unknown>): ValidationError[]

  /** 用 Schema 默认值填充缺失字段（深度合并） */
  fillDefaults(schema: ConfigSchema, config: Record<string, unknown>): Record<string, unknown>

  /** 校验 + 填充默认值，一步完成 */
  mergeAndValidate(
    schema: ConfigSchema,
    userConfig: Record<string, unknown>
  ): { errors: ValidationError[]; merged: Record<string, unknown> }
}
```

**合并规则**（与 C++ 侧 `ServiceConfigValidator` 一致）：
- 用户配置覆盖 Schema 默认值
- `object` 类型深度合并
- 标量和 `array` 类型整体替换
- 未填写的字段使用 Schema `default` 值

---

## 7. 调度引擎

### 7.1 设计思路

调度引擎基于 `setInterval` 驱动，每秒轮询一次。Node.js 单线程事件循环天然保证调度判断的串行执行，无需额外加锁。

### 7.2 实现

```typescript
// src/scheduler/scheduler.ts

export class Scheduler {
  private timer: NodeJS.Timeout | null = null;
  private runningInstances = new Map<string, Set<string>>(); // projectId → instanceIds

  constructor(
    private db: Database.Database,
    private runner: InstanceRunner,
    private logger: Logger
  ) {}

  start(): void {
    this.timer = setInterval(() => this.tick(), 1000);
  }

  stop(): void {
    if (this.timer) clearInterval(this.timer);
  }

  private tick(): void {
    const schedules = this.scheduleRepo.listWithEnabledProjects();
    for (const schedule of schedules) {
      if (this.shouldTrigger(schedule)) {
        this.handleTrigger(schedule);
      }
    }
  }

  private shouldTrigger(schedule: ScheduleWithProject): boolean {
    // 根据 schedule.type 分发到对应 trigger
  }

  private handleTrigger(schedule: ScheduleWithProject): void {
    const running = this.runningInstances.get(schedule.projectId)?.size ?? 0;
    if (running >= schedule.maxConcurrent) {
      if (schedule.overlapPolicy === 'skip') {
        this.logger.info({ projectId: schedule.projectId }, 'skipped: overlap');
        return;
      }
      if (schedule.overlapPolicy === 'terminate') {
        this.terminateRunning(schedule.projectId);
      }
    }
    this.runner.launch(schedule.projectId);
  }
}
```

### 7.3 触发判断

各调度类型的触发逻辑封装为独立函数：

```typescript
// src/scheduler/triggers/fixed-rate.ts
export function shouldTriggerFixedRate(schedule: Schedule, now: number): boolean {
  if (!schedule.lastStartAt) return true;
  return now - new Date(schedule.lastStartAt).getTime() >= schedule.intervalMs;
}

// src/scheduler/triggers/fixed-delay.ts
export function shouldTriggerFixedDelay(schedule: Schedule, now: number): boolean {
  if (!schedule.lastEndAt) return true;
  return now - new Date(schedule.lastEndAt).getTime() >= schedule.intervalMs;
  // intervalMs=0 时上一实例结束即触发
}

// src/scheduler/triggers/cron.ts
import { parseExpression } from 'cron-parser';
export function shouldTriggerCron(schedule: Schedule, now: Date): boolean {
  const interval = parseExpression(schedule.cronExpr, { currentDate: now });
  const prev = interval.prev().toDate();
  // 检查 prev 是否在上次触发之后
  return !schedule.lastStartAt || prev > new Date(schedule.lastStartAt);
}
```

---

## 8. 实例运行器

### 8.1 设计思路

实例运行器负责启动、监控、终止 `stdiolink_service` 子进程。使用 Node.js `child_process.spawn` 替代 C++ 方案中的 `QProcess`。

### 8.2 启动流程

```typescript
// src/runner/instance-runner.ts
import { spawn, ChildProcess } from 'node:child_process';

export class InstanceRunner {
  private processes = new Map<string, ChildProcess>(); // instanceId → process

  constructor(
    private db: Database.Database,
    private instanceLogger: InstanceLogger,
    private config: AppConfig,
    private logger: Logger
  ) {}

  launch(projectId: string): string {
    const project = this.projectRepo.get(projectId);
    const service = this.serviceRepo.get(project.serviceId);
    const instanceId = nanoid();

    // 1. 写入 instance 记录
    this.instanceRepo.insert({
      id: instanceId,
      projectId,
      status: 'starting',
      startedAt: new Date().toISOString(),
    });

    // 2. 构造启动参数
    const projectDir = path.join(this.config.dataRoot, 'projects', projectId);
    const configFile = path.join(projectDir, 'config.json');
    const workspaceDir = path.join(projectDir, 'workspace');

    // 3. 启动子进程
    const child = spawn(
      this.config.stdiolinkServicePath,
      [service.serviceDir, `--config-file=${configFile}`],
      {
        cwd: workspaceDir,
        env: {
          ...process.env,
          STDIOLINK_WORKSPACE: workspaceDir,
          STDIOLINK_SHARED: path.join(this.config.dataRoot, 'shared'),
          STDIOLINK_DATA_ROOT: this.config.dataRoot,
          STDIOLINK_PROJECT_ID: projectId,
          STDIOLINK_INSTANCE_ID: instanceId,
          STDIOLINK_SERVICE_ID: project.serviceId,
        },
        stdio: ['ignore', 'ignore', 'pipe'], // stdin 关闭, stdout 忽略, stderr 捕获
      }
    );

    this.processes.set(instanceId, child);
    this.instanceRepo.updateStatus(instanceId, 'running', child.pid);

    // 4. 捕获 stderr 日志
    this.instanceLogger.attach(instanceId, projectId, child.stderr);

    // 5. 监听退出
    child.on('close', (code) => this.onExit(instanceId, projectId, code));

    // 6. 超时控制
    this.setupTimeout(instanceId, projectId);

    return instanceId;
  }
}
```

### 8.3 退出处理与超时

```typescript
private onExit(instanceId: string, projectId: string, code: number | null): void {
  this.processes.delete(instanceId);
  const status = code === 0 ? 'finished' : 'failed';
  this.instanceRepo.finish(instanceId, status, code);
  this.scheduleRepo.updateLastEnd(projectId, new Date().toISOString());
}

private setupTimeout(instanceId: string, projectId: string): void {
  const schedule = this.scheduleRepo.get(projectId);
  if (!schedule?.timeoutMs || schedule.timeoutMs <= 0) return;

  setTimeout(() => {
    const proc = this.processes.get(instanceId);
    if (!proc) return;
    proc.kill('SIGTERM');
    // 给 5 秒 graceful shutdown，之后 SIGKILL
    setTimeout(() => {
      if (this.processes.has(instanceId)) proc.kill('SIGKILL');
    }, 5000);
    this.instanceRepo.finish(instanceId, 'timeout', null);
  }, schedule.timeoutMs);
}
```

### 8.4 启动恢复

管理器启动时，将数据库中 `starting` 和 `running` 状态的 Instance 标记为 `failed`：

```typescript
recoverStaleInstances(): void {
  this.db.prepare(
    `UPDATE instances SET status = 'failed', exit_reason = 'server_restart'
     WHERE status IN ('starting', 'running')`
  ).run();
}
```

---

## 9. Driver 元数据获取

### 9.1 JSONL 协议交互

Driver 元数据通过 stdiolink JSONL 协议获取。管理器需要实现一个轻量的 JSONL 客户端，启动 Driver 进程并发送 `meta.describe` 命令。

### 9.2 实现

```typescript
// src/runner/driver-meta.ts
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';

export async function fetchDriverMeta(
  program: string,
  timeoutMs = 5000
): Promise<DriverMeta> {
  return new Promise((resolve, reject) => {
    const child = spawn(program, [], {
      stdio: ['pipe', 'pipe', 'ignore'],
    });

    const rl = createInterface({ input: child.stdout });
    const timer = setTimeout(() => {
      child.kill();
      reject(new Error('meta.describe timeout'));
    }, timeoutMs);

    rl.on('line', (line) => {
      try {
        const msg = JSON.parse(line);
        if (msg.id === 1 && msg.result) {
          clearTimeout(timer);
          child.kill();
          resolve(msg.result as DriverMeta);
        }
      } catch { /* 忽略非 JSON 行 */ }
    });

    child.on('error', (err) => {
      clearTimeout(timer);
      reject(err);
    });

    // 发送 meta.describe 请求
    child.stdin.write(
      JSON.stringify({ id: 1, cmd: 'meta.describe' }) + '\n'
    );
  });
}
```

**协议说明**：
- 请求格式：`{"id":1,"cmd":"meta.describe"}\n`（一行 JSON + 换行）
- 响应格式：`{"id":1,"result":{...}}\n`（Driver 返回元数据）
- 超时后直接 kill 进程

---

## 10. 日志系统

### 10.1 管理器自身日志

使用 pino 作为管理器日志库（Fastify 默认集成）：

```typescript
import pino from 'pino';

const logger = pino({
  level: config.logLevel,
  transport: config.logFile
    ? { target: 'pino-roll', options: { file: config.logFile, size: '10m', limit: { count: 5 } } }
    : { target: 'pino-pretty' },  // 开发时美化输出
});
```

### 10.2 实例日志

每个 Instance 的 stderr 输出写入 Project 目录下的日志文件：

```typescript
// src/logger/instance-logger.ts
import { createWriteStream, WriteStream } from 'node:fs';
import { Readable } from 'node:stream';
import { createInterface } from 'node:readline';

export class InstanceLogger {
  private streams = new Map<string, WriteStream>();

  attach(instanceId: string, projectId: string, stderr: Readable): void {
    const logDir = path.join(this.dataRoot, 'projects', projectId, 'logs');
    fs.mkdirSync(logDir, { recursive: true });

    const logPath = path.join(logDir, `${instanceId}.log`);
    const ws = createWriteStream(logPath, { flags: 'a' });
    this.streams.set(instanceId, ws);

    const rl = createInterface({ input: stderr });
    rl.on('line', (line) => {
      const ts = new Date().toISOString();
      ws.write(`${ts} ${line}\n`);
    });
  }

  detach(instanceId: string): void {
    const ws = this.streams.get(instanceId);
    if (ws) { ws.end(); this.streams.delete(instanceId); }
  }
}
```

**日志文件路径**：`<data_root>/projects/<project_id>/logs/<instance_id>.log`

**日志读取 API**：读取日志文件内容，支持 offset + limit 分页（按行数）。

---

## 11. Web API 层

### 11.1 Fastify 路由注册

```typescript
// src/api/index.ts
import { FastifyInstance } from 'fastify';

export async function registerRoutes(app: FastifyInstance, ctx: AppContext) {
  app.register(driverRoutes, { prefix: '/api/v1/drivers' });
  app.register(serviceRoutes, { prefix: '/api/v1/services' });
  app.register(projectRoutes, { prefix: '/api/v1/projects' });
  app.register(instanceRoutes, { prefix: '/api/v1/instances' });
}
```

### 11.2 路由示例（Projects）

```typescript
// src/api/projects.ts
import { FastifyInstance } from 'fastify';

export async function projectRoutes(app: FastifyInstance) {
  const { projectManager } = app.ctx;

  app.get('/', async () => projectManager.list());

  app.get('/:id', async (req) => {
    const project = projectManager.get(req.params.id);
    if (!project) throw app.httpErrors.notFound();
    return project;
  });

  app.post('/', async (req, reply) => {
    const project = projectManager.create(req.body);
    reply.code(201);
    return project;
  });

  app.put('/:id/config', async (req) => {
    return projectManager.updateConfig(req.params.id, req.body);
  });

  app.post('/:id/enable', async (req) => {
    projectManager.enable(req.params.id);
    return { ok: true };
  });

  app.post('/:id/trigger', async (req) => {
    const instanceId = app.ctx.runner.launch(req.params.id);
    return { instanceId };
  });
}
```

### 11.3 API 端点总览

API 端点与设计文档 Section 12 完全一致，此处不重复列出。Fastify 的 JSON Schema 校验可直接用于请求体校验：

```typescript
app.post('/', {
  schema: {
    body: {
      type: 'object',
      required: ['serviceId', 'name'],
      properties: {
        serviceId: { type: 'string' },
        name: { type: 'string' },
        description: { type: 'string' },
        tags: { type: 'array', items: { type: 'string' } },
        config: { type: 'object' },
      },
    },
  },
}, async (req, reply) => { ... });
```

### 11.4 错误处理

```typescript
// Fastify 统一错误处理
app.setErrorHandler((error, req, reply) => {
  const status = error.statusCode ?? 500;
  reply.status(status).send({
    error: error.message,
    code: error.code,
  });
});
```

---

## 12. 前端集成

### 12.1 开发模式

开发时前后端分离运行：

- 后端：`tsx src/index.ts`（端口 8080）
- 前端：`cd web && pnpm dev`（Vite 端口 5173，proxy 到 8080）

Vite 开发服务器配置 API 代理：

```typescript
// web/vite.config.ts
export default defineConfig({
  server: {
    proxy: {
      '/api': 'http://localhost:8080',
    },
  },
});
```

### 12.2 生产模式

构建前端后由 Fastify 静态托管：

```typescript
// src/index.ts
import fastifyStatic from '@fastify/static';

app.register(fastifyStatic, {
  root: path.join(__dirname, '../web/dist'),
  prefix: '/',
});
```

`pnpm build` 脚本同时构建后端（tsc）和前端（vite build）。

---

## 13. 与现有 C++ 代码的关系

### 13.1 交互方式

管理器与现有 C++ 代码的交互完全通过**进程启动**和**文件系统**，不存在库级别的链接或 FFI 调用。

```
Node.js 管理器                          C++ 可执行文件
──────────────                          ──────────────
                  child_process.spawn
InstanceRunner  ─────────────────────►  stdiolink_service
                  args: [svc_dir, --config-file=...]
                  env:  STDIOLINK_WORKSPACE=...
                  cwd:  workspace/

                  child_process.spawn
DriverManager   ─────────────────────►  driver 可执行文件
(元数据获取)       stdin:  {"id":1,"cmd":"meta.describe"}\n
                  stdout: {"id":1,"result":{...}}\n
```

### 13.2 需要在 Node.js 侧重新实现的逻辑

| C++ 模块 | Node.js 替代方案 | 复杂度 |
|----------|-----------------|--------|
| `ServiceDirectory` | `fs.existsSync` 检查三个文件 | 低 |
| `ServiceManifest` | `JSON.parse(fs.readFileSync('manifest.json'))` | 低 |
| `ServiceConfigSchema` | 自定义 Schema 解析器（遍历 fields 递归构建） | 中 |
| `ServiceConfigValidator` | 自定义校验器（类型检查 + 默认值合并） | 中 |
| `DriverRegistry.scanDirectory` | `fs.readdirSync` + `fs.accessSync` 检查可执行权限 | 低 |
| `Driver` JSONL 通信 | `child_process.spawn` + readline 逐行解析 | 低 |

其中复杂度最高的是 `ConfigValidator`，需要完整实现 stdiolink 的 FieldMeta 类型系统校验逻辑。建议为此编写充分的单元测试，确保与 C++ 侧行为一致。

### 13.3 不修改现有代码

与 C++ 方案一致，Node.js 管理器不修改 `src/stdiolink/` 和 `src/stdiolink_service/` 的任何代码。管理器作为独立项目存在于 `server_manager/` 目录。

---

## 14. 分阶段实施路线

### Phase 1：核心闭环

**目标**：最小可用的服务管理器，能注册、配置、调度、执行 Project。

| 步骤 | 内容 | 涉及文件 |
|------|------|---------|
| 1 | 项目脚手架：pnpm init、tsconfig、Fastify 启动 | `package.json`, `tsconfig.json`, `src/index.ts` |
| 2 | SQLite 初始化 + 全部建表 | `src/storage/database.ts` |
| 3 | Driver Manager：CRUD + 目录扫描 + 元数据获取 | `src/core/driver-manager.ts`, `src/runner/driver-meta.ts` |
| 4 | Service Manager：CRUD + 目录校验 + Schema 缓存 | `src/core/service-manager.ts` |
| 5 | Config Validator：Schema 解析 + 校验 + 默认值合并 | `src/core/config-validator.ts` |
| 6 | Project Manager：CRUD + 配置持久化 + 启停 | `src/core/project-manager.ts` |
| 7 | Instance Runner：子进程启动/监控/终止 + 日志文件 | `src/runner/instance-runner.ts`, `src/logger/instance-logger.ts` |
| 8 | Scheduler：`once` / `fixed_rate` / `cron` | `src/scheduler/scheduler.ts`, `src/scheduler/triggers/` |
| 9 | API Routes：全部 RESTful 端点 | `src/api/*.ts` |
| 10 | 集成测试 | `tests/` |

### Phase 2：可用性增强

**目标**：基础 Web UI、补全调度类型、WebSocket 推送。

| 模块 | 内容 |
|------|------|
| Scheduler | 补充 `fixed_delay` / `daemon` |
| Web UI | Vue 3 SPA：Dashboard、Driver/Service/Project 列表、配置表单、日志查看 |
| WebSocket | `@fastify/websocket` 实时事件推送（实例状态、日志） |
| 动态表单 | 前端根据 config-schema API 自动渲染配置表单 |

### Phase 3：扩展能力

**目标**：配置插件、权限认证、Service 包管理。

| 模块 | 内容 |
|------|------|
| Config Plugin | 基于 `ui.plugin` 扩展点实现前端插件组件 |
| 权限认证 | JWT Token 认证、API 访问控制 |
| Service 包管理 | Service 打包/导入/导出、版本管理 |
| 日志管理 | 日志归档、跨 Project 日志搜索 |

### Phase 4：高级功能

**目标**：在线编辑、审计、高级监控。

| 模块 | 内容 |
|------|------|
| 在线 JS 编辑器 | Monaco Editor 集成、语法高亮、保存/回滚 |
| 审计日志 | 操作审计记录 |
| 监控增强 | Project 资源占用统计、历史趋势图表 |

---

## 附录
