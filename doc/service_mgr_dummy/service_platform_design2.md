# StdioLink 服务管理平台（类操作系统）研发设计方案

> 目标：在现有 **StdioLink（stdio/console IPC + Driver Meta + JS Runtime）** 基础上，新增一个“服务管理平台”，把 Driver、JS Service、计划任务、日志、状态、配置、插件化 UI 等统一到一个可视化 Web 控制台中，形成“类似操作系统”的服务管理程序。

---

## 1. 愿景与边界

### 1.1 愿景
- 把“驱动（Driver）”和“服务（Service）”统一抽象为可安装、可配置、可运行、可观测、可编排的 **Unit（单元）**。
- 以 Web 控制台作为“桌面”，提供：
  - 在线编辑代码（JS/TS）与文件管理
  - Driver 安装/升级/实例化/测试
  - Service 创建/运行/停止/重启/自动拉起
  - 计划任务（Cron/间隔/事件触发）
  - 实时日志（Tail/检索/归档）
  - 在线状态/指标/健康检查
  - 完整进程树（含子进程）与资源监控
  - 配置系统：基于 **Meta（schema）** 自动生成 UI
  - 插件系统：Driver 注册时可带入 Web 配置插件（如点云流水线工作流编辑器）

### 1.2 非目标（第一阶段不做）
- 多机集群调度/分布式一致性（可预留）
- 强隔离容器级沙盒（可后续增强）
- 完整的多租户计费系统

---

## 2. 核心概念与统一抽象

### 2.1 Unit（统一单元）
平台把可运行对象统一为 Unit：
- **Driver Unit**：外部可执行 Driver（StdioLink 协议）
- **Service Unit**：以 `stdiolink_service` 运行 JS/TS 入口文件的服务（可 oneshot/keepalive）

共同能力：
- 安装（package）→ 配置（config）→ 实例化（instance）→ 运行（process）→ 观测（status/logs/metrics）

### 2.2 Package / Instance
- **Package（包）**：静态发布物
  - Driver 包：driver.exe + meta + docs + 可选 web 插件
  - Service 包：JS/TS 项目 + meta + 可选 web 插件
- **Instance（实例）**：运行态配置与隔离
  - 同一个 Package 可创建多个 Instance（不同配置/目录/策略）

### 2.3 Meta（配置与 UI 的“真理来源”）
- Driver 已具备 `DriverMeta`：包含 `config schema`、`commands`、`types`、`examples`、`errors`
- Service 需要引入 `ServiceMeta`：与 DriverMeta 同构或兼容 JSON Schema

Meta 用途：
- 自动生成配置界面（表单/校验/默认值）
- 生成 TS 类型声明（编辑器智能提示）
- 生成 API 文档（Markdown/OpenAPI/HTML）
- 为插件提供“可组合的配置组件”和“工作流节点定义”

---

## 3. 总体架构

### 3.1 组件划分
**A. stdoilinkd（管理守护进程 / Kernel）**
- 负责：进程监管、包管理、实例管理、沙盒目录、调度器、日志采集、指标、API 服务端

**B. Web Console（控制台 / Desktop）**
- 负责：交互 UI、代码编辑器、配置 UI、日志查看、状态监控、插件加载

**C. CLI（可选）**
- 负责：本地脚本化操作（安装/启动/查看状态/导出 meta/doc）

### 3.2 数据与事件流（文字版）
1) 用户在 Web Console 创建/配置 Service Instance
2) Console 调用 `stdiolinkd API` 写入配置与目录结构
3) `stdiolinkd` 启动进程（driver/service）并将 stdout 作为协议通道、stderr 作为日志通道
4) `stdiolinkd` 通过 WebSocket 推送：状态变化、日志流、任务事件
5) UI 根据 Meta 渲染配置表单/工作流编辑器，并能触发运行/测试/计划任务

---

## 4. 目录与沙盒设计

### 4.1 统一目录布局（建议）
以“实例”为粒度（便于隔离与备份）：

- `DATA_ROOT/`
  - `packages/`
    - `drivers/<pkgId>/<version>/...`
    - `services/<pkgId>/<version>/...`
  - `instances/`
    - `drivers/<instanceId>/`
      - `private/`（实例私有）
      - `shared/`（实例共享挂载点，指向全局或指定共享）
      - `config/instance.json`
      - `logs/`
    - `services/<instanceId>/`
      - `workspace/`（在线编辑代码的工作区）
      - `private/`
      - `shared/`
      - `config/instance.json`
      - `logs/`
  - `shared/`（全局共享）
  - `db/manager.sqlite`

Windows 可映射到：`%ProgramData%\StdioLink\`；Linux 可映射到：`/var/lib/stdiolink/`

### 4.2 “轻量沙盒”策略（第一阶段）
- **文件系统隔离**：所有 UI 文件操作限制在 instance 目录内（强校验 + 规范化路径）
- **环境变量注入**：启动进程时注入：
  - `STDIOLINK_INSTANCE_ID`
  - `STDIOLINK_PRIVATE_DIR`
  - `STDIOLINK_SHARED_DIR`
  - `STDIOLINK_WORKSPACE_DIR`（service）
  - `STDIOLINK_CONFIG_PATH`
  - `STDIOLINK_LOG_DIR`
- **权限收敛（可选增强）**：
  - Windows：Job Object + 限制 token（后续）
  - Linux：cgroup（资源限制）+ namespaces（后续）

### 4.3 完整进程树获取
- 启动时把 root pid 关联到一个“监管容器”：
  - Windows：Job Object（能追踪子进程）
  - Linux：cgroup 或读取 `/proc/<pid>/task/...` + PPID 反查
- 对外提供 `GET /instances/{id}/proctree` 返回树结构 + 资源占用

---

## 5. 进程监管与生命周期

### 5.1 统一状态机
- `Created → Configured → Starting → Running → Stopping → Stopped`
- 异常：`Crashed`（带 exitCode/error）
- 策略：
  - 自动重启：`always | on-failure | never`
  - 退避：指数退避 + 最大重试

### 5.2 Profile：oneshot / keepalive
- Driver 通常 keepalive（可选 oneshot）
- Service 支持：
  - oneshot：执行入口后退出（适合计划任务）
  - keepalive：常驻并对外提供命令/事件（适合在线状态与编排）

建议：
- **平台内部把 Service 也做成“Driver-like”**：
  - `stdiolink_service --profile keepalive --entry main.js`
  - JS 通过 meta 声明可调用命令（commands），平台统一用 StdioLink Task API 调用

---

## 6. 配置系统（Meta 驱动）

### 6.1 配置类型
- **Driver Package 配置**：包级默认（很少用）
- **Driver Instance 配置**：实例级（最常用）
- **Service Instance 配置**：实例级 + 依赖 Driver 的引用与覆盖

### 6.2 Service 安装 Driver 的“通用配置”
引入“依赖安装向导”：
- Service meta 声明 `dependencies`：
  - `driverPkgId`
  - 所需 capabilities / command 约束
  - 建议的 instance template（默认 config）
- UI：
  1) 选择 driver 包版本
  2) 根据 DriverMeta.config 自动渲染表单
  3) 生成 DriverInstance，并在 ServiceInstance 里引用（by instanceId）

### 6.3 Meta 与表单渲染
- 采用统一 schema：
  - 优先复用现有 `DriverMeta.config`（FieldMeta/Constraints）
  - 对 ServiceMeta 设计与之兼容（或者直接复用同一套 FieldMeta）
- 表单引擎能力：
  - 默认值、必填、范围、枚举、正则、依赖显示
  - 支持“字段级敏感信息”标记（UI 遮罩 + 存储加密）

---

## 7. 在线代码编辑与构建运行

### 7.1 Web IDE 需求
- 文件树、搜索、重命名、上传下载
- Monaco Editor：JS/TS 高亮、跳转、lint（可选）
- 一键运行/调试（启动 service oneshot/keepalive）

### 7.2 TS 类型提示（关键体验点）
- 平台从 DriverMeta 自动生成 `.d.ts`：
  - 使用现有 DocGenerator 的 `toTypeScript(meta)`
- UI 把 `.d.ts` 注入 Monaco 的 language service（无需真实写到磁盘）
- 结果：开发 JS 调用 driver 命令时，有参数提示与类型校验

### 7.3 构建策略
- V1：只支持纯 JS（快速落地）
- V2：支持 TS → JS：
  - 方案 A：平台内置 `esbuild`（最实用）
  - 方案 B：调用 Node 工具链（依赖更重）

---

## 8. 日志与可观测性

### 8.1 日志通道约定（强烈建议）
- **stdout：协议输出（StdioLink frame）**
- **stderr：业务日志**（spdlog/qDebug/console.* 都走 stderr）

平台行为：
- stdout 解析为 Task 消息（event/done/error）
- stderr 作为原始日志流 → 写入 `logs/*.log` + 索引（SQLite/轻量倒排可选）

### 8.2 实时日志
- WebSocket：`/ws/logs?instanceId=...` 支持 tail + 按级别过滤（若日志带结构化字段）

### 8.3 状态与指标
- 进程级：CPU、内存、句柄数、线程数、启动时间、重启次数
- 任务级：最近 N 次任务耗时、错误率
- 健康检查：
  - 默认：进程存活
  - 可选：调用 `health` 命令（若 meta 声明）

---

## 9. 计划任务（Scheduler）

### 9.1 触发器
- Cron 表达式（如 `0 */5 * * * *`）
- 固定间隔（每 N 秒/分钟）
- 事件触发（如 driver event、文件变更、Webhook）——后续

### 9.2 动作模型
- 运行 Service oneshot
- 调用 Service/Driver 的某个命令（keepalive）
- 运行预定义工作流（见插件）

### 9.3 运行历史
- 保存：开始/结束、exitCode、stderr 摘要、产出文件链接（可选）
- UI：可重跑、可导出

---

## 10. 插件系统（Driver 带 Web 配置插件）

### 10.1 插件类型
1) **Config UI Plugin**：增强配置页面（自定义组件/分步向导）
2) **Workflow Plugin**：提供“流水线/工作流编辑器”的节点定义与 UI（例如点云滤波 pipeline）
3) **Dashboard Widget Plugin**：提供实时图表/状态卡片

### 10.2 插件包结构（建议）
Driver 包内携带：
- `web/plugin/manifest.json`
- `web/plugin/dist/*`（静态资源）

manifest 示例（概念）：
- `pluginId`
- `compatibleDriverPkgId`
- `uiModules`：配置页路由、入口模块
- `workflowNodes`：节点类型、参数 schema、图标
- `permissions`：允许调用哪些后台 API

### 10.3 插件加载机制
- 后端：
  - 安装包时扫描插件 manifest，登记到数据库
  - 静态资源通过 `GET /plugins/<pluginId>/*` 暴露
- 前端：
  - 以“微前端”方式动态 import ESM 模块
  - 插件注册到统一的 UI Registry：routes/components/nodes/widgets

### 10.4 点云滤波流水线示例（怎么落地）
- DriverMeta 提供：
  - 可用滤波算子 commands（如 voxel/grid/statistical/radius…）
  - 每个 command 的参数 schema
- Workflow Plugin 提供：
  - 节点面板（node palette）
  - 画布编辑器（DAG）
  - 节点参数面板（自动用 meta 渲染 + 少量自定义）
  - 导出 pipeline 为 JSON（平台保存到 service config 或 workspace）
  - 一键运行 pipeline：平台把 pipeline JSON 下发给 Service 或直接调用 driver

---

## 11. API 设计（REST + WebSocket）

### 11.1 基础资源
- `/api/packages/drivers`、`/api/packages/services`
- `/api/instances/drivers`、`/api/instances/services`
- `/api/instances/{id}/start|stop|restart`
- `/api/instances/{id}/status`
- `/api/instances/{id}/proctree`
- `/api/instances/{id}/logs`（查询）
- `/api/files/*`（受限文件操作）
- `/api/meta/{pkgOrInstanceId}`（获取 meta）
- `/api/plugins`（插件清单）

### 11.2 WebSocket
- `/ws/status`：推送状态变化
- `/ws/logs`：推送日志 tail
- `/ws/tasks`：推送任务事件（event/done/error）

---

## 12. 安全设计

### 12.1 认证与授权
- V1：本地单用户（token）
- V2：用户/角色（RBAC）
  - Admin / Operator / Viewer

### 12.2 关键安全点
- 文件 API：严格限制到 instance 目录，禁止路径穿越
- 插件：默认只允许访问平台公开 API；可配置白名单
- 配置加密：敏感字段（密码/密钥）加密存储（主密钥在本机）

---

## 13. 研发里程碑（建议）

### M1：Kernel/Daemon 基座（最小可用）
- 包/实例数据库（SQLite）
- 启停进程 + 自动重启策略
- stdout/stderr 分流与日志文件
- 状态查询 + 进程树（先用基础实现）

**验收**：能安装一个 driver、启动/停止、查看日志与进程树。

### M2：Web API + Console 骨架
- REST + WebSocket
- 登录（token）
- Dashboard：实例列表、状态、基本操作

**验收**：浏览器完成启动/停止、实时看到日志滚动。

### M3：在线文件管理 + JS 编辑器
- workspace 文件 API
- Monaco Editor 集成
- 一键运行（oneshot）

**验收**：在网页里写 JS，点击运行，能看到输出与日志。

### M4：Meta 配置自动 UI + Driver 安装向导
- DriverMeta/ServiceMeta 拉取与缓存
- 表单渲染引擎
- Service 依赖 Driver 的安装/配置/引用

**验收**：新增 Service 时，可一键安装并配置 driver，运行成功。

### M5：计划任务 + 运行历史
- Cron/interval
- 任务历史与重跑

**验收**：定时执行 oneshot service，历史可查询与导出。

### M6：插件系统（Config + Workflow）
- 插件 manifest 与资源托管
- UI Registry + 微前端加载
- 首个示例：点云滤波 pipeline 编辑器

**验收**：安装点云滤波 driver 后，自动出现流水线工作流配置页并可运行。

### M7：隔离与运维增强
- Windows Job Object / Linux cgroup（资源与进程树更可靠）
- 指标面板（CPU/内存/重启次数/错误率）
- 备份/导入导出（实例目录 + db）

---

## 14. 关键工程决策建议

1) **强制 stdout=协议、stderr=日志**：否则协议解析与日志会互相污染（这是平台稳定性的生命线）。
2) **Service 也做成 Driver-like**：用统一的 StdioLink 调用模型，平台实现大幅简化。
3) **Meta 是第一公民**：配置 UI、TS 类型、文档、插件都以 meta 为中心。
4) **插件走微前端**：Driver 带 UI 的成本最低、扩展最自然。

---

## 15. 下一步：我建议你先落地的“最小切片”

- 先实现 `stdiolinkd`：
  - 进程监管 + 日志分流 + REST/WS
- UI 先只做三页：
  - Instances 列表页
  - Instance 详情页（状态 + 启停 + 日志）
  - Workspace 编辑页（运行 oneshot）

等这三页跑通，再逐步引入 Meta 表单与插件。

---

如果你愿意，我可以在这个方案基础上继续细化到：
- `ServiceMeta` 的 JSON 结构（字段、约束、命令声明、依赖声明）
- 插件 `manifest.json` 的完整字段定义
- 后端数据库表结构（SQLite schema）
- REST/WS 的具体请求/响应示例（含权限与错误码）
- “点云滤波流水线插件”的节点定义与运行协议

