# stdiolink 服务管理器 — 需求与开发设计文档

> 版本: 0.1.0-draft
> 日期: 2026-02-08
> 基于: stdiolink 核心库 + stdiolink_service JS 运行时

---

## 目录

1. [概述](#1-概述)
2. [术语定义](#2-术语定义)
3. [系统架构](#3-系统架构)
4. [Driver 管理](#4-driver-管理)
5. [Service 管理](#5-service-管理)
6. [配置系统](#6-配置系统)
7. [调度系统](#7-调度系统)
8. [文件与目录隔离](#8-文件与目录隔离)
9. [Web API 设计](#9-web-api-设计)
10. [Web UI 设计](#10-web-ui-设计)
11. [扩展规划](#11-扩展规划)
12. [数据模型](#12-数据模型)
13. [实现路线](#13-实现路线)

---

## 1. 概述

### 1.1 背景

stdiolink 已实现完整的 Driver ↔ Host IPC 通信、JS Service 运行时、配置 Schema 声明与验证体系。在此基础上，需要一个**服务管理器（Server Manager）**来统一管理 Driver 注册、Service 编排、配置分发、任务调度，并通过 Web API + Web UI 提供可视化操作界面。

### 1.2 核心目标

| 目标 | 说明 |
|------|------|
| Driver 注册管理 | 注册、发现、增删改查 Driver 可执行文件，自动提取元数据 |
| Service 生命周期 | 创建、配置、启停、删除 Service，每个 Service 拥有独立目录 |
| 配置声明与分发 | 基于 config.schema.json 动态生成配置界面，支持配置模板和配置插件 |
| 强大的调度能力 | 固定速率、固定延迟、防重叠、连续执行、守护进程、Cron、单次、超时控制 |
| Web API + Web UI | RESTful API 管理所有资源，Web UI 提供交互操作 |
| 文件隔离 | 每个 Service 拥有独立工作目录，另有全局共享目录 |

### 1.3 设计原则

- **Driver 无状态**：Driver 是纯粹的命令执行器，不持有配置，所有参数通过命令行或运行时命令传入
- **Service 有状态**：Service 持有配置、调度策略、运行日志，是调度和管理的基本单元
- **配置驱动**：所有行为通过声明式配置定义，配置模板和插件提供可扩展性
- **最小侵入**：在现有 `stdiolink` 核心库和 `stdiolink_service` 运行时之上构建，不修改底层协议

---

## 2. 术语定义

| 术语 | 定义 |
|------|------|
| **Driver** | 实现 stdiolink JSONL 协议的可执行程序，无状态，通过 stdin/stdout 通信 |
| **Service** | 一个服务目录（manifest.json + index.js + config.schema.json），编排一个或多个 Driver 的调用逻辑 |
| **Service Instance** | Service 的一次运行实例，由调度器创建和管理 |
| **Config Template** | 预定义的配置值集合，可应用于多个 Service，覆盖 schema 默认值 |
| **Config Plugin** | 提供特殊 UI 组件的插件，用于生成定制化配置（如设备选择器、文件浏览器、地图坐标选取等） |
| **Schedule** | 调度策略，定义 Service 何时、如何、以什么频率执行 |
| **Server Manager** | 本文档描述的管理系统，提供 Web API 和 Web UI |

---

## 3. 系统架构

### 3.1 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         Web UI (SPA)                            │
│  Service 列表 │ Driver 列表 │ 配置编辑 │ 调度监控 │ 日志查看    │
└──────────────────────────┬──────────────────────────────────────┘
                           │ HTTP / WebSocket
┌──────────────────────────▼──────────────────────────────────────┐
│                    Server Manager (C++ / Qt)                    │
│                                                                 │
│  ┌─────────────┐ ┌─────────────┐ ┌──────────────┐ ┌──────────┐│
│  │ Web API     │ │ Driver      │ │ Service      │ │ Schedule ││
│  │ (REST +     │ │ Registry    │ │ Registry     │ │ Engine   ││
│  │  WebSocket) │ │             │ │              │ │          ││
│  └──────┬──────┘ └──────┬──────┘ └──────┬───────┘ └────┬─────┘│
│         │               │               │              │       │
│  ┌──────▼───────────────▼───────────────▼──────────────▼─────┐ │
│  │                  Core Services Layer                      │ │
│  │  ConfigManager │ TemplateEngine │ PluginHost │ FileStore  │ │
│  └───────────────────────────┬───────────────────────────────┘ │
│                              │                                  │
│  ┌───────────────────────────▼───────────────────────────────┐ │
│  │              stdiolink_service (JS Runtime)               │ │
│  │  JsEngine │ JsDriverBinding │ JsConfigBinding │ Scheduler│ │
│  └───────────────────────────┬───────────────────────────────┘ │
│                              │                                  │
│  ┌───────────────────────────▼───────────────────────────────┐ │
│  │                 stdiolink (Core Library)                   │ │
│  │  Driver (Host) │ Task │ Protocol │ Meta │ Validator       │ │
│  └───────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
         │                    │                    │
    ┌────▼────┐          ┌───▼────┐          ┌───▼────┐
    │ Driver  │          │ Driver │          │ Driver │
    │ Process │          │ Process│          │ Process│
    │ (无状态) │          │ (无状态)│          │ (无状态)│
    └─────────┘          └────────┘          └────────┘
```

### 3.2 模块划分

Server Manager 作为新的顶层应用，位于 `src/stdiolink_server/`：

```
src/stdiolink_server/
├── main.cpp                    # 入口
├── app/
│   └── server_app.h/cpp        # 应用初始化、信号处理
├── api/                        # Web API 层
│   ├── api_server.h/cpp        # HTTP 服务器 (基于 Qt HTTP Server 或轻量库)
│   ├── ws_server.h/cpp         # WebSocket 服务器 (实时事件推送)
│   ├── routes/
│   │   ├── driver_routes.h/cpp
│   │   ├── service_routes.h/cpp
│   │   ├── config_routes.h/cpp
│   │   ├── schedule_routes.h/cpp
│   │   ├── template_routes.h/cpp
│   │   └── log_routes.h/cpp
│   └── middleware/
│       ├── auth_middleware.h/cpp      # (扩展) 认证中间件
│       └── cors_middleware.h/cpp
├── registry/                   # 注册中心
│   ├── driver_registry.h/cpp   # Driver 注册与元数据缓存
│   └── service_registry.h/cpp  # Service 注册与状态管理
├── config/                     # 配置管理
│   ├── config_manager.h/cpp    # 配置合并、分发
│   ├── template_engine.h/cpp   # 配置模板引擎
│   └── plugin_host.h/cpp       # 配置插件宿主
├── scheduler/                  # 调度引擎
│   ├── schedule_engine.h/cpp   # 调度核心
│   ├── schedule_policy.h/cpp   # 调度策略定义
│   ├── instance_runner.h/cpp   # Service 实例运行器
│   └── timeout_guard.h/cpp     # 超时监控
├── storage/                    # 持久化
│   ├── db_store.h/cpp          # SQLite 存储
│   └── file_store.h/cpp        # 文件目录管理
└── webui/                      # 静态 Web UI 资源
    └── dist/                   # 前端构建产物
```

### 3.3 技术选型

| 组件 | 技术 | 理由 |
|------|------|------|
| HTTP Server | Qt HTTP Server (Qt 6.4+) 或 cpp-httplib | Qt 生态一致性，轻量无额外依赖 |
| WebSocket | Qt WebSockets | 实时事件推送（日志、状态变更） |
| 数据库 | SQLite (via Qt SQL) | 零部署，适合单机管理场景 |
| 前端 | Vue 3 + Vite | 轻量 SPA，动态表单生成能力强 |
| 配置插件 | 动态加载 QPlugin 或 JS 插件 | 可扩展的 UI 配置生成 |

---

## 4. Driver 管理

### 4.1 Driver 定位

Driver 是无状态的可执行程序，遵循 stdiolink JSONL 协议。Server Manager 需要知道 Driver 的位置和能力，但不为 Driver 维护配置。

**Driver 注册信息：**

| 字段 | 类型 | 说明 |
|------|------|------|
| id | string | 唯一标识符（自动生成或用户指定） |
| name | string | 显示名称（来自 DriverMeta 或用户指定） |
| path | string | 可执行文件绝对路径 |
| meta | DriverMeta | 自动提取的元数据（命令列表、参数 schema 等） |
| tags | string[] | 用户标签，便于分类筛选 |
| status | enum | registered / available / unavailable |
| registeredAt | datetime | 注册时间 |
| lastProbeAt | datetime | 最近一次元数据探测时间 |

### 4.2 Driver 注册流程

```
用户提供 Driver 路径
        │
        ▼
  检查文件是否存在且可执行
        │
        ▼
  启动 Driver 进程，发送 meta.describe
        │
        ▼
  解析 DriverMeta（命令、参数、版本等）
        │
        ▼
  写入 DriverRegistry（SQLite）
        │
        ▼
  标记 status = available
```

### 4.3 Driver CRUD 操作

| 操作 | 说明 |
|------|------|
| **注册** | 提供路径，自动探测元数据，写入注册表 |
| **查询** | 按 id / name / tag 查询，返回元数据摘要 |
| **更新** | 修改名称、标签；重新探测元数据 |
| **删除** | 从注册表移除（不删除可执行文件）；检查是否有 Service 引用 |
| **探测** | 重新启动 Driver 获取最新 meta.describe |
| **批量扫描** | 扫描指定目录，自动发现并注册所有 Driver |

### 4.4 Driver 与 Service 的关系

Driver 本身不需要配置。Service 的 JS 脚本通过 `openDriver(path)` 或 `new Driver()` 启动 Driver 进程时，可以传入命令行参数。这些参数由 Service 的配置决定：

```javascript
// index.js — Service 脚本示例
import { openDriver, getConfig } from 'stdiolink';

const cfg = getConfig();

// 方式1：通过启动参数传入
const driver = await openDriver(cfg.driverPath, {
    args: ['--profile=keepalive', `--precision=${cfg.precision}`]
});

// 方式2：通过命令在运行时传入
await driver.configure({ timeout: cfg.timeout, retries: cfg.retries });
```

### 4.5 底层对接

Driver 注册利用现有 `stdiolink::Driver`（Host 端）的能力：

- `driver.start(path, args)` — 启动进程
- `driver.queryMeta()` — 发送 `meta.describe` 获取 `DriverMeta`
- `DriverMeta.info` — 提取 id / name / version / vendor
- `DriverMeta.commands` — 提取命令列表及参数 schema
- `driver.terminate()` — 探测完成后关闭进程

---

## 5. Service 管理

### 5.1 Service 定义

Service 是调度和管理的基本单元。每个 Service 对应一个服务目录，包含：

```
services/{service-id}/
├── manifest.json           # 服务元信息
├── index.js                # 入口脚本（ES Module）
├── config.schema.json      # 配置 Schema 声明
├── config.json             # 当前生效的配置值（由管理器写入）
├── data/                   # 服务私有数据目录（运行时读写）
├── logs/                   # 服务日志目录
└── lib/                    # 可选的辅助 JS 模块
```

### 5.2 Service 注册信息

| 字段 | 类型 | 说明 |
|------|------|------|
| id | string | 唯一标识符（manifest.id） |
| name | string | 显示名称（manifest.name） |
| version | string | 版本号（manifest.version） |
| description | string | 描述 |
| dirPath | string | 服务目录绝对路径 |
| status | enum | stopped / running / error / disabled |
| scheduleId | string? | 关联的调度策略 ID |
| templateId | string? | 关联的配置模板 ID |
| configHash | string | 当前配置的哈希值（变更检测） |
| createdAt | datetime | 创建时间 |
| updatedAt | datetime | 最近修改时间 |

### 5.3 Service CRUD 操作

| 操作 | 说明 |
|------|------|
| **创建** | 指定 id，生成服务目录骨架（manifest + 空 index.js + 空 schema） |
| **导入** | 导入已有服务目录，解析 manifest 和 schema |
| **查询** | 按 id / name / status / tag 查询，支持分页 |
| **更新** | 修改 manifest 信息、更新脚本、更新 schema |
| **删除** | 停止运行实例，归档或删除服务目录 |
| **启动** | 根据调度策略启动 Service Instance |
| **停止** | 终止运行中的 Service Instance |
| **重启** | 停止后重新启动 |
| **克隆** | 复制服务目录为新 Service，生成新 id |

### 5.4 Service 生命周期状态机

```
                  create
                    │
                    ▼
    ┌──────────► stopped ◄──────────┐
    │               │               │
    │          start │          stop │
    │               ▼               │
    │           running ────────────┤
    │               │               │
    │         error  │       timeout │
    │               ▼               │
    │            error ─────────────┘
    │               │
    │       disable  │
    │               ▼
    └────────── disabled
```

### 5.5 Service 运行机制

Server Manager 通过 `stdiolink_service` 可执行文件运行 Service：

```cpp
// InstanceRunner 内部实现
QProcess proc;
QStringList args;
args << serviceDir;                              // 服务目录路径
args << "--config-file=" + configFilePath;        // 合并后的配置文件
proc.setWorkingDirectory(serviceDataDir);         // 工作目录设为 data/
proc.start(stdiolinkServicePath, args);
```

运行时环境变量注入：

| 环境变量 | 值 | 说明 |
|----------|-----|------|
| `STDIOLINK_SERVICE_ID` | service id | 当前服务 ID |
| `STDIOLINK_SERVICE_DIR` | 服务目录路径 | 服务根目录 |
| `STDIOLINK_DATA_DIR` | data/ 路径 | 服务私有数据目录 |
| `STDIOLINK_SHARED_DIR` | 全局共享目录路径 | 跨服务共享目录 |
| `STDIOLINK_LOG_DIR` | logs/ 路径 | 服务日志目录 |

---

## 6. 配置系统

### 6.1 配置层次模型

配置值的最终合并遵循优先级从高到低：

```
运行时 CLI 覆盖（--config.key=value）
        │
        ▼
  配置插件输出（Plugin 生成的定制值）
        │
        ▼
  用户手动编辑的配置值
        │
        ▼
  配置模板提供的值（Template）
        │
        ▼
  config.schema.json 中的 default 值
```

### 6.2 动态界面生成

现有 `FieldMeta` 已包含完整的 UI 渲染提示，Web UI 可直接利用：

| FieldMeta 字段 | UI 用途 |
|----------------|---------|
| `type` | 决定控件类型（文本框 / 数字框 / 开关 / 下拉 / 数组编辑器 / 对象编辑器） |
| `required` | 标记必填，前端校验 |
| `defaultValue` | 预填充值 |
| `description` | 字段说明文字 |
| `constraints.min/max` | 数字范围滑块或输入限制 |
| `constraints.minLength/maxLength` | 文本长度限制 |
| `constraints.pattern` | 正则校验提示 |
| `constraints.enumValues` | 下拉选项列表 |
| `constraints.maxItems` | 数组最大长度 |
| `ui.widget` | 指定控件类型覆盖（如 `textarea`、`color`、`slider`） |
| `ui.group` | 分组/折叠面板 |
| `ui.order` | 字段排列顺序 |
| `ui.placeholder` | 输入框占位文字 |
| `ui.advanced` | 折叠到"高级"区域 |
| `ui.readonly` | 只读展示 |
| `ui.unit` | 单位标签（如 "ms"、"MB"） |
| `ui.step` | 数字步进值 |
| `ui.visibleIf` | 条件显示表达式 |
| `fields` | 嵌套对象递归渲染子表单 |
| `items` | 数组元素 schema，渲染数组项编辑器 |

**前端渲染流程：**

```
GET /api/services/{id}/config-schema
        │
        ▼
  返回 FieldMeta[] JSON
        │
        ▼
  前端 SchemaFormRenderer 递归遍历
        │
        ├─ String → TextInput / Textarea / Select(enum)
        ├─ Int/Double → NumberInput / Slider
        ├─ Bool → Switch / Checkbox
        ├─ Enum → Select / RadioGroup
        ├─ Array → ArrayEditor (可增删项)
        ├─ Object → 嵌套 FormGroup
        └─ Plugin widget → 动态加载插件组件
```

### 6.3 配置模板（Config Template）

配置模板是预定义的配置值集合，可复用于多个 Service。

**模板结构：**

```json
{
    "id": "modbus-default",
    "name": "Modbus 默认配置",
    "description": "适用于标准 Modbus RTU 设备的默认参数",
    "targetSchemaId": "modbus-service",
    "values": {
        "baudRate": 9600,
        "dataBits": 8,
        "parity": "none",
        "stopBits": 1,
        "timeout": 3000
    },
    "tags": ["modbus", "serial"],
    "createdAt": "2026-02-08T10:00:00Z"
}
```

**模板操作：**

| 操作 | 说明 |
|------|------|
| 创建 | 从零创建或从现有 Service 配置导出 |
| 应用 | 将模板值合并到 Service 配置（模板值 < 用户编辑值） |
| 更新 | 修改模板值，可选择是否同步到已应用的 Service |
| 删除 | 移除模板，不影响已应用的 Service 当前配置 |
| 继承 | 模板可基于另一个模板扩展，形成继承链 |

**模板继承：**

```json
{
    "id": "modbus-fast",
    "name": "Modbus 高速配置",
    "extends": "modbus-default",
    "values": {
        "baudRate": 115200,
        "timeout": 1000
    }
}
```

合并规则：子模板值覆盖父模板值，未覆盖的字段继承父模板。

### 6.4 配置插件（Config Plugin）

配置插件为特定字段提供定制化的 UI 组件和值生成逻辑，解决标准表单控件无法满足的场景。

**插件声明（在 config.schema.json 中）：**

```json
{
    "deviceAddress": {
        "type": "string",
        "required": true,
        "description": "目标设备地址",
        "ui": {
            "widget": "plugin:device-scanner",
            "pluginConfig": {
                "protocol": "modbus",
                "scanRange": "1-247"
            }
        }
    }
}
```

当 `ui.widget` 以 `plugin:` 前缀开头时，前端加载对应的插件组件替代默认控件。

**插件体系架构：**

```
┌──────────────────────────────────────────────┐
│              Plugin Host (后端)               │
│  ┌────────────┐  ┌────────────┐              │
│  │ JS Plugin  │  │ Qt Plugin  │  ...         │
│  │ (QuickJS)  │  │ (QPlugin)  │              │
│  └─────┬──────┘  └─────┬──────┘              │
│        │               │                     │
│        └───────┬───────┘                     │
│                ▼                             │
│     Plugin Registry (注册/发现/调用)          │
└──────────────────┬───────────────────────────┘
                   │ REST API
┌──────────────────▼───────────────────────────┐
│           Plugin UI Component (前端)          │
│  ┌────────────────────────────────────────┐  │
│  │  Vue 组件：设备扫描器 / 文件选择器 /   │  │
│  │  地图坐标 / 串口选择 / 颜色选取 ...    │  │
│  └────────────────────────────────────────┘  │
└──────────────────────────────────────────────┘
```

**插件接口定义：**

```cpp
// 后端插件接口
class IConfigPlugin {
public:
    virtual ~IConfigPlugin() = default;
    virtual QString pluginId() const = 0;
    virtual QString displayName() const = 0;

    // 插件可提供候选值列表（如扫描到的设备列表）
    virtual QJsonArray enumerate(const QJsonObject& pluginConfig) = 0;

    // 插件可对用户选择的值进行转换/验证
    virtual QJsonValue transform(const QJsonValue& rawValue,
                                 const QJsonObject& pluginConfig) = 0;
};
```

**内置插件示例：**

| 插件 ID | 用途 | UI 组件 |
|---------|------|---------|
| `serial-port` | 列举系统串口 | 下拉选择器 |
| `file-browser` | 浏览服务器文件系统 | 文件选择对话框 |
| `device-scanner` | 扫描网络/总线设备 | 设备列表 + 扫描按钮 |
| `ip-port` | IP 地址 + 端口组合输入 | 双字段组合控件 |
| `cron-editor` | Cron 表达式可视化编辑 | Cron 编辑器 |
| `driver-selector` | 从已注册 Driver 中选择 | Driver 列表选择器 |

### 6.5 配置分发流程

```
                    config.schema.json
                          │
                          ▼
              ┌─── Schema 解析 ───┐
              │                   │
              ▼                   ▼
        填充 default        生成 UI 表单
              │                   │
              ▼                   ▼
        合并 Template       用户编辑配置
              │                   │
              ▼                   ▼
        合并 Plugin 输出    前端提交 JSON
              │                   │
              └────────┬──────────┘
                       ▼
            ServiceConfigValidator
            mergeAndValidate()
                       │
                       ▼
              写入 config.json
                       │
                       ▼
            启动时 --config-file 传入
                       │
                       ▼
            JS 脚本 getConfig() 读取
```

### 6.6 Driver 读取 Service 配置

Driver 本身无状态，但 Service 的 JS 脚本可以选择性地将配置传递给 Driver：

```javascript
import { openDriver, getConfig } from 'stdiolink';

const cfg = getConfig();

// 场景1：通过启动参数传入
const drv = await openDriver(cfg.driverPath, {
    args: [`--baud=${cfg.baudRate}`, `--parity=${cfg.parity}`]
});

// 场景2：启动后通过命令传入
await drv.configure({
    baudRate: cfg.baudRate,
    parity: cfg.parity,
    timeout: cfg.timeout
});

// 场景3：每次请求时携带参数
const result = await drv.readRegisters({
    address: cfg.deviceAddress,
    start: 0,
    count: 10
});
```

### 6.7 底层对接

配置系统复用现有组件：

| 现有组件 | 用途 |
|----------|------|
| `ServiceConfigSchema::fromJsonFile()` | 加载 config.schema.json |
| `ServiceConfigValidator::mergeAndValidate()` | 合并与校验配置 |
| `DefaultFiller::fillDefaults()` | 递归填充默认值（含嵌套对象） |
| `FieldMeta.ui` (UIHint) | 前端表单渲染提示 |
| `Constraints` | 前后端双重校验 |
| `ServiceConfigHelp::generate()` | CLI --help 配置项文档 |

---

## 7. 调度系统

### 7.1 调度策略类型

调度引擎是 Server Manager 的核心能力，支持以下策略：

| 策略 | 标识 | 说明 |
|------|------|------|
| 单次执行 | `once` | 立即执行一次，完成后停止 |
| 固定速率 | `fixedRate` | 每隔固定时间触发，不等上次完成（可能重叠） |
| 固定延迟 | `fixedDelay` | 上次完成后等待固定时间再触发 |
| 防重叠 | `noOverlap` | 固定速率但跳过上次未完成的触发 |
| 连续执行 | `continuous` | 上次完成后立即启动下一次（fixedDelay=0） |
| 常驻/守护 | `daemon` | 持续运行，异常退出后自动重启 |
| Cron 定时 | `cron` | 基于 Cron 表达式的定时调度 |

### 7.2 调度策略数据结构

```json
{
    "id": "sch-001",
    "type": "fixedDelay",
    "intervalMs": 60000,
    "cronExpr": null,
    "timeoutMs": 30000,
    "maxRetries": 3,
    "retryDelayMs": 5000,
    "maxConcurrent": 1,
    "enabled": true,
    "startTime": "2026-02-08T08:00:00Z",
    "endTime": null,
    "daemon": {
        "restartDelayMs": 3000,
        "maxRestarts": -1,
        "resetCounterAfterMs": 300000
    }
}
```

**字段说明：**

| 字段 | 类型 | 说明 |
|------|------|------|
| type | enum | 调度策略类型 |
| intervalMs | int | 间隔毫秒（fixedRate / fixedDelay / noOverlap） |
| cronExpr | string? | Cron 表达式（cron 类型） |
| timeoutMs | int | 单次执行超时，0 表示不限 |
| maxRetries | int | 失败重试次数，0 表示不重试 |
| retryDelayMs | int | 重试间隔毫秒 |
| maxConcurrent | int | 最大并发实例数（fixedRate 允许 >1） |
| enabled | bool | 是否启用 |
| startTime | datetime? | 生效起始时间 |
| endTime | datetime? | 生效截止时间 |
| daemon.restartDelayMs | int | 守护模式重启延迟 |
| daemon.maxRestarts | int | 最大重启次数，-1 表示无限 |
| daemon.resetCounterAfterMs | int | 稳定运行多久后重置重启计数 |

### 7.3 调度引擎架构

```
┌─────────────────────────────────────────────────────┐
│                  ScheduleEngine                      │
│                                                      │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────┐ │
│  │ PolicyRouter │  │ TimerManager │  │ CronParser│ │
│  │ (策略分发)    │  │ (定时器管理)  │  │ (Cron解析)│ │
│  └──────┬───────┘  └──────┬───────┘  └─────┬─────┘ │
│         │                 │                │        │
│  ┌──────▼─────────────────▼────────────────▼──────┐ │
│  │              InstanceRunner                     │ │
│  │  ┌────────────┐ ┌──────────────┐ ┌───────────┐│ │
│  │  │ ProcessMgr │ │ TimeoutGuard │ │ RetryLogic││ │
│  │  └────────────┘ └──────────────┘ └───────────┘│ │
│  └────────────────────────┬───────────────────────┘ │
│                           │                          │
│  ┌────────────────────────▼───────────────────────┐ │
│  │           InstanceStateTracker                  │ │
│  │  running / succeeded / failed / timeout / retry │ │
│  └─────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
```

### 7.4 各策略执行逻辑

**once（单次执行）：**

```
trigger → 启动 Instance → 等待完成 → 标记 Service 为 stopped
```

**fixedRate（固定速率）：**

```
trigger → 启动 Instance
  ↑                        （不等完成）
  └── intervalMs 后再次 trigger
      如果 maxConcurrent > 1，允许多实例并行
```

**fixedDelay（固定延迟）：**

```
trigger → 启动 Instance → 等待完成 → 等待 intervalMs → 再次 trigger
```

**noOverlap（防重叠）：**

```
trigger → 检查是否有运行中实例
  ├─ 有 → 跳过本次
  └─ 无 → 启动 Instance
  ↑
  └── intervalMs 后再次 trigger
```

**continuous（连续执行）：**

```
trigger → 启动 Instance → 等待完成 → 立即再次 trigger（等同 fixedDelay=0）
```

**daemon（守护进程）：**

```
启动 Instance → 持续运行
  │
  ├─ 正常退出 → 等待 restartDelayMs → 重启
  ├─ 异常退出 → restartCount++
  │    ├─ restartCount <= maxRestarts → 等待 restartDelayMs → 重启
  │    └─ restartCount > maxRestarts → 标记 error，停止调度
  │
  └─ 稳定运行 resetCounterAfterMs → restartCount = 0
```

**cron（Cron 定时）：**

```
解析 cronExpr → 计算下次触发时间 → 设置定时器
  ↑                                      │
  └──────────────────────────────────────┘
                                          ↓
                                    启动 Instance
```

### 7.5 超时与重试

**超时控制（TimeoutGuard）：**

当 `timeoutMs > 0` 时，InstanceRunner 启动一个定时器监控执行时长：

```cpp
// TimeoutGuard 伪代码
class TimeoutGuard : public QObject {
    Q_OBJECT
public:
    void start(QProcess* proc, int timeoutMs) {
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, [=]() {
            proc->terminate();
            QTimer::singleShot(5000, proc, &QProcess::kill); // 5s 后强杀
            emit timeout();
        });
        m_timer.start(timeoutMs);
    }
    void cancel() { m_timer.stop(); }
signals:
    void timeout();
private:
    QTimer m_timer;
};
```

**重试逻辑（RetryLogic）：**

```
Instance 执行失败
    │
    ▼
retryCount < maxRetries ?
    ├─ 是 → 等待 retryDelayMs → retryCount++ → 重新启动 Instance
    └─ 否 → 标记最终失败，触发 error 事件
```

重试仅在非超时失败时触发。超时视为最终失败（除非配置允许超时重试）。

### 7.6 Service Instance 状态机

```
         start
           │
           ▼
       starting ──────────────────┐
           │                      │ (启动失败)
           ▼                      ▼
       running              failed/error
           │                      │
     ┌─────┼─────┐          retry?│
     │     │     │           ├─ 是 → starting
     │     │     │           └─ 否 → stopped
     ▼     ▼     ▼
succeeded failed timeout
     │     │       │
     ▼     ▼       ▼
   stopped (根据调度策略决定是否再次启动)
```

**Instance 运行记录：**

| 字段 | 类型 | 说明 |
|------|------|------|
| instanceId | string | 实例唯一 ID |
| serviceId | string | 所属 Service |
| scheduleId | string | 触发的调度策略 |
| status | enum | starting / running / succeeded / failed / timeout |
| startedAt | datetime | 启动时间 |
| finishedAt | datetime? | 结束时间 |
| exitCode | int? | 进程退出码 |
| retryCount | int | 当前重试次数 |
| logPath | string | 本次运行日志路径 |

---

## 8. 文件与目录隔离

### 8.1 目录结构总览

```
stdiolink_server_data/                    # Server Manager 数据根目录
├── server.db                             # SQLite 主数据库
├── config/
│   └── server_config.json                # Server Manager 自身配置
├── drivers/                              # Driver 注册信息缓存
│   └── meta_cache/                       # Driver 元数据缓存文件
├── services/                             # Service 实例目录（每个 Service 独立）
│   ├── {service-id-1}/
│   │   ├── manifest.json
│   │   ├── index.js
│   │   ├── config.schema.json
│   │   ├── config.json                   # 当前生效配置
│   │   ├── data/                         # 服务私有数据
│   │   ├── logs/                         # 服务日志
│   │   └── lib/                          # 辅助 JS 模块
│   └── {service-id-2}/
│       └── ...
├── shared/                               # 全局共享目录
│   ├── data/                             # 跨服务共享数据
│   └── drivers/                          # 共享 Driver 可执行文件
├── templates/                            # 配置模板存储
│   └── {template-id}.json
├── plugins/                              # 配置插件
│   ├── js/                               # JS 插件
│   └── native/                           # 原生 Qt 插件
└── logs/                                 # Server Manager 自身日志
    └── server.log
```

### 8.2 隔离策略

| 目录 | 归属 | 读写权限 | 说明 |
|------|------|----------|------|
| `services/{id}/data/` | 单个 Service | 读写 | 服务私有数据，其他服务不可访问 |
| `services/{id}/logs/` | 单个 Service | 写入 | 服务运行日志，管理器可读 |
| `services/{id}/config.json` | 管理器 | 管理器写 / 服务读 | 由管理器合并生成，服务只读 |
| `shared/data/` | 全局 | 所有服务读写 | 跨服务共享数据交换 |
| `shared/drivers/` | 全局 | 只读 | 共享 Driver 可执行文件 |

### 8.3 环境变量注入

Service 启动时，InstanceRunner 通过环境变量告知各目录路径：

```cpp
QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
env.insert("STDIOLINK_SERVICE_ID",  serviceId);
env.insert("STDIOLINK_SERVICE_DIR", serviceDir);       // services/{id}/
env.insert("STDIOLINK_DATA_DIR",    serviceDir + "/data");
env.insert("STDIOLINK_LOG_DIR",     serviceDir + "/logs");
env.insert("STDIOLINK_SHARED_DIR",  sharedDir);         // shared/
env.insert("STDIOLINK_CONFIG_FILE", serviceDir + "/config.json");
proc.setProcessEnvironment(env);
```

### 8.4 日志管理

**日志分层：**

| 层级 | 来源 | 存储位置 | 说明 |
|------|------|----------|------|
| Server 日志 | Server Manager 自身 | `logs/server.log` | API 请求、调度事件、系统错误 |
| Service 日志 | Service 进程 stdout/stderr | `services/{id}/logs/` | JS 脚本输出、Driver 通信日志 |
| Instance 日志 | 单次运行 | `services/{id}/logs/{instanceId}.log` | 每次执行的独立日志 |

**日志轮转策略：**

- 单文件最大 10MB，超出后轮转
- 保留最近 10 个轮转文件
- 可通过 `server_config.json` 配置轮转参数
- Instance 日志保留最近 100 次运行记录，超出后自动清理

---

## 9. Web API 设计

### 9.1 API 总览

所有 API 以 `/api/v1` 为前缀，返回 JSON 格式。

**通用响应格式：**

```json
{
    "code": 0,
    "message": "ok",
    "data": { ... }
}
```

**错误响应格式：**

```json
{
    "code": 40001,
    "message": "Service not found",
    "data": null
}
```

### 9.2 Driver API

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/v1/drivers` | 注册 Driver（提供路径，自动探测） |
| GET | `/api/v1/drivers` | 查询 Driver 列表（支持 ?tag=&name=&status= 筛选） |
| GET | `/api/v1/drivers/{id}` | 获取 Driver 详情（含完整 DriverMeta） |
| PUT | `/api/v1/drivers/{id}` | 更新 Driver 信息（名称、标签） |
| DELETE | `/api/v1/drivers/{id}` | 删除 Driver 注册（检查引用） |
| POST | `/api/v1/drivers/{id}/probe` | 重新探测 Driver 元数据 |
| POST | `/api/v1/drivers/scan` | 批量扫描目录，发现并注册 Driver |

**注册 Driver 请求示例：**

```json
POST /api/v1/drivers
{
    "path": "/opt/drivers/modbus_rtu_driver",
    "name": "Modbus RTU Driver",
    "tags": ["modbus", "serial"]
}
```

**注册 Driver 响应示例：**

```json
{
    "code": 0,
    "message": "ok",
    "data": {
        "id": "drv-a1b2c3",
        "name": "Modbus RTU Driver",
        "path": "/opt/drivers/modbus_rtu_driver",
        "status": "available",
        "meta": {
            "info": { "id": "modbus-rtu", "name": "Modbus RTU Driver", "version": "1.0.0" },
            "commands": [
                { "name": "readRegisters", "description": "读取保持寄存器" },
                { "name": "writeRegister", "description": "写入单个寄存器" }
            ]
        },
        "tags": ["modbus", "serial"],
        "registeredAt": "2026-02-08T10:00:00Z"
    }
}
```

### 9.3 Service API

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/v1/services` | 创建 Service（生成目录骨架） |
| POST | `/api/v1/services/import` | 导入已有服务目录 |
| GET | `/api/v1/services` | 查询 Service 列表（支持 ?status=&name= 筛选分页） |
| GET | `/api/v1/services/{id}` | 获取 Service 详情 |
| PUT | `/api/v1/services/{id}` | 更新 Service 信息 |
| DELETE | `/api/v1/services/{id}` | 删除 Service（停止实例，归档目录） |
| POST | `/api/v1/services/{id}/start` | 启动 Service |
| POST | `/api/v1/services/{id}/stop` | 停止 Service |
| POST | `/api/v1/services/{id}/restart` | 重启 Service |
| POST | `/api/v1/services/{id}/clone` | 克隆 Service |
| GET | `/api/v1/services/{id}/status` | 获取运行状态与实例信息 |

### 9.4 Config API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/v1/services/{id}/config-schema` | 获取配置 Schema（FieldMeta[] JSON） |
| GET | `/api/v1/services/{id}/config` | 获取当前生效配置 |
| PUT | `/api/v1/services/{id}/config` | 更新配置（合并验证后写入） |
| POST | `/api/v1/services/{id}/config/validate` | 仅验证配置，不写入 |
| POST | `/api/v1/services/{id}/config/reset` | 重置为默认配置 |

### 9.5 Template API

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/v1/templates` | 创建配置模板 |
| GET | `/api/v1/templates` | 查询模板列表 |
| GET | `/api/v1/templates/{id}` | 获取模板详情 |
| PUT | `/api/v1/templates/{id}` | 更新模板 |
| DELETE | `/api/v1/templates/{id}` | 删除模板 |
| POST | `/api/v1/templates/{id}/apply/{serviceId}` | 将模板应用到 Service |
| POST | `/api/v1/templates/export/{serviceId}` | 从 Service 当前配置导出为模板 |

### 9.6 Schedule API

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/v1/schedules` | 创建调度策略 |
| GET | `/api/v1/schedules` | 查询调度策略列表 |
| GET | `/api/v1/schedules/{id}` | 获取调度策略详情 |
| PUT | `/api/v1/schedules/{id}` | 更新调度策略 |
| DELETE | `/api/v1/schedules/{id}` | 删除调度策略 |
| POST | `/api/v1/schedules/{id}/enable` | 启用调度 |
| POST | `/api/v1/schedules/{id}/disable` | 禁用调度 |
| POST | `/api/v1/services/{id}/schedule` | 为 Service 绑定调度策略 |
| DELETE | `/api/v1/services/{id}/schedule` | 解绑 Service 的调度策略 |

### 9.7 Plugin API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/v1/plugins` | 查询已注册插件列表 |
| GET | `/api/v1/plugins/{id}` | 获取插件详情 |
| POST | `/api/v1/plugins/{id}/enumerate` | 调用插件枚举（如扫描设备列表） |
| POST | `/api/v1/plugins/{id}/transform` | 调用插件值转换 |

### 9.8 Log API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/v1/services/{id}/logs` | 获取 Service 日志列表（分页） |
| GET | `/api/v1/services/{id}/logs/{instanceId}` | 获取指定 Instance 的日志内容 |
| GET | `/api/v1/services/{id}/logs/latest` | 获取最新一次运行日志 |
| DELETE | `/api/v1/services/{id}/logs` | 清理历史日志 |
| GET | `/api/v1/logs/server` | 获取 Server Manager 自身日志 |

### 9.9 WebSocket 实时事件

**连接地址：** `ws://host:port/api/v1/ws`

**事件推送格式：**

```json
{
    "type": "event",
    "topic": "service.status",
    "data": {
        "serviceId": "svc-001",
        "oldStatus": "stopped",
        "newStatus": "running",
        "timestamp": "2026-02-08T10:05:00Z"
    }
}
```

**事件主题（Topic）：**

| Topic | 触发时机 | data 内容 |
|-------|----------|-----------|
| `service.status` | Service 状态变更 | serviceId, oldStatus, newStatus |
| `service.config` | Service 配置更新 | serviceId, configHash |
| `instance.start` | Instance 启动 | serviceId, instanceId |
| `instance.finish` | Instance 完成 | serviceId, instanceId, exitCode, status |
| `instance.timeout` | Instance 超时 | serviceId, instanceId, timeoutMs |
| `instance.log` | Instance 实时日志行 | serviceId, instanceId, line, level |
| `driver.status` | Driver 状态变更 | driverId, oldStatus, newStatus |
| `schedule.trigger` | 调度触发 | scheduleId, serviceId |
| `schedule.skip` | 调度跳过（防重叠） | scheduleId, serviceId, reason |

**客户端订阅机制：**

客户端连接 WebSocket 后，通过发送订阅消息选择关注的 Topic：

```json
{
    "type": "subscribe",
    "topics": ["service.status", "instance.log"],
    "filter": { "serviceId": "svc-001" }
}
```

取消订阅：

```json
{
    "type": "unsubscribe",
    "topics": ["instance.log"]
}
```

未发送订阅消息的客户端默认接收所有事件。

---

## 10. Web UI 设计

### 10.1 技术栈

| 组件 | 技术 | 说明 |
|------|------|------|
| 框架 | Vue 3 (Composition API) | 轻量、响应式、生态成熟 |
| 构建 | Vite | 快速开发与构建 |
| UI 库 | Element Plus 或 Naive UI | 丰富的表单组件，适合管理后台 |
| 状态管理 | Pinia | 轻量状态管理 |
| 路由 | Vue Router 4 | SPA 路由 |
| WebSocket | 原生 WebSocket + 自动重连 | 实时事件接收 |
| 图表 | ECharts | 调度监控可视化 |

### 10.2 页面结构

```
┌──────────────────────────────────────────────────────┐
│  顶部导航栏：Logo │ Dashboard │ Drivers │ Services │  │
│                   Templates │ Schedules │ Logs │ ⚙   │
├──────────────────────────────────────────────────────┤
│                                                      │
│                    主内容区域                          │
│                                                      │
├──────────────────────────────────────────────────────┤
│  底部状态栏：连接状态 │ 运行中 Service 数 │ 版本      │
└──────────────────────────────────────────────────────┘
```

### 10.3 页面清单

**Dashboard（仪表盘）：**

- Service 运行状态概览（运行中 / 已停止 / 错误 数量卡片）
- Driver 注册数量统计
- 最近调度执行记录（时间线）
- 实时事件流（WebSocket 推送）
- 系统资源概览（CPU / 内存占用）

**Driver 管理页：**

- Driver 列表表格（名称、路径、状态、标签、注册时间）
- 注册 Driver 对话框（输入路径，自动探测）
- 批量扫描目录按钮
- Driver 详情抽屉（完整 DriverMeta、命令列表、参数 Schema）
- 重新探测按钮
- 标签编辑

**Service 管理页：**

- Service 列表表格（名称、状态、调度策略、最近运行时间）
- 状态指示灯（绿色运行 / 灰色停止 / 红色错误）
- 快捷操作按钮（启动 / 停止 / 重启）
- 创建 Service 向导（输入 id → 选择模板 → 编辑配置 → 绑定调度）
- 导入已有服务目录
- 克隆 Service

**Service 详情页：**

- 基本信息卡片（manifest 信息、目录路径、状态）
- 配置编辑 Tab（基于 config.schema.json 动态生成表单）
- 调度策略 Tab（绑定/编辑调度策略）
- 运行历史 Tab（Instance 列表、状态、耗时、退出码）
- 实时日志 Tab（WebSocket 推送的实时日志流）
- 脚本查看 Tab（只读查看 index.js 内容）

**配置编辑器（SchemaFormRenderer）：**

- 根据 `config.schema.json` 的 FieldMeta 递归渲染表单
- 支持所有字段类型：String、Int、Double、Bool、Enum、Array、Object
- 支持 `ui.widget` 覆盖默认控件（textarea、color、slider 等）
- 支持 `ui.group` 分组折叠面板
- 支持 `ui.advanced` 高级区域折叠
- 支持 `ui.visibleIf` 条件显示
- 支持 `plugin:*` 前缀加载插件组件
- 实时前端校验（required、min/max、pattern、enum）
- 配置变更对比（diff 视图）

**调度管理页：**

- 调度策略列表（类型、间隔、关联 Service、启用状态）
- 创建/编辑调度策略表单
- Cron 表达式可视化编辑器
- 调度时间线预览（未来 N 次触发时间点）

**模板管理页：**

- 模板列表（名称、目标 Schema、标签、继承关系）
- 创建/编辑模板表单
- 模板继承关系可视化（树形图）
- 一键应用到 Service

**日志查看页：**

- Service 日志列表（按 Instance 分组）
- 日志内容查看器（语法高亮、搜索、过滤）
- 实时日志流（WebSocket 推送，自动滚动）
- 日志级别过滤（DEBUG / INFO / WARN / ERROR）
- 日志下载

### 10.4 前端部署

Web UI 构建产物放置在 `src/stdiolink_server/webui/dist/` 目录，Server Manager 启动时通过 HTTP Server 静态托管：

```cpp
// ApiServer 中注册静态文件路由
void ApiServer::setupStaticFiles() {
    QString webRoot = QCoreApplication::applicationDirPath() + "/webui/dist";
    // 所有非 /api/ 开头的请求返回静态文件
    // SPA 模式：未匹配的路径返回 index.html
}
```

生产环境也可将前端部署到独立 Web 服务器（Nginx），通过反向代理访问 API。

---

## 11. 扩展规划

### 11.1 账户与权限系统（Phase 2）

**用户模型：**

| 字段 | 类型 | 说明 |
|------|------|------|
| id | string | 用户唯一 ID |
| username | string | 登录用户名 |
| passwordHash | string | bcrypt 哈希密码 |
| role | enum | admin / operator / viewer |
| createdAt | datetime | 创建时间 |
| lastLoginAt | datetime | 最近登录时间 |

**角色权限矩阵：**

| 操作 | admin | operator | viewer |
|------|-------|----------|--------|
| 查看 Dashboard | ✓ | ✓ | ✓ |
| 查看 Driver / Service 列表 | ✓ | ✓ | ✓ |
| 查看日志 | ✓ | ✓ | ✓ |
| 启动 / 停止 Service | ✓ | ✓ | ✗ |
| 编辑配置 | ✓ | ✓ | ✗ |
| 创建 / 删除 Service | ✓ | ✗ | ✗ |
| 注册 / 删除 Driver | ✓ | ✗ | ✗ |
| 管理用户 | ✓ | ✗ | ✗ |
| 编辑 JS 脚本 | ✓ | ✗ | ✗ |

**认证机制：**

- 基于 JWT Token 的无状态认证
- 登录接口：`POST /api/v1/auth/login` → 返回 access_token + refresh_token
- API 请求携带 `Authorization: Bearer <token>` 头
- Token 过期时间可配置（默认 access_token 2h，refresh_token 7d）
- WebSocket 连接时通过 URL 参数传递 token：`ws://host:port/api/v1/ws?token=<token>`

### 11.2 在线 JS 脚本编辑器（Phase 2）

**功能需求：**

- 基于 Monaco Editor（VS Code 内核）的浏览器端代码编辑器
- 支持 ES Module 语法高亮与智能提示
- 自动加载 stdiolink API 的 TypeScript 类型定义（`.d.ts`）
- 文件树浏览服务目录（index.js、lib/*.js）
- 保存时自动语法检查
- 支持在线调试运行（启动一次性 Instance，实时查看输出）

**脚本编辑 API：**

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/v1/services/{id}/files` | 列出服务目录文件树 |
| GET | `/api/v1/services/{id}/files/{path}` | 读取文件内容 |
| PUT | `/api/v1/services/{id}/files/{path}` | 写入文件内容 |
| POST | `/api/v1/services/{id}/files/{path}` | 创建新文件 |
| DELETE | `/api/v1/services/{id}/files/{path}` | 删除文件 |
| POST | `/api/v1/services/{id}/debug-run` | 调试运行（once 模式，实时日志） |
| GET | `/api/v1/typedefs/stdiolink.d.ts` | 获取 stdiolink API 类型定义 |

### 11.3 Service 市场（Phase 3）

- Service 打包为 `.slpkg` 归档（zip 格式，含 manifest + 脚本 + schema）
- 支持从本地文件或 URL 安装 Service 包
- 版本管理：同一 Service 可安装多个版本，支持回滚
- 依赖声明：manifest.json 中声明依赖的 Driver 和最低版本

### 11.4 集群与远程管理（Phase 3）

- 多节点 Server Manager 互联，统一管理面板
- 远程 Driver 注册（通过 TCP Bridge 跨网络调用 Driver）
- Service 迁移：将 Service 从一个节点迁移到另一个节点
- 集中式日志收集与检索

---

## 12. 数据模型

### 12.1 SQLite 数据库表结构

Server Manager 使用 SQLite 作为持久化存储，数据库文件位于 `stdiolink_server_data/server.db`。

**drivers 表：**

```sql
CREATE TABLE drivers (
    id          TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    path        TEXT NOT NULL UNIQUE,
    meta_json   TEXT,              -- DriverMeta JSON 缓存
    tags        TEXT,              -- JSON 数组
    status      TEXT NOT NULL DEFAULT 'registered',
    registered_at TEXT NOT NULL,
    last_probe_at TEXT
);
```

**services 表：**

```sql
CREATE TABLE services (
    id          TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    version     TEXT,
    description TEXT,
    dir_path    TEXT NOT NULL UNIQUE,
    status      TEXT NOT NULL DEFAULT 'stopped',
    schedule_id TEXT REFERENCES schedules(id),
    template_id TEXT REFERENCES templates(id),
    config_hash TEXT,
    created_at  TEXT NOT NULL,
    updated_at  TEXT NOT NULL
);
```

**schedules 表：**

```sql
CREATE TABLE schedules (
    id              TEXT PRIMARY KEY,
    type            TEXT NOT NULL,
    interval_ms     INTEGER,
    cron_expr       TEXT,
    timeout_ms      INTEGER DEFAULT 0,
    max_retries     INTEGER DEFAULT 0,
    retry_delay_ms  INTEGER DEFAULT 5000,
    max_concurrent  INTEGER DEFAULT 1,
    enabled         INTEGER DEFAULT 1,
    start_time      TEXT,
    end_time        TEXT,
    daemon_json     TEXT,
    created_at      TEXT NOT NULL,
    updated_at      TEXT NOT NULL
);
```

**templates 表：**

```sql
CREATE TABLE templates (
    id              TEXT PRIMARY KEY,
    name            TEXT NOT NULL,
    description     TEXT,
    target_schema_id TEXT,
    extends_id      TEXT REFERENCES templates(id),
    values_json     TEXT NOT NULL,
    tags            TEXT,
    created_at      TEXT NOT NULL,
    updated_at      TEXT NOT NULL
);
```

**instances 表：**

```sql
CREATE TABLE instances (
    id          TEXT PRIMARY KEY,
    service_id  TEXT NOT NULL REFERENCES services(id),
    schedule_id TEXT REFERENCES schedules(id),
    status      TEXT NOT NULL DEFAULT 'starting',
    started_at  TEXT NOT NULL,
    finished_at TEXT,
    exit_code   INTEGER,
    retry_count INTEGER DEFAULT 0,
    log_path    TEXT
);

CREATE INDEX idx_instances_service ON instances(service_id);
CREATE INDEX idx_instances_status ON instances(status);
```

### 12.2 数据关系图

```
drivers 1───────────N service_driver_refs
                              │
services 1──────────N instances
    │
    ├── 1:0..1 ──── schedules
    └── 1:0..1 ──── templates ──── 0..1:0..1 ──── templates (继承)
```

---

## 13. 实现路线

### 13.1 Phase 1：核心框架（M33-M38）

| 里程碑 | 内容 | 依赖 |
|--------|------|------|
| M33 | 项目骨架搭建：CMake 配置、目录结构、SQLite 初始化 | 无 |
| M34 | Driver Registry：注册、探测、CRUD、元数据缓存 | M33 |
| M35 | Service Registry：创建、导入、目录管理、状态机 | M33 |
| M36 | Config Manager：Schema 加载、模板合并、配置验证与分发 | M35 |
| M37 | Schedule Engine：调度策略实现、InstanceRunner、超时与重试 | M35 |
| M38 | HTTP API Server：REST 路由、CORS、错误处理 | M34, M35, M36, M37 |

### 13.2 Phase 1 补充：实时通信与前端（M39-M42）

| 里程碑 | 内容 | 依赖 |
|--------|------|------|
| M39 | WebSocket Server：事件推送、订阅机制、实时日志流 | M38 |
| M40 | Web UI 基础框架：Vue 3 + Vite 项目、路由、布局、API 对接 | M38 |
| M41 | Web UI 核心页面：Dashboard、Driver 管理、Service 管理 | M40 |
| M42 | SchemaFormRenderer：基于 FieldMeta 的动态配置表单渲染 | M41 |

### 13.3 Phase 2：高级功能（M43-M46）

| 里程碑 | 内容 | 依赖 |
|--------|------|------|
| M43 | 配置模板系统：模板 CRUD、继承链、应用与导出 | M36, M42 |
| M44 | 配置插件系统：Plugin Host、内置插件、前端插件加载 | M36, M42 |
| M45 | 调度管理 UI：调度策略编辑、Cron 编辑器、时间线预览 | M37, M41 |
| M46 | 日志系统：日志轮转、实时日志流、日志查看 UI | M39, M41 |

### 13.4 Phase 2 补充：安全与编辑（M47-M49）

| 里程碑 | 内容 | 依赖 |
|--------|------|------|
| M47 | 账户与权限：用户 CRUD、JWT 认证、角色权限中间件 | M38 |
| M48 | 在线 JS 编辑器：Monaco Editor 集成、文件 API、调试运行 | M41, M39 |
| M49 | 集成测试与文档：端到端测试、API 文档（OpenAPI）、部署指南 | M38-M48 |

### 13.5 Phase 3：扩展能力（M50-M52）

| 里程碑 | 内容 | 依赖 |
|--------|------|------|
| M50 | Service 打包与安装：`.slpkg` 格式、版本管理、依赖检查 | M35 |
| M51 | 集群管理：多节点互联、远程 Driver、Service 迁移 | M38, M39 |
| M52 | 性能优化与稳定性：压力测试、内存泄漏检测、长期运行验证 | M33-M49 |

### 13.6 里程碑依赖图

```
M33 (骨架)
 ├── M34 (Driver Registry)
 │    └── M38 (HTTP API) ─── M39 (WebSocket) ─── M40 (Web UI 框架)
 ├── M35 (Service Registry)                            │
 │    ├── M36 (Config Manager)                         │
 │    │    ├── M43 (模板系统)                           │
 │    │    └── M44 (插件系统)                           │
 │    └── M37 (Schedule Engine)                        │
 │         └── M45 (调度 UI)                           │
 │                                                     │
 └── M38 ──── M47 (账户权限)                           │
                                                       │
              M41 (核心页面) ◄─────────────────────────┘
               ├── M42 (SchemaFormRenderer)
               ├── M46 (日志系统)
               └── M48 (JS 编辑器)
                        │
                        └── M49 (集成测试)
                             │
                        M50-M52 (Phase 3)
```
