# stdiolink 服务管理器 — 需求与开发设计文档

> 版本: 0.1.0
> 日期: 2026-02-08
> 基于: stdiolink M1-M32 已实现里程碑
> 设计原则: 架构简约、层次分明、稳定可靠、不要求过高性能、灵活易扩展

---

## 目录

1. [需求概述](#1-需求概述)
2. [设计原则与约束](#2-设计原则与约束)
3. [核心概念与术语](#3-核心概念与术语)
4. [系统架构](#4-系统架构)
5. [Driver 管理](#5-driver-管理)
6. [Service 管理](#6-service-管理)
7. [Project 管理](#7-project-管理)
8. [配置系统](#8-配置系统)
9. [调度系统](#9-调度系统)
10. [实例运行与生命周期](#10-实例运行与生命周期)
11. [文件与存储系统](#11-文件与存储系统)
12. [Web API 设计](#12-web-api-设计)
13. [Web UI 设计](#13-web-ui-设计)
14. [数据模型](#14-数据模型)
15. [与现有代码的集成](#15-与现有代码的集成)
16. [分阶段实施路线](#16-分阶段实施路线)
17. [附录](#附录)

---

## 1. 需求概述

### 1.1 背景

stdiolink 已完成 M1-M32 里程碑，建立了完整的 IPC 协议层、Driver 框架、Host 管理、元数据系统和 JS Service 运行时。当前缺少一个统一的服务管理器来编排和调度这些 Service 与 Driver。

### 1.2 核心需求

| 领域 | 需求 |
|------|------|
| Driver 管理 | 注册、发现、元数据缓存、CRUD |
| Service 管理 | 只读服务模板注册与发现、CRUD |
| Project 管理 | 基于 Service 创建运行单元、独立配置、启停控制、CRUD |
| 配置系统 | Schema 声明、动态 UI 生成、配置传入 JS 脚本 |
| 调度系统 | 固定速率、固定延迟、防重叠、守护进程、Cron、单次执行、超时控制 |
| 文件隔离 | 每个 Project 独立工作目录，全局共享目录 |
| 管理接口 | Web API (RESTful) + Web UI |
| 后期扩展 | 权限管理、在线 JS 编辑与测试 |

### 1.3 不在范围内

- 分布式部署与集群调度
- 高并发/高吞吐场景优化
- Service 间直接通信（当前通过 Driver 中转）

---

## 2. 设计原则与约束

### 2.1 核心原则

| 原则 | 说明 |
|------|------|
| 架构简约 | 单进程管理器 + 多子进程执行，不引入消息队列或分布式组件 |
| 层次分明 | 控制面（管理器）与执行面（stdiolink_service）严格分离；Service → Project → Instance 三层概念清晰 |
| 稳定可靠 | 明确的状态机、恢复策略、单线程写入数据库 |
| 灵活扩展 | 预留配置插件扩展点、预留权限与编辑器扩展点 |
| 不追求高性能 | 优先一致性和可维护性，SQLite 足够 |

### 2.2 技术约束

- **语言**: C++17 / Qt 6（与现有代码一致）
- **数据库**: SQLite（单文件，零部署）
- **Web 框架**: Qt HTTP Server 或轻量嵌入式 HTTP 库
- **前端**: 静态 SPA（Vue/React），由管理器内嵌提供
- **Driver**: 无状态可执行文件，参数通过命令行传入
- **Service**: 有状态 JS 脚本，配置通过 `config.schema.json` + 管理器注入

### 2.3 与现有代码的关系

```
现有代码（不修改）              新增代码
─────────────────              ────────────
src/stdiolink/       ◄─────── 服务管理器直接复用
  ├─ host/Driver              （DriverRegistry、Driver、Task）
  ├─ host/DriverRegistry
  ├─ protocol/
  └─ platform/

src/stdiolink_service/ ◄───── 管理器作为父进程启动
  ├─ config/                   stdiolink_service 子进程
  ├─ engine/
  └─ bindings/

src/server_manager/   ◄───── 新增目录
  ├─ api/                      Web API 层
  ├─ core/                     核心管理逻辑
  │   ├─ service_manager/      Service 只读注册
  │   └─ project_manager/      Project 管理（配置、启停）
  ├─ scheduler/                调度引擎
  ├─ storage/                  数据持久化
  └─ web/                      静态前端资源
```

---

## 3. 核心概念与术语

### 3.1 概念模型

```
┌─────────────────────────────────────────────────────────────┐
│                      Server Manager                          │
│                                                             │
│  ┌──────────┐   ┌──────────┐   ┌───────────┐              │
│  │  Driver   │   │ Service  │   │  Project   │              │
│  │ Registry  │   │ Registry │   │  Manager   │              │
│  └────┬─────┘   └────┬─────┘   └─────┬─────┘              │
│       │              │                │                      │
│       │              │          ┌─────┴──────┐              │
│       │              │          │  Project    │              │
│       │              │          │  Config     │              │
│       │              │          └─────┬──────┘              │
│       │              │                │                      │
│       │              │    ┌───────────┼───────────┐         │
│       │              │    │           │           │         │
│       │              │  ┌─┴──────┐ ┌─┴──────┐ ┌─┴──────┐  │
│       │              │  │Schedule│ │Instance│ │Instance│  │
│       │              │  │ Rule   │ │ Runner │ │ Runner │  │
│       │              │  └────────┘ └───┬────┘ └───┬────┘  │
│       │              │                 │          │         │
└───────┼──────────────┼─────────────────┼──────────┼─────────┘
        │              │                 │          │
        ▼              ▼                 ▼          ▼
   Driver 可执行文件  manifest.json    stdiolink_service
   （无状态）        + index.js        子进程（每 Project）
                     + config.schema
```

**三层关系**：

```
Service（只读模板）──── 1:N ────► Project（运行单元）──── 1:N ────► Instance（一次执行）
```

### 3.2 术语定义

| 术语 | 定义 |
|------|------|
| **Driver** | 无状态可执行程序，通过 stdin/stdout JSONL 协议通信，不持有配置，参数通过命令行传入 |
| **Service** | 只读服务模板，一个标准服务目录（manifest.json + index.js + config.schema.json），定义服务的代码和配置 Schema |
| **Project** | 基于 Service 创建的运行单元，持有独立的 config.json 和 workspace 目录，是调度和执行的基本单位 |
| **Project Config** | Project 的运行时配置，由 Schema 声明、用户填写，持久化到 Project 目录下的 config.json |
| **Config Plugin** | （预留扩展点）特殊 UI 组件，为特定配置字段提供定制化编辑界面，当前阶段不实现 |
| **Schedule Rule** | 调度规则，定义 Project 的执行时机和策略 |
| **Instance** | Project 的一次执行实例，对应一个 `stdiolink_service` 子进程 |
| **Workspace** | 每个 Project 的独立工作目录，用于文件读写 |

---

## 4. 系统架构

### 4.1 整体架构

```
                    ┌─────────────┐
                    │   Web UI    │
                    │  (SPA 前端)  │
                    └──────┬──────┘
                           │ HTTP (Phase 2: + WebSocket)
                    ┌──────┴──────┐
                    │  API Server │
                    └──────┬──────┘
                           │
          ┌────────────────┼────────────────┐
          │                │                │
   ┌──────┴──────┐ ┌──────┴──────┐ ┌───────┴───────┐
   │   Driver    │ │   Service   │ │   Project     │
   │  Manager    │ │   Manager   │ │   Manager     │
   └──────┬──────┘ └─────────────┘ └───────┬───────┘
          │                                │
          │                ┌───────────────┼───────────────┐
          │                │               │               │
          │         ┌──────┴──────┐ ┌──────┴──────┐ ┌──────┴──────┐
          │         │   Config    │ │  Schedule   │ │  Instance   │
          │         │   Manager   │ │   Engine    │ │   Runner    │
          │         └─────────────┘ └─────────────┘ └──────┬──────┘
          │                                                │
   ┌──────┴──────┐                                  ┌──────┴──────┐
   │   Storage   │                                  │ QProcess    │
   │  (SQLite)   │                                  │ 子进程池     │
   └─────────────┘                                  └─────────────┘
```

### 4.2 模块职责

| 模块 | 职责 |
|------|------|
| **API Server** | HTTP 路由、请求校验、JSON 序列化（Phase 2 增加 WebSocket 事件推送） |
| **Driver Manager** | Driver 的注册/注销/发现/元数据缓存，复用现有 `DriverRegistry` |
| **Service Manager** | Service 只读模板的注册/注销/发现/Schema 缓存 |
| **Project Manager** | Project 的创建/配置/启停/状态管理，绑定 Service 模板 |
| **Config Manager** | 配置 Schema 解析、配置合并与校验、插件 UI 标记 |
| **Schedule Engine** | 调度规则解析、定时触发、防重叠控制 |
| **Instance Runner** | 启动/监控/终止 `stdiolink_service` 子进程，收集日志与退出状态 |
| **Storage** | SQLite 持久化，单线程写入队列 |

### 4.3 进程模型

```
server_manager (主进程，常驻)
  │
  ├─ [Project: 料仓A数据采集] (基于 Service data-collector)
  │  └─ stdiolink_service <svc_dir_A> --config-file=<projects/proj_1/config.json>
  │       └─ modbus_driver (孙进程，由 JS openDriver 启动)
  │
  ├─ [Project: 料仓B数据采集] (基于 Service data-collector)
  │  └─ stdiolink_service <svc_dir_A> --config-file=<projects/proj_2/config.json>
  │       └─ modbus_driver (孙进程)
  │
  └─ [Project: 设备监控] (基于 Service device-monitor)
     └─ stdiolink_service <svc_dir_B> --config-file=<projects/proj_3/config.json>
          ├─ modbus_driver (孙进程)
          └─ vision_driver (孙进程)
```

- 管理器是唯一的常驻进程
- 同一 Service 可创建多个 Project，各自持有独立配置
- 每个 Project Instance 是一个独立的 `stdiolink_service` 子进程
- Driver 由 Service 的 JS 脚本通过 `openDriver()` 按需启动
- 管理器不直接与 Driver 通信，仅管理 Driver 的注册信息

---

## 5. Driver 管理

### 5.1 设计思路

Driver 是无状态的可执行程序，不需要配置文件。管理器对 Driver 的管理聚焦于**注册发现**和**元数据缓存**，不涉及 Driver 的运行时状态。

Service 的 JS 脚本在执行过程中通过 `openDriver()` 自行启动 Driver 进程，管理器仅提供 Driver 的路径和元数据信息供 Service 引用。

### 5.2 Driver 注册信息

```json
{
  "id": "calculator_driver",
  "program": "/opt/drivers/calculator_driver",
  "description": "数学计算驱动",
  "tags": ["math", "demo"],
  "meta": { /* 缓存的 DriverMeta，来自 meta.describe */ },
  "registeredAt": "2026-02-08T10:00:00Z"
}
```

### 5.3 Driver 发现机制

复用现有 `DriverRegistry::scanDirectory()` 逻辑，支持三种注册方式：

| 方式 | 说明 |
|------|------|
| 手动注册 | 通过 API 指定可执行文件路径 |
| 目录扫描 | 扫描指定目录下的可执行文件，自动发现 |
| 元数据文件 | 读取 `driver.meta.json` 静态文件（无需启动进程） |

### 5.4 元数据缓存

- 首次注册时启动 Driver 进程，发送 `meta.describe` 命令获取元数据
- 元数据持久化到 SQLite
- 后续访问直接读取缓存，不启动进程
- 提供手动刷新 API，按需重新获取元数据

### 5.5 健康检查

- 注册时验证 Driver 可执行文件存在且可执行
- 提供手动刷新 API，按需检查 Driver 可用性
- 不做定期自动检查

---

## 6. Service 管理

### 6.1 设计思路

Service 是只读的服务模板。每个 Service 对应一个标准服务目录，管理器负责其注册、发现和 Schema 缓存。Service 本身不持有运行时配置和调度规则——这些归属于基于 Service 创建的 Project。

### 6.2 Service 注册信息

```json
{
  "manifestVersion": "1",
  "id": "com.example.data-collector",
  "name": "数据采集服务",
  "version": "1.0.0",
  "serviceDir": "/data/services/data-collector",
  "description": "定时采集 Modbus 设备数据",
  "tags": ["modbus", "collector"],
  "status": "available",
  "registeredAt": "2026-02-08T10:00:00Z"
}
```

### 6.3 Service 目录结构

管理器管理的 Service 遵循 M29-M32 标准目录结构：

```
/data/services/data-collector/
  ├── manifest.json           # 服务元数据（manifestVersion, id, name, version, description, author）
  ├── index.js                # 入口脚本
  └── config.schema.json      # 配置 Schema 声明
```

### 6.4 Service 注册流程

```
API: POST /api/services
        │
        ▼
  验证服务目录（manifest.json / index.js / config.schema.json 均存在）
        │
        ▼
  解析 manifest.json → 提取 manifestVersion, id, name, version
        （注意：未知字段会报错，manifestVersion 必须为 "1"）
        │
        ▼
  解析 config.schema.json → 缓存 Schema
        │
        ▼
  写入 SQLite（services 表）
        │
        ▼
  返回 Service 注册信息
```

### 6.5 Service 状态

| 状态 | 说明 |
|------|------|
| `available` | 已注册，服务目录和文件完整，可用于创建 Project |
| `error` | 服务目录或文件异常（manifest.json 缺失等），需人工修复 |

---

## 7. Project 管理

### 7.1 设计思路

Project 是三层模型的核心运行单元。每个 Project 基于一个 Service 模板创建，持有独立的配置（config.json）和工作目录（workspace）。调度规则和执行实例都绑定到 Project 而非 Service。

这使得同一个 Service 模板可以创建多个 Project，各自拥有不同的配置参数。例如"激光雷达扫描"Service 可以为不同料仓创建多个 Project，每个 Project 配置不同的雷达 IP 和料仓形状参数。

### 7.2 Project 注册信息

```json
{
  "id": "proj-silo-a-scan",
  "name": "料仓A激光扫描",
  "serviceId": "com.example.lidar-scan",
  "description": "料仓A的激光雷达扫描任务",
  "enabled": true,
  "tags": ["lidar", "silo-a"],
  "createdAt": "2026-02-08T10:00:00Z",
  "updatedAt": "2026-02-08T10:00:00Z"
}
```

### 7.3 Project 创建流程

```
API: POST /api/v1/projects
        │
        ▼
  校验 serviceId → 确认 Service 存在且状态为 available
        │
        ▼
  创建 Project 目录: <data_root>/projects/<project_id>/
        │
        ▼
  若请求包含初始配置 → 校验配置符合 Schema → 合并 Schema 默认值
        │                                        │
        ▼                                        ▼
  否则使用 Schema 默认值生成完整配置     写入 config.json 到 Project 目录
        │                                        │
        ├────────────────────────────────────────┘
        ▼
  创建 workspace 子目录: <data_root>/projects/<project_id>/workspace/
        │
        ▼
  写入 SQLite（projects 表）
        │
        ▼
  返回 Project 注册信息
```

**关键点**：config.json 在 Project 创建时即持久化到磁盘，后续 Instance 启动时直接引用，不再每次生成临时文件。

### 7.4 配置更新流程

```
API: PUT /api/v1/projects/{id}/config
        │
        ▼
  校验配置符合 Service 的 config.schema.json
        │
        ▼
  合并 Schema 默认值
        │
        ▼
  覆盖写入 <data_root>/projects/<project_id>/config.json
        │
        ▼
  更新 SQLite（projects 表的 updated_at）
        │
        ▼
  返回更新后的配置
```

### 7.5 Project 状态

| 状态 | 说明 |
|------|------|
| `enabled` | 已启用，调度引擎会按规则触发 |
| `disabled` | 已禁用，调度引擎跳过 |
| `error` | 配置异常或关联的 Service 不可用，需人工修复 |

### 7.6 Service 与 Project 的关系

```
Service: com.example.lidar-scan (只读模板)
  │
  ├── Project: proj-silo-a-scan (料仓A)
  │     ├── config.json  → { "lidarIp": "192.168.1.10", "shape": "cylinder", ... }
  │     ├── workspace/
  │     └── Schedule: fixed_rate 5000ms
  │
  ├── Project: proj-silo-b-scan (料仓B)
  │     ├── config.json  → { "lidarIp": "192.168.1.11", "shape": "cone", ... }
  │     ├── workspace/
  │     └── Schedule: fixed_rate 5000ms
  │
  └── Project: proj-silo-c-scan (料仓C)
        ├── config.json  → { "lidarIp": "192.168.1.12", "shape": "box", ... }
        ├── workspace/
        └── Schedule: cron "0 */2 * * *"
```

- 删除 Service 前需先删除或迁移其所有 Project
- Project 创建后可独立管理，不受 Service 模板变更影响
- Service 更新（如新版本 index.js）不自动影响已有 Project 的配置

---

## 8. 配置系统

### 8.1 设计思路

配置系统是服务管理器的核心能力之一。目标是：

1. 复用现有 `config.schema.json` 声明机制（M28/M31）
2. 配置归属于 Project，在创建/更新时持久化到 Project 目录下的 config.json
3. Instance 启动时直接引用 Project 目录下已有的 config.json，不再每次生成临时文件
4. 预留配置插件扩展点（`ui` 属性），当前阶段不实现

### 8.2 配置数据流

```
config.schema.json          用户配置 (Web UI)
      │                          │
      ▼                          ▼
  Schema 解析             用户填写/编辑
      │                          │
      ▼                          ▼
  动态生成表单 ──────────► 配置校验 (ServiceConfigValidator)
                                 │
                                 ▼
                    写入 Project 目录下 config.json（创建/更新时持久化）
                                 │
                                 ▼
              stdiolink_service <svc_dir> --config-file=<projects/proj_id/config.json>
                                 │
                                 ▼
                    JS 脚本通过 getConfig() 读取
```

**Schema 解析注意事项（与现有实现对齐）**：

- 约束字段必须放在 `constraints` 对象中（如 `min/max/minLength/maxLength/enumValues`），顶层同名字段会被忽略。
- `enum` 类型的可选值应写在 `constraints.enumValues` 中。
- `array` 类型当前仅解析 `items.type` 与 `items.constraints`，不解析 `items.fields`（数组元素为对象时的子字段）。

### 8.3 配置优先级

只保留两层，简单明确：

```
用户配置（Web UI 填写 / API 提交）
         ↓
Schema 默认值（config.schema.json 中的 default）
```

- 用户配置覆盖 Schema 默认值
- 对象类型字段深度合并，标量/数组类型整体替换
- 未填写的字段使用 Schema 默认值
- 配置中出现 Schema 未定义的字段会被判定为非法（reject）

### 8.4 配置插件（预留扩展点）

Schema 中的 `ui` 属性为后续配置插件预留扩展点。当前阶段前端仅根据字段 `type` 渲染标准表单组件，`ui` 属性由前端忽略。
注意：现有 `ServiceConfigSchema` 解析并不会读取 `ui` 字段（也不会保留），如果管理器直接复用该解析器，则 `ui` 信息会丢失。若需要使用 `ui`，需扩展解析逻辑后再启用相关前端功能。

**预留声明格式**：

```json
{
  "deviceAddress": {
    "type": "string",
    "description": "设备地址",
    "ui": {
      "plugin": "device-selector",
      "options": { "protocol": "modbus-tcp" }
    }
  }
}
```

后续可按需实现具体插件组件（如文件浏览器、Cron 编辑器等），前端根据 `ui.plugin` 名称选择对应组件，产出值仍为标准 JSON。

### 8.5 动态 UI 生成

管理器通过 `config.schema.json` 自动生成配置表单，复用现有 `FieldMeta` 类型系统（M7-M11）：

| Schema 字段属性 | UI 映射 |
|----------------|---------|
| `type: "string"` | 文本输入框 |
| `type: "int"` / `"double"` | 数字输入框（带 min/max 约束） |
| `type: "bool"` | 开关/复选框 |
| `type: "enum"` + `constraints.enumValues` | 下拉选择框 |
| `type: "array"` | 动态列表编辑器 |
| `type: "object"` | 嵌套表单分组 |
| `required: true` | 必填标记 |
| `description` | 字段说明提示 |
| `ui.plugin` | 替换为对应插件组件 |
| `ui.group` | 字段分组标签页 |

**API 端点**：`GET /api/v1/services/{id}/config-schema` 返回**规范化** Schema JSON（`fields` 数组结构，而非原始 `config.schema.json` 的键值对象结构），前端据此渲染表单。

### 8.6 配置与 Driver 的关系

Driver 本身无状态、无配置。Service 的 JS 脚本通过 `getConfig()` 获取配置后，在调用 `openDriver()` 时将相关参数作为命令行参数传入 Driver：

```javascript
import { getConfig, openDriver } from 'stdiolink';

const config = getConfig();
const driver = await openDriver(config.driverPath, [
    '--host=' + config.deviceHost,
    '--port=' + String(config.devicePort)
]);
const result = await driver.readRegisters({ start: 0, count: 10 });
```

这样 Driver 保持无状态，所有配置由 Service 层控制和传递。

---

## 9. 调度系统

### 9.1 设计思路

调度系统是管理器的执行引擎，负责按规则触发 Project 实例。设计上将**调度类型**与**执行策略**分离，通过组合实现丰富的调度行为，同时保持每个维度的实现简单。

### 9.2 调度类型

| 类型 | 说明 | 典型场景 |
|------|------|---------|
| `once` | 单次执行，触发后立即运行 | 手动测试、一次性任务 |
| `fixed_rate` | 固定速率，从上次**开始**时间算起 | 定时采集（每 5 秒一次） |
| `fixed_delay` | 固定延迟，从上次**结束**时间算起；`intervalMs=0` 时等价于连续执行 | 依赖上次结果的串行任务、持续轮询 |
| `cron` | 标准 Cron 表达式（5 字段） | 每天凌晨 2 点执行 |
| `daemon` | 常驻进程，异常退出后自动重启 | 长连接服务、WebSocket 监听 |

### 9.3 执行策略

执行策略独立于调度类型，通过组合配置：

| 策略 | 默认值 | 说明 |
|------|--------|------|
| `maxConcurrent` | 1 | 同一 Project 最大并发实例数 |
| `overlapPolicy` | `skip` | 触发时已有实例运行的处理方式 |
| `timeoutMs` | 0（无限） | 单次执行超时，超时后终止进程 |
| `maxRetries` | 0 | 失败后最大重试次数 |
| `retryDelayMs` | 5000 | 重试间隔（固定） |

**重叠策略（overlapPolicy）**：

| 值 | 行为 |
|----|------|
| `skip` | 跳过本次触发，记录 skip 事件 |
| `terminate` | 终止正在运行的实例，启动新实例 |

### 9.4 调度规则配置示例

```json
{
  "projectId": "proj-silo-a-collector",
  "type": "fixed_rate",
  "intervalMs": 5000,
  "execution": {
    "maxConcurrent": 1,
    "overlapPolicy": "skip",
    "timeoutMs": 30000,
    "maxRetries": 2,
    "retryDelayMs": 3000
  }
}
```

```json
{
  "projectId": "proj-daily-report",
  "type": "cron",
  "cron": "0 2 * * *",
  "execution": {
    "timeoutMs": 600000,
    "maxRetries": 1,
    "retryDelayMs": 60000
  }
}
```

```json
{
  "projectId": "proj-device-monitor",
  "type": "daemon",
  "execution": {
    "maxRetries": -1,
    "retryDelayMs": 5000
  }
}
```

### 9.5 调度引擎实现

调度引擎基于 `QTimer` 驱动，单线程轮询：

```
QTimer (1s tick)
     │
     ▼
遍历所有 enabled 的 Project 的 ScheduleRule
     │
     ├─ 检查是否到达触发时间
     ├─ 检查重叠策略
     ├─ 检查并发限制
     │
     ▼
满足条件 → 通知 Instance Runner 启动
不满足   → 记录 skip 事件并跳过
```

**各调度类型的触发判断**：

| 类型 | 触发条件 |
|------|---------|
| `once` | 未执行过 |
| `fixed_rate` | `now - lastStartTime >= intervalMs` |
| `fixed_delay` | `now - lastEndTime >= intervalMs`（`intervalMs=0` 时上一实例结束即触发） |
| `cron` | Cron 表达式匹配当前时间 |
| `daemon` | 无运行中实例（含重启延迟） |

---

## 10. 实例运行与生命周期

### 10.1 实例状态机

```
         ┌──────────┐
         │ starting  │
         └────┬─────┘
              │ 进程启动成功
              ▼
         ┌──────────┐
    ┌────│ running   │────┐
    │    └──────────┘    │
    │         │          │
    │    正常退出(0)   异常退出
    │         │          │
    │         ▼          ▼
    │   ┌──────────┐ ┌──────────┐
    │   │ finished │ │  failed  │
    │   └──────────┘ └──────────┘
    │
    │ 超时
    ▼
┌──────────┐
│ timeout  │
└──────────┘
```

### 10.2 状态定义

| 状态 | 说明 |
|------|------|
| `starting` | 进程正在启动，尚未确认运行 |
| `running` | 进程已启动并正在执行 |
| `finished` | 进程正常退出（exit code 0） |
| `failed` | 进程异常退出（exit code != 0）或启动失败 |
| `timeout` | 执行超时，进程被强制终止 |

### 10.3 Instance Runner 职责

- 准备运行环境：设置工作目录、引用 Project 目录下已有的 config.json
- 启动 `stdiolink_service` 子进程（QProcess）
- 捕获 stderr 输出，通过 spdlog 写入 Project 日志文件
- 监控进程退出状态，更新 Instance 记录
- 超时控制：QTimer 到期后发送 terminate，等待 graceful 退出，超时后 kill

### 10.4 启动恢复策略

管理器启动时扫描数据库中的 Instance 记录：

| 发现状态 | 处理方式 |
|---------|---------|
| `starting` | 标记为 `failed`，记录 `exit_reason = "server_restart"` |
| `running` | 标记为 `failed`，记录 `exit_reason = "server_restart"` |
| `finished` / `failed` / `timeout` | 保持不变 |

恢复完成后，调度引擎正常启动，按规则重新触发。

### 10.5 日志收集

- Instance 的 stderr 输出通过 Qt 日志系统（qDebug/qWarning/qCritical）转发到 spdlog
- spdlog 使用 rotating file sink，写入 Project 目录下的 `logs/` 子目录
- 日志文件命名：`<instance_id>.log`，单文件上限可配置（默认 10MB），保留文件数可配置（默认 5 个）
- 每条日志包含时间戳、级别、内容
- 提供 API 读取日志文件内容（分页）
- 日志保留策略：按文件轮转自动清理，无需额外定时任务

---

## 11. 文件与存储系统

### 11.1 目录布局

```
<data_root>/
  ├── server_manager.db          # SQLite 数据库
  ├── drivers/                   # Driver 可执行文件目录（可配置多个扫描路径）
  ├── services/                  # Service 只读模板目录
  │   ├── data-collector/
  │   │   ├── manifest.json
  │   │   ├── index.js
  │   │   └── config.schema.json
  │   └── daily-report/
  │       ├── manifest.json
  │       ├── index.js
  │       └── config.schema.json
  ├── projects/                  # Project 独立目录（每个 Project 一个子目录）
  │   ├── proj-silo-a-collector/
  │   │   ├── config.json        # 持久化的运行时配置
  │   │   ├── logs/              # 实例日志（spdlog rotating files）
  │   │   └── workspace/         # 独立工作目录
  │   │       ├── output/
  │   │       └── cache/
  │   └── proj-daily-report/
  │       ├── config.json
  │       ├── logs/
  │       └── workspace/
  │           └── reports/
  ├── shared/                    # 全局共享读写目录
  │   ├── data/
  │   └── temp/
  └── logs/                      # 管理器自身日志
```

### 11.2 Workspace 隔离

每个 Project 实例启动时，管理器将 QProcess 的工作目录设置为对应 Project 的 workspace 路径：

```cpp
process.setWorkingDirectory(workspacePath);  // <data_root>/projects/<project_id>/workspace/
```

JS 脚本中的相对路径文件操作自然隔离到各自目录。

### 11.3 共享目录

`shared/` 目录对所有 Project 可见。管理器通过环境变量注入路径：

| 环境变量 | 值 |
|---------|---|
| `STDIOLINK_WORKSPACE` | `<data_root>/projects/<project_id>/workspace` |
| `STDIOLINK_SHARED` | `<data_root>/shared` |
| `STDIOLINK_DATA_ROOT` | `<data_root>` |

> **注意**：当前 JS 运行时未暴露读取环境变量的 API（无 `process.env`），这些环境变量主要供 JS 脚本通过 `exec()` 启动的子进程继承读取，或由后续扩展提供直接访问能力。

### 11.4 SQLite 写入策略

- 所有写操作通过单线程队列串行执行，避免 SQLite 并发写入问题
- 读操作可并发（SQLite WAL 模式）
- 实例日志不写入 SQLite，由 spdlog 直接写入文件

---

## 12. Web API 设计

### 12.1 总体设计

- RESTful 风格，JSON 请求/响应
- 基础路径：`/api/v1/`
- 错误响应统一格式：`{ "error": "message" }`
- 分页参数：`?page=1&pageSize=20`
- WebSocket 端点：`/ws` 用于实时事件推送（Phase 2 实现，Phase 1 使用 HTTP 轮询）

### 12.2 Driver API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/v1/drivers` | 列出所有已注册 Driver |
| POST | `/api/v1/drivers` | 注册 Driver（指定路径） |
| GET | `/api/v1/drivers/{id}` | 获取 Driver 详情（含元数据） |
| PUT | `/api/v1/drivers/{id}` | 更新 Driver 信息 |
| DELETE | `/api/v1/drivers/{id}` | 注销 Driver |
| POST | `/api/v1/drivers/{id}/refresh-meta` | 刷新元数据缓存 |
| POST | `/api/v1/drivers/scan` | 扫描目录发现 Driver |

### 12.3 Service API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/v1/services` | 列出所有 Service 模板 |
| POST | `/api/v1/services` | 注册 Service（指定服务目录） |
| GET | `/api/v1/services/{id}` | 获取 Service 详情（含 Schema） |
| PUT | `/api/v1/services/{id}` | 更新 Service 信息 |
| DELETE | `/api/v1/services/{id}` | 注销 Service |
| GET | `/api/v1/services/{id}/config-schema` | 获取配置 Schema |
| GET | `/api/v1/services/{id}/projects` | 列出基于此 Service 的所有 Project |

### 12.4 Project API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/v1/projects` | 列出所有 Project |
| POST | `/api/v1/projects` | 创建 Project（指定 serviceId + 初始配置） |
| GET | `/api/v1/projects/{id}` | 获取 Project 详情 |
| PUT | `/api/v1/projects/{id}` | 更新 Project 信息 |
| DELETE | `/api/v1/projects/{id}` | 删除 Project |
| POST | `/api/v1/projects/{id}/enable` | 启用 Project |
| POST | `/api/v1/projects/{id}/disable` | 禁用 Project |

### 12.5 Config API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/v1/projects/{id}/config` | 获取 Project 当前配置 |
| PUT | `/api/v1/projects/{id}/config` | 更新 Project 配置（校验后保存并持久化到磁盘） |
| POST | `/api/v1/projects/{id}/config/validate` | 仅校验，不保存 |

### 12.6 Schedule API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/v1/projects/{id}/schedule` | 获取调度规则 |
| PUT | `/api/v1/projects/{id}/schedule` | 设置/更新调度规则 |
| DELETE | `/api/v1/projects/{id}/schedule` | 删除调度规则 |
| POST | `/api/v1/projects/{id}/trigger` | 手动触发一次执行 |

### 12.7 Instance API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/v1/projects/{id}/instances` | 列出 Project 的执行历史 |
| GET | `/api/v1/instances/{instanceId}` | 获取实例详情 |
| POST | `/api/v1/instances/{instanceId}/stop` | 终止运行中的实例 |
| GET | `/api/v1/instances/{instanceId}/logs` | 获取实例日志（从日志文件读取，支持分页） |

### 12.8 WebSocket 事件（Phase 2）

> Phase 1 使用 HTTP 轮询获取实例状态和日志，Phase 2 实现 WebSocket 实时推送。

连接 `/ws` 后，服务端推送以下事件：

| 事件类型 | 说明 |
|---------|------|
| `instance.started` | 实例启动 |
| `instance.finished` | 实例正常结束 |
| `instance.failed` | 实例异常退出 |
| `instance.timeout` | 实例超时 |
| `instance.log` | 实例日志行 |
| `schedule.skipped` | 调度触发被跳过（重叠） |

事件格式：

```json
{
  "type": "instance.log",
  "timestamp": "2026-02-08T12:00:00.123Z",
  "data": {
    "instanceId": "inst-001",
    "projectId": "proj-silo-a-collector",
    "level": "info",
    "message": "Connected to device 192.168.1.100"
  }
}
```

---

## 13. Web UI 设计

### 13.1 技术方案

- 静态 SPA 前端，构建产物嵌入管理器二进制或从指定目录加载
- 管理器内置静态文件服务，访问 `/` 返回前端页面
- 前端通过 `/api/v1/` 调用后端（Phase 1 使用 HTTP 轮询，Phase 2 通过 `/ws` 接收实时事件）

### 13.2 页面规划

| 页面 | 功能 |
|------|------|
| Dashboard | 总览：Project 数量、运行中实例、最近事件 |
| Drivers | Driver 列表、注册、详情（元数据展示）、健康状态 |
| Services | Service 模板列表、注册、只读查看、"创建 Project" 入口 |
| Projects | Project 列表、启停、状态指示 |
| Project Detail | 配置编辑（动态表单）、调度规则、执行历史、实时日志 |
| Logs | 全局日志查看、按 Project/Instance 过滤 |

### 13.3 动态配置表单

前端根据 `config-schema` API 返回的 Schema 自动渲染表单：

1. 遍历 Schema 字段，按 `type` 选择对应输入组件
2. 检查 `ui` 相关属性（如 `ui.plugin`/`ui.group`），若后端解析已支持则应用插件/分组
3. 按 `ui.group` 分组显示（标签页或折叠面板）
4. `required` 字段标记必填，`description` 显示为提示文字
5. 提交时调用 `validate` API 预校验，通过后保存

---

## 14. 数据模型

### 14.1 drivers 表

```sql
CREATE TABLE drivers (
    id            TEXT PRIMARY KEY,
    program       TEXT NOT NULL,
    description   TEXT DEFAULT '',
    tags          TEXT DEFAULT '[]',
    meta_json     TEXT,
    registered_at TEXT NOT NULL
);
```

### 14.2 services 表

```sql
CREATE TABLE services (
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

### 14.3 projects 表

```sql
CREATE TABLE projects (
    id            TEXT PRIMARY KEY,
    name          TEXT NOT NULL,
    service_id    TEXT NOT NULL REFERENCES services(id),
    description   TEXT DEFAULT '',
    tags          TEXT DEFAULT '[]',
    status        TEXT DEFAULT 'enabled',
    created_at    TEXT NOT NULL,
    updated_at    TEXT NOT NULL
);

CREATE INDEX idx_projects_service ON projects(service_id);
```

### 14.4 schedules 表

```sql
CREATE TABLE schedules (
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

### 14.5 instances 表

```sql
CREATE TABLE instances (
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

### 14.6 日志存储

实例日志不存入 SQLite，改为文件存储：

- 日志路径：`<data_root>/projects/<project_id>/logs/<instance_id>.log`
- 使用 spdlog rotating file sink，单文件上限 10MB，保留 5 个轮转文件
- Qt 日志（qDebug/qWarning/qCritical）通过自定义 message handler 转发到 spdlog

---

## 15. 与现有代码的集成

### 15.1 复用清单

| 现有模块 | 复用方式 |
|---------|---------|
| `DriverRegistry` | Driver Manager 直接调用，扩展持久化到 SQLite |
| `Driver` / `Task` | 健康检查时启动 Driver 发送 `meta.describe` |
| `ServiceDirectory` | 注册 Service 时复用目录校验逻辑 |
| `ServiceManifest` | 注册 Service 时复用 manifest 解析 |
| `ServiceConfigSchema` | 复用 Schema 解析，生成 UI 描述 |
| `ServiceConfigValidator` | 复用配置校验与合并逻辑 |
| `meta::FieldMeta` | 复用字段类型系统，驱动动态表单生成 |
| `PlatformUtils` | 复用平台抽象（可执行文件路径、控制台编码等） |

### 15.2 不修改现有代码

管理器作为独立可执行文件，链接 `stdiolink` 库和 `stdiolink_service` 的配置模块。不修改现有 `src/stdiolink/` 和 `src/stdiolink_service/` 的任何代码。

如需扩展（如 DriverRegistry 持久化），通过继承或组合在管理器侧实现。

### 15.3 Instance Runner 启动流程

管理器启动 Project 实例时的具体步骤：

```
1. 从 Project 目录读取已持久化的 config.json
2. 构造启动命令：
   stdiolink_service <service_dir> --config-file=<projects>/<project_id>/config.json
3. 设置环境变量：
   STDIOLINK_WORKSPACE=<projects>/<project_id>/workspace
   STDIOLINK_SHARED=<shared>
   STDIOLINK_PROJECT_ID=<project_id>
4. 设置工作目录为 Project 的 workspace 路径
5. 通过 QProcess 启动子进程
6. 连接 QProcess::finished 信号，更新 Instance 状态
7. 连接 QProcess::readyReadStandardError 信号，通过 spdlog 写入日志文件
```

---

## 16. 分阶段实施路线

### Phase 1：核心闭环

**目标**：最小可用的服务管理器，能注册、配置、调度、执行 Project。

| 模块 | 内容 |
|------|------|
| Storage | SQLite 初始化、表创建、单线程写入队列 |
| Driver Manager | CRUD、目录扫描、元数据缓存 |
| Service Manager | 只读模板注册/注销/发现、Schema 缓存 |
| Project Manager | CRUD、配置持久化、启停控制 |
| Config Manager | Schema 解析、用户配置 CRUD、合并校验 |
| Schedule Engine | `once` / `fixed_rate` / `cron` 三种调度 |
| Instance Runner | QProcess 启动/监控/终止、spdlog 文件日志 |
| API Server | 上述模块的 RESTful API |

### Phase 2：可用性增强

**目标**：基础 Web UI、补全调度类型。

| 模块 | 内容 |
|------|------|
| Schedule Engine | 补充 `fixed_delay` / `daemon` |
| Web UI | Dashboard、Driver/Service/Project 列表、配置表单、日志查看 |
| WebSocket | 实时事件推送（实例状态、日志） |

### Phase 3：扩展能力

**目标**：配置插件实现、权限认证、Service 包管理。

| 模块 | 内容 |
|------|------|
| Config Plugin | 基于 `ui.plugin` 扩展点实现插件组件（file-browser、cron-editor 等） |
| 权限认证 | 基础 Token 认证、API 访问控制 |
| Service 包管理 | Service 打包/导入/导出、版本管理 |
| 日志管理 | 日志归档、跨 Project 日志搜索、保留策略增强 |

### Phase 4：高级功能

**目标**：在线编辑、审计、高级监控。

| 模块 | 内容 |
|------|------|
| 在线 JS 编辑器 | Web UI 内嵌代码编辑器、语法高亮、保存/回滚 |
| 审计日志 | 操作审计记录（谁在何时做了什么） |
| 监控增强 | Project 资源占用统计、历史趋势图表 |
| 插件签名 | 第三方插件签名校验、沙箱执行 |

---

## 附录

### A. 配置 Schema 完整示例

```json
{
  "device": {
    "type": "object",
    "description": "设备连接参数",
    "fields": {
      "host": {
        "type": "string",
        "description": "设备 IP 地址",
        "default": "192.168.1.100",
        "required": true
      },
      "port": {
        "type": "int",
        "description": "端口号",
        "default": 502,
        "constraints": { "min": 1, "max": 65535 }
      },
      "slaveId": {
        "type": "int",
        "description": "从站地址",
        "default": 1,
        "constraints": { "min": 0, "max": 247 }
      }
    }
  },
  "polling": {
    "type": "object",
    "description": "轮询参数",
    "fields": {
      "intervalMs": {
        "type": "int",
        "description": "采集间隔（毫秒）",
        "default": 1000,
        "constraints": { "min": 100 }
      },
      "registers": {
        "type": "array",
        "description": "寄存器地址列表",
        "items": {
          "type": "int",
          "constraints": { "min": 0, "max": 65535 }
        }
      }
    }
  },
  "output": {
    "type": "object",
    "description": "输出设置",
    "fields": {
      "format": {
        "type": "enum",
        "description": "输出格式",
        "constraints": { "enumValues": ["json", "csv"] },
        "default": "json"
      },
      "filePath": {
        "type": "string",
        "description": "输出文件路径",
        "default": "data/output.json"
      }
    }
  }
}
```

### B. 调度规则完整示例集

**固定速率采集**：

```json
{
  "projectId": "proj-silo-a-collector",
  "type": "fixed_rate",
  "intervalMs": 5000,
  "execution": {
    "maxConcurrent": 1,
    "overlapPolicy": "skip",
    "timeoutMs": 30000,
    "maxRetries": 2,
    "retryDelayMs": 3000
  }
}
```

**Cron 定时报表**：

```json
{
  "projectId": "proj-daily-report",
  "type": "cron",
  "cron": "0 2 * * *",
  "execution": {
    "timeoutMs": 600000,
    "maxRetries": 1,
    "retryDelayMs": 60000
  }
}
```

**常驻守护进程**：

```json
{
  "projectId": "proj-device-monitor",
  "type": "daemon",
  "execution": {
    "maxRetries": -1,
    "retryDelayMs": 5000
  }
}
```

**单次手动执行**：

```json
{
  "projectId": "proj-migration-tool",
  "type": "once",
  "execution": {
    "timeoutMs": 3600000
  }
}
```

### C. Service JS 脚本示例

```javascript
import { getConfig, openDriver, log } from 'stdiolink';

const config = getConfig();

// 根据配置打开 Driver
const driver = await openDriver(config.driverPath, [
    '--host=' + config.device.host,
    '--port=' + String(config.device.port),
    '--slave=' + String(config.device.slaveId)
]);

// 按配置的寄存器地址列表采集数据
for (const addr of config.polling.registers) {
    const result = await driver.readRegisters({ start: addr, count: 1 });
    log.info(`address=${addr} value=${JSON.stringify(result.values)}`);
}

// 关闭 Driver
await driver.close();
```

### D. 管理器启动命令行参数

```
server_manager [选项]

选项：
  --data-root=<path>       数据根目录（默认: ./data）
  --port=<port>            HTTP 服务端口（默认: 8080）
  --host=<addr>            监听地址（默认: 127.0.0.1）
  --driver-dirs=<paths>    Driver 扫描目录，逗号分隔
  --log=<file>             日志输出文件
  --log-level=<level>      日志级别: debug/info/warning/error
  --help                   显示帮助信息
  --version                显示版本号
```

### E. API 错误码参考

| HTTP 状态码 | 错误场景 |
|-------------|---------|
| 400 | 请求参数校验失败、配置不符合 Schema |
| 404 | Driver/Service/Project/Instance 不存在 |
| 409 | Service ID 已存在、Driver ID 已注册、Project ID 已存在 |
| 422 | 服务目录结构不完整（缺少 manifest.json 等） |
| 500 | 内部错误（数据库异常、进程启动失败等） |

错误响应统一格式：

```json
{
  "error": "Service directory missing manifest.json",
  "code": "INVALID_SERVICE_DIR",
  "details": {
    "path": "/data/services/broken-service",
    "missing": ["manifest.json"]
  }
}
```

### F. 环境变量参考

| 环境变量 | 说明 | 注入对象 |
|---------|------|---------|
| `STDIOLINK_WORKSPACE` | Project 独立工作目录路径 | stdiolink_service 子进程 |
| `STDIOLINK_SHARED` | 全局共享目录路径 | stdiolink_service 子进程 |
| `STDIOLINK_DATA_ROOT` | 数据根目录路径 | stdiolink_service 子进程 |
| `STDIOLINK_SERVICE_ID` | 当前 Service 模板的 ID | stdiolink_service 子进程 |
| `STDIOLINK_PROJECT_ID` | 当前 Project 的 ID | stdiolink_service 子进程 |
| `STDIOLINK_INSTANCE_ID` | 当前执行实例的 ID | stdiolink_service 子进程 |
