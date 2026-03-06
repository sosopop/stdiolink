# 料仓扫描联动服务（BinScanOrchestrator）
## 功能需求规格说明书（最终版）

| 属性 | 内容 |
|---|---|
| 文档编号 | SL-SVC-BINSCAN-001 |
| 版本 | v1.3 |
| 状态 | ✅ 可进入实现 |
| 所属模块 | `stdiolink/services` |
| 关联驱动 | `3dvision.api`、`plc.crane` |

## 1. 文档说明

### 1.1 目标

定义 BinScanOrchestrator 服务的可实现、可测试功能需求。该服务编排 PLC Crane 与 3DVision 驱动，完成单料仓一次扫描流程。

### 1.2 输入来源

- 原始需求：`doc/requirement/bin_scan_orchestrator_requirements_raw.txt`
- 协议抓包：
  - `tmp/driverlab_3dvision.api_1772693232592_websocket_event.json`
  - `tmp/driverlab_3dvision.api_1772693258361_cmd_list.json`
- 参考实现：
  - `src/drivers/driver_plc_crane/`
  - `src/drivers/driver_3dvision/`
  - `src/data_root/services/`

### 1.3 已确认决策

- D-01：扫描完成确认机制 = `WS + 命令轮询` 强制并行。
- D-02：多 Crane 处理策略 = 强一致，任一失败立即中止。
- D-03：3DVision WS 与命令驱动必须独立实例。
- D-04：扫描完成默认不自动复位 Crane。
- D-05：`ERROR/TIMEOUT` 仅支持外部手动 `reset` 恢复。

### 1.4 本版关键修正

- 修正与真实驱动接口不一致问题（`plc.crane/read_status` 不存在 `in_position` 字段）。
- 统一进度计算规则，消除 36%/37% 文档冲突。
- 明确 `logTime` 与 `scan_started_at` 的时区与比较规则。
- 收敛为命令触发模型，避免“自动启动/外部命令”混杂。

---

## 2. 功能概述

### 2.1 服务定位

BinScanOrchestrator 是协调型服务：
1. 控制一个或多个 Crane 进入扫描就绪状态。
2. 触发 3DVision 扫描。
3. 通过 WS 与轮询双通道确认扫描完成。
4. 输出扫描结果与状态快照。

### 2.2 触发模型

- 采用命令触发：`start`。
- 一次 `start` 对应一次扫描流程。
- 服务同一时刻仅允许一个活动流程。

### 2.3 扫描就绪判定

服务不依赖 `in_position` 字段，使用 `plc.crane/read_status` 的真实返回字段判定：

`in_position := (cylinder_down == true && valve_open == true)`

---

## 3. 使用场景

### 3.1 单 Crane 正常扫描

1 台 Crane 接入，收到 `start` 后完成自动下送、扫描、结果输出。

### 3.2 多 Crane 强一致扫描

多台 Crane 任一台未到位/报错，整体中止，不允许降级扫描。

### 3.3 WS 抖动兜底

WS 事件丢失时，轮询 `vessellog.last` 仍可确认完成。

### 3.4 多料仓广播过滤

WS 广播包含全部料仓，服务仅处理目标 `vessel_id` 事件。

---

## 4. 详细功能说明

### 4.1 生命周期与状态机

状态集合：
- `IDLE`
- `CRANE_PREPARING`
- `CRANE_WAITING`
- `SCAN_STARTING`
- `SCAN_RUNNING`
- `RESULT_OUTPUT`
- `ERROR`
- `TIMEOUT`

核心转移：
1. `IDLE --start--> CRANE_PREPARING`
2. `CRANE_PREPARING --all_ok--> CRANE_WAITING`
3. `CRANE_WAITING --all_in_position--> SCAN_STARTING`
4. `SCAN_STARTING --scan_ok--> SCAN_RUNNING`
5. `SCAN_RUNNING --ws_or_poll_done--> RESULT_OUTPUT`
6. `RESULT_OUTPUT --done--> IDLE`
7. 任意阶段失败按规则进入 `ERROR` 或 `TIMEOUT`
8. `ERROR/TIMEOUT --reset--> IDLE`

### 4.2 流程步骤

#### 4.2.1 初始化

1. 校验配置。
2. 打开 Crane 驱动实例。
3. 打开 3DVision 命令实例并调用 `login` 获取 token。
4. 打开 3DVision WS 实例，`ws.connect` + `ws.subscribe("vessel.notify")`。
5. 进入 `IDLE`。

#### 4.2.2 Crane 准备（`CRANE_PREPARING`）

- 并行调用每台 Crane：`set_mode({ mode: "auto" })`。
- 任一失败：按 D-02 立即中止，进入 `ERROR`。

#### 4.2.3 Crane 等待到位（`CRANE_WAITING`）

- 周期调用每台 Crane：`read_status({})`。
- 到位判定：`cylinder_down && valve_open`。
- 全部到位 -> `SCAN_STARTING`。
- 超时 -> `TIMEOUT`。

#### 4.2.4 扫描启动（`SCAN_STARTING`）

调用 3DVision 命令实例：

```json
{
  "cmd": "vessel.command",
  "data": {
    "addr": "host:port",
    "id": 15,
    "cmd": "scan",
    "token": "<token>"
  }
}
```

成功条件：`code=0 && status="done"`（`data` 允许为 `null`）。

#### 4.2.5 扫描执行与完成确认（`SCAN_RUNNING`）

并行启动两条通道：

- 通道 A（WS）：监听 `scanner.*` 事件，过滤规则：
  - `payload.status == "event"`
  - `payload.data.data.id == vessel_id`
  - 完成事件：`scanner.result`

- 通道 B（轮询）：周期调用 `vessellog.last`。
  - 完成判定：`logTime`（解析后 UTC）`> scan_started_at_utc - clock_skew_tolerance_ms`

完成协调：
- 任意通道先确认完成，原子置位，只处理一次结果。
- 另一通道立即取消。

#### 4.2.6 结果输出（`RESULT_OUTPUT`）

输出模式：`事件 + 状态快照`。

最小输出字段：
- `vessel_id`
- `fsm_state`
- `scan_channel` (`ws`/`poll`)
- `scan_started_at`
- `scan_completed_at`
- `scan_duration_ms`
- `scan_progress_raw` (`current`,`total`)
- `scan_progress_percent`
- `scan_result`（含 `pointCloudPath`、`volume` 等）

### 4.3 进度计算规则

- 输入：`scanner.progress` 的 `current` 与 `total`。
- 规则：`scan_progress_percent = floor(current * 100 / total)`。
- 同时保留 `scan_progress_raw`，避免舍入争议。

### 4.4 错误恢复与复位

- 成功后默认不自动复位（D-04）。
- `ERROR/TIMEOUT` 不自动恢复（D-05）。
- 外部 `reset` 后回到 `IDLE`。
- `on_error_reset=true` 时，失败路径对 Crane 发送安全动作：
  - 默认动作：`set_mode({ mode: "manual" })`

> 注：当前需求不依赖 3DVision 的“扫描停止命令”；超时按 `TIMEOUT` 处理。

---

## 5. 接口约定

### 5.1 服务对外命令

| 命令 | 说明 | 成功输出 |
|---|---|---|
| `start` | 启动一次扫描流程 | `accepted=true` |
| `get_status` | 查询当前状态快照 | 状态对象 |
| `reset` | 仅在 `ERROR/TIMEOUT` 清理上下文恢复 | `reset=true` |
| `stop` | 可选；中止活动流程并释放资源 | `stopped=true` |

### 5.2 服务配置（BinScanOrchestratorConfig）

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `vessel_id` | int | 是 | - | 目标料仓 ID |
| `cranes` | array | 是 | - | Crane 列表，至少 1 台 |
| `vision_cmd` | object | 是 | - | 3DVision 命令驱动引用 |
| `vision_ws` | object | 是 | - | 3DVision WS 驱动引用 |
| `3dv_addr` | string | 是 | - | `host:port` |
| `3dv_username` | string | 是 | - | 登录用户名 |
| `3dv_password` | string | 是 | - | 登录密码 |
| `3dv_view_mode` | bool | 否 | `false` | 登录 viewMode |
| `crane_set_mode_timeout_ms` | int | 否 | `5000` | Crane 模式切换超时 |
| `crane_poll_interval_ms` | int | 否 | `1000` | Crane 轮询间隔 |
| `crane_wait_timeout_ms` | int | 否 | `60000` | Crane 到位等待超时 |
| `scan_start_timeout_ms` | int | 否 | `8000` | 扫描启动超时 |
| `scan_start_retry_count` | int | 否 | `2` | 扫描启动重试次数 |
| `scan_timeout_ms` | int | 否 | `120000` | 扫描总超时 |
| `scan_poll_interval_ms` | int | 否 | `3000` | `vessellog.last` 轮询间隔 |
| `scan_poll_fail_limit` | int | 否 | `5` | 轮询连续失败上限 |
| `ws_reconnect_max_retries` | int | 否 | `5` | WS 重连次数 |
| `ws_reconnect_interval_ms` | int | 否 | `2000` | WS 重连间隔 |
| `clock_skew_tolerance_ms` | int | 否 | `2000` | `logTime` 比较容忍偏差 |
| `crane_auto_reset` | bool | 否 | `false` | 成功后自动复位 |
| `on_error_reset` | bool | 否 | `true` | 失败后执行安全复位动作 |
| `log_level` | string | 否 | `info` | 日志级别 |

Crane 子配置：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| `crane_id` | string | 是 | - | Crane 标识 |
| `driver_ref` | object | 是 | - | Crane 驱动引用 |

### 5.3 PLC Crane 驱动约定（与真实驱动一致）

- `set_mode({mode:"auto"|"manual"})`
- `read_status({}) -> {cylinder_up, cylinder_down, valve_open, valve_closed}`

服务内部到位映射：
- `in_position = cylinder_down && valve_open`

### 5.4 3DVision 驱动约定（与真实驱动一致）

- `login`
- `vessel.command`（`cmd="scan"`）
- `vessellog.last`
- `ws.connect`
- `ws.subscribe(topic="vessel.notify")`

WS 完成事件：`scanner.result`。

---

## 6. 边界条件与异常处理

### 6.1 配置边界

- `vessel_id <= 0` -> 初始化失败。
- `cranes` 为空 -> 初始化失败。
- `crane_wait_timeout_ms <= crane_poll_interval_ms` -> 初始化失败。
- `scan_poll_interval_ms <= 0` -> 初始化失败。

### 6.2 并发与重入

- 活动流程期间重复 `start` -> 返回 `busy`。
- `get_status` 任意状态可调用。
- `stop/reset` 要求幂等。

### 6.3 Crane 异常

- 任一 Crane `set_mode` 或轮询失败 -> 强一致中止（D-02）。
- 部分到位其余超时 -> `TIMEOUT`。

### 6.4 3DVision 异常

- `vessel.command(scan)` 失败重试后仍失败 -> `ERROR`。
- WS 断开：重连 + 轮询兜底。
- WS 与轮询都不可用 -> `ERROR`。
- 收到非目标 `vessel_id` 事件 -> 忽略。

### 6.5 时间与结果判定边界

- `logTime` 使用 3DVision 服务器时区解析后转换 UTC 比较。
- 当 `abs(logTime - scan_started_at)` 在容忍窗口内，按“本次扫描结果”处理。

---

## 7. Project 配置要求

项目需实例化：
- `plc.crane` 驱动：N 台（至少 1 台）
- `3dvision.api` 驱动：2 台（Cmd/WS 分离，D-03）
- `bin_scan_orchestrator` 服务：1 个

示例（关键结构）：

```json
{
  "drivers": {
    "crane_a": { "type": "plc.crane" },
    "vision_cmd": { "type": "3dvision.api" },
    "vision_ws": { "type": "3dvision.api" }
  },
  "services": {
    "bin_scan_orchestrator": {
      "type": "BinScanOrchestrator",
      "config": {
        "vessel_id": 15,
        "cranes": [{ "crane_id": "a", "driver_ref": "crane_a" }],
        "vision_cmd": { "driver_ref": "vision_cmd" },
        "vision_ws": { "driver_ref": "vision_ws" },
        "3dv_addr": "localhost:6100",
        "3dv_username": "admin",
        "3dv_password": "123456"
      }
    }
  }
}
```

---

## 8. 验收标准

### 8.1 功能验收用例

| 编号 | 用例 | 预期 |
|---|---|---|
| TC-01 | 单 Crane 正常流程 | 成功进入 `IDLE`，产出结果 |
| TC-02 | 多 Crane 全到位后启动扫描 | 未全到位前不得调用 `vessel.command(scan)` |
| TC-03 | 任一 Crane 故障 | 整体中止，状态 `ERROR` |
| TC-04 | WS 广播混入其他 `vessel_id` | 非目标事件不影响状态 |
| TC-05 | 进度计算一致性 | `current=11,total=30 -> 36`；`20/30 -> 66` |
| TC-06 | WS 通道先完成 | `scan_channel=ws`，轮询取消 |
| TC-07 | 轮询通道先完成 | `scan_channel=poll`，WS处理取消 |
| TC-08 | 旧日志不误判 | `logTime` 早于启动时间不视为完成 |
| TC-09 | WS 断线后轮询兜底 | 扫描可完成并输出结果 |
| TC-10 | 双通道同时命中 | 仅输出一次结果（幂等） |
| TC-11 | 扫描超时 | 状态 `TIMEOUT` |
| TC-12 | `ERROR/TIMEOUT` 不自动恢复 | 未 `reset` 前状态不变 |
| TC-13 | `reset` 后可重新 `start` | 回到 `IDLE` 并可再次执行 |
| TC-14 | 失败路径 `on_error_reset=true` | 发送 Crane 安全动作（manual） |

### 8.2 非功能验收

| 指标 | 要求 |
|---|---|
| 并发安全 | 同一实例单活动流程，重复 start 稳定返回 busy |
| 可观测性 | 每次状态迁移有日志（状态、时间、原因、vessel_id） |
| 时序一致性 | 完成判定可处理时钟偏差（`clock_skew_tolerance_ms`） |
| 鲁棒性 | WS 中断时可由轮询通道兜底 |

---

## 9. 变更记录

| 版本 | 日期 | 说明 |
|---|---|---|
| v1.3 | 2026-03-06 | 依据原始需求与驱动真值完成终稿优化：修复接口不一致、统一进度与时间判定、补全边界条件与可验收标准 |
