# 里程碑 101：料仓扫描联动服务（BinScanOrchestrator，现有框架版）

> **前置条件**: M86（`resolveDriver` 绑定）、M89（统一 runtime 布局）、`stdio.drv.plc_crane`、`stdio.drv.3dvision`
> **目标**: 在不新增底层 runtime / server / driver 能力的前提下，基于现有 `manifest.json + config.schema.json + index.js + Project` 机制交付可运行、可调度、可测试的单次料仓扫描编排服务

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| `src/data_root/services` | `bin_scan_orchestrator` Service 模板 |
| `src/data_root/projects` | `manual` / `fixed_rate` Project 示例 |
| `src/tests` | 基于真实 `stdiolink_service` 进程的黑盒服务测试 |
| `src/smoke_tests` | CLI / Server 主链路冒烟脚本与 CTest 接入 |
| `doc/milestone` | 与现有框架一致的开发计划文档 |

- 交付一个 one-shot JS orchestrator，一次进程启动只完成一次扫描。
- 只复用现有 `getConfig`、`resolveDriver`、`openDriver`、`stdiolink/fs`、Project 调度能力，不新增底层接口。
- 支持三种加载方式：命令行直接运行、Server `manual` 调度、Server `fixed_rate` 调度。
- 编排 PLC Crane 与 3DVision 完成单料仓扫描，覆盖准备、等待到位、触发扫描、结果确认、结果落盘、失败复位。
- 提供适配 WebUI 的 `config.schema.json`，确保现有 Services / Projects 页面可直接加载。
- 单元测试覆盖核心功能场景 100%，并提供统一入口冒烟脚本。

## 2. 背景与问题

- 当前仓库已经具备 JS Service 运行框架、Driver Proxy、Project 调度、Service/Project 扫描与 WebUI 基本管理能力。
- 旧版 M101 文档把需求写成“长期驻留 + 命令接口 + 自定义业务事件”的 service，这与当前仓库真实能力不一致。
- 用户已明确本次实现不能新增底层能力，只能在现有框架基础上补 JS 与 JSON 配置，因此里程碑必须回到当前仓库真实契约。
- 当前可落地的正确方案，是把 BinScanOrchestrator 设计成 one-shot JS Service：
  - 一次进程启动对应一次扫描流程。
  - 通过 `stdiolink_service` 或 `POST /api/projects/{id}/start` 或 `fixed_rate` 周期调度触发。
  - 通过日志、退出码、结果文件、Project Runtime / Logs 页面观察结果。

### 2.1 设计原则（强约束）

- **P1: 不补底层能力**。实现只能建立在 `manifest.json + config.schema.json + index.js + Project` 机制上。
- **P2: one-shot 优先**。不做常驻 orchestrator，不做 `start/get_status/reset/stop` 服务命令。
- **P3: 现有契约优先**。凡是需求文档与当前代码/手册冲突，以现有仓库真实能力为准。
- **P4: 主链路必须本地可测**。关键失败路径必须由本地 fake/mock 稳定触发，不依赖真实硬件或公网。
- **P5: 结果可观测但不造新轮子**。统一使用 stderr 日志、退出码、结果文件、Project Runtime / Logs。

**范围**:
- 新增 `src/data_root/services/bin_scan_orchestrator/` Service 目录。
- 新增 `manual` / `fixed_rate` 两个 Project 示例。
- 基于现有驱动能力调用 `stdio.drv.plc_crane` 与 `stdio.drv.3dvision`。
- 提供本地可控的黑盒服务测试与冒烟测试方案。
- 为 WebUI 明确“Service 列表可见、Project 可配置、Start/Runtime/Logs 可观测”的验收路径。

**非目标**:
- 不新增 `stdiolink_service` 绑定、调度器或 Host 消息泵能力。
- 不新增 Server API、SSE 业务事件、Service 自定义命令接口。
- 不修改 PLC Crane / 3DVision 驱动协议。
- 不支持多料仓并发扫描。
- 不把 WebSocket 广播事件作为当前里程碑唯一完成判据。

### 2.2 实施与测试前置资产

- Service 实现前置产物为：`stdiolink_service`、`stdiolink_server`、`stdio.drv.plc_crane`、`stdio.drv.3dvision`。
- 黑盒/冒烟测试额外要求已构建 `stdio.drv.plc_crane_sim`。缺失该驱动时，对应测试必须直接报 FAIL，并输出缺失产物路径，不允许静默跳过。
- 3DVision fake HTTP 能力默认复用现有 [`src/tests/helpers/http_test_server.h`](/D:/code/stdiolink/src/tests/helpers/http_test_server.h) 作为底座，再新增 `FakeVisionServer` 夹具做脚本化响应编排；本里程碑不要求从零实现新的通用 HTTP mock 框架。
- PLC 侧默认复用现有 `stdio.drv.plc_crane_sim`，并新增 `PlcCraneSimHandle` 测试夹具封装子进程生命周期、端口分配、寄存器读取与模式断言；基线方案不要求额外实现 `FakePlcCraneServer`。

## 3. 技术要点

### 3.1 运行契约与加载方式

当前框架下，BinScanOrchestrator 的统一运行模型如下：

```text
配置来源
  ├─ CLI: --data-root / --config.xxx / --config-file
  └─ Project.config
       ↓
config.schema.json 校验 + 默认值合并
       ↓
index.js 读取 getConfig()
       ↓
resolveDriver() -> openDriver()
       ↓
一次扫描流程
       ↓
stderr 日志 + 可选 result.json + 退出码
```

三种加载方式共用同一套 Service 产物：

| 加载方式 | 触发入口 | 运行模型 | 验收观察点 |
|----------|----------|----------|------------|
| 命令行 | `stdiolink_service <service_dir> --data-root=<data_root>` | 单次执行 | 退出码、stderr、结果文件 |
| Server 手动 | `POST /api/projects/{id}/start` | 单次实例 | Runtime、Logs、实例退出码 |
| Server 定时 | `fixed_rate` | 周期性 one-shot | 多次实例日志与执行次数 |
| WebUI | 现有 Services / Projects 页面 | 配置与触发入口 | Schema 渲染、Start、Runtime、Logs |

旧文档与现有框架的冲突点必须在本里程碑中一次性收敛：

| 旧文档假设 | 现有框架事实 | 本里程碑处理 |
|------------|--------------|--------------|
| Service 有 `start/get_status/reset/stop` 命令接口 | Service 只有进程启动入口 | 改为 one-shot |
| Project 通过 `driver_ref` 绑定现有 driver 实例 | 当前 service 需要自己 `resolveDriver/openDriver` | 改为 service 自建 driver 连接 |
| 结果通过自定义业务事件流上报 | 当前只有日志、退出码、实例运行态、项目日志 | 改为日志 + 可选结果文件 |
| WebSocket 广播可作为 service 主完成通道 | 当前 JS 运行时无法稳定消费无活动 Task 的全局事件 | 主链路改为命令 + 轮询 |

执行契约补充：
- 命令行和黑盒测试场景如果使用临时 `data_root`，必须显式传 `--data-root=<temp_data_root>`。
- 只有在 Server 通过既有 data_root 启动实例时，才依赖 Server 注入的标准运行目录。
- 成功路径的退出契约为：主 IIFE 执行完成，`finally` 中关闭所有 driver，且不再保留未完成 Promise / 定时器，进程自然退出并返回 `0`。
- 失败路径的退出契约为：捕获异常后记录日志，`finally` 中关闭所有 driver，然后继续抛错交给 `stdiolink_service` runtime 生成非 `0` 退出码。

### 3.2 配置模型与 Schema

`config.schema.json` 必须使用当前项目自定义 schema，而不是 JSON Schema。建议配置模型如下：

```json
{
  "vessel_id": {
    "type": "int",
    "required": true,
    "description": "目标料仓 ID",
    "constraints": { "min": 1 }
  },
  "vision": {
    "type": "object",
    "required": true,
    "fields": {
      "addr": { "type": "string", "required": true, "description": "3DVision 地址，格式 host:port" },
      "user_name": { "type": "string", "required": true, "description": "登录用户名" },
      "password": { "type": "string", "required": true, "description": "登录密码" },
      "view_mode": { "type": "bool", "default": false, "description": "login.viewMode" }
    }
  },
  "cranes": {
    "type": "array",
    "required": true,
    "constraints": { "minItems": 1 },
    "items": {
      "type": "object",
      "fields": {
        "id": { "type": "string", "required": true },
        "host": { "type": "string", "required": true },
        "port": { "type": "int", "default": 502, "constraints": { "min": 1, "max": 65535 } },
        "unit_id": { "type": "int", "default": 1, "constraints": { "min": 1, "max": 247 } },
        "timeout_ms": { "type": "int", "default": 3000, "constraints": { "min": 100, "max": 60000 } }
      }
    }
  },
  "crane_poll_interval_ms": { "type": "int", "default": 1000, "constraints": { "min": 100, "max": 10000 } },
  "crane_wait_timeout_ms": { "type": "int", "default": 60000, "constraints": { "min": 1000, "max": 600000 } },
  "scan_request_timeout_ms": { "type": "int", "default": 8000, "constraints": { "min": 500, "max": 60000 } },
  "scan_start_retry_count": { "type": "int", "default": 2, "constraints": { "min": 0, "max": 10 } },
  "scan_start_retry_interval_ms": { "type": "int", "default": 1000, "constraints": { "min": 0, "max": 30000 } },
  "scan_poll_interval_ms": { "type": "int", "default": 3000, "constraints": { "min": 200, "max": 60000 } },
  "scan_poll_fail_limit": { "type": "int", "default": 5, "constraints": { "min": 1, "max": 100 } },
  "scan_timeout_ms": { "type": "int", "default": 120000, "constraints": { "min": 1000, "max": 1800000 } },
  "clock_skew_tolerance_ms": { "type": "int", "default": 2000, "constraints": { "min": 0, "max": 60000 } },
  "on_error_set_manual": { "type": "bool", "default": true },
  "result_output_path": { "type": "string", "default": "" }
}
```

字段约束：
- 不再引入 `vision_cmd/vision_ws/driver_ref` 之类现有框架外抽象。
- 3DVision 连接信息和 PLC 连接信息都直接放在 Project `config` 中。
- Schema 默认值必须保证 WebUI 创建 Project 时可直接得到可编辑表单。

### 3.3 编排主流程

主流程必须只依赖现有 `resolveDriver + openDriver + Proxy 调用 + 命令轮询`，并遵循 **proxy-first** 约束：普通命令默认走 `openDriver()` 返回的 Proxy；只有未来确需保留原始 Task 语义的点位，才允许额外引入 `$rawRequest()`，且必须单独说明。

```js
import { getConfig, openDriver } from "stdiolink";
import { resolveDriver } from "stdiolink/driver";
import { writeJson } from "stdiolink/fs";
import { sleep } from "stdiolink/time";
import { createLogger } from "stdiolink/log";

const vision = await openDriver(resolveDriver("stdio.drv.3dvision"));
const crane = await openDriver(resolveDriver("stdio.drv.plc_crane"));

const loginResult = await vision.login({
  addr: cfg.vision.addr,
  userName: cfg.vision.user_name,
  password: cfg.vision.password,
  viewMode: cfg.vision.view_mode
});

await crane.set_mode({ host, port, unit_id, timeout, mode: "auto" });
const status = await crane.read_status({ host, port, unit_id, timeout });
const scanResp = await vision["vessel.command"]({
  addr: cfg.vision.addr,
  token: loginResult.token,
  id: cfg.vessel_id,
  cmd: "scan"
});

const cranes = await Promise.all(
  cfg.cranes.map(() => openDriver(resolveDriver("stdio.drv.plc_crane")))
);

await Promise.all(
  cranes.map((crane, index) =>
    crane.set_mode({
      host: cfg.cranes[index].host,
      port: cfg.cranes[index].port,
      unit_id: cfg.cranes[index].unit_id,
      timeout: cfg.cranes[index].timeout_ms,
      mode: "auto"
    })
  )
);
```

```text
加载配置
  ↓
打开 3DVision 命令驱动
  ↓
login 获取 token
  ↓
打开所有 PLC Crane 驱动
  ↓
并行 set_mode(auto)
  ↓
轮询 read_status 直到全部 cylinder_down && valve_open
  ↓
vessel.command(scan)
  ↓
轮询 vessellog.last
  ├─ 命中“新日志” -> 成功
  ├─ 连续失败超限 -> 失败
  └─ 总超时 -> 失败
       ↓
可选写 result.json
       ↓
成功 exit 0 / 失败非 0
```

建议拆分的 helper（其中 `openVision()` 明确定义为返回 `{ driver, token }`）：

```js
async function openVision(visionCfg) {}
async function openCranes(craneCfgList) {}
async function prepareCranes(cranes) {}
async function waitCranesReady(cranes, cfg) {}
async function startScan(visionDriver, cfg, token) {}
async function pollScanCompleted(visionDriver, cfg, scanStartedAt) {}
async function safeSetManual(cranes) {}
async function writeResultFile(cfg, result) {}
```

实现约束：
- `login`、`set_mode`、`read_status`、`vessellog.last` 等常规命令默认使用 Proxy 方式调用。
- `vessel.command` 也优先通过 `vision["vessel.command"](...)` 调用。
- 本里程碑文档不把 `$rawRequest() + task.waitNext()` 作为默认实现路径。

Crane 就绪判定固定映射当前真实驱动字段：

```js
function isCraneReady(status) {
  return status.cylinder_down === true && status.valve_open === true;
}
```

扫描完成判定固定使用 `vessellog.last` 新鲜度比较：

```js
function isFreshLog(logInfo, scanStartedAt, toleranceMs) {
  const logTimeMs = Date.parse(String(logInfo.logTime ?? ""));
  if (!Number.isFinite(logTimeMs)) return false;
  return logTimeMs > (scanStartedAt - toleranceMs);
}
```

### 3.4 `waitAny` 与 WebSocket 的边界

当前框架中，`waitAny` 适用于“已有 Task 的多路等待”，不适合把 3DVision WebSocket 广播事件作为本里程碑主完成通道。原因如下：

- `waitAny` 监听的是 `Task` 消息队列。
- `stdio.drv.3dvision` 的 `ws.connect` 在连接建立后即返回 `done`。
- 后续 WebSocket 广播事件并不是围绕持续存活 Task 暴露的稳定主契约。
- Host 侧当前消息泵对“无活动任务时的全局 stdout 事件”没有可靠消费契约。

因此本里程碑强约束：

- 不把 WS 事件作为当前实现的成功判据。
- 不在 `index.js` 主链路中引入“WS 与轮询双通道竞态”。
- 保留未来增强空间，但本里程碑的交付基线是“命令 + 轮询”。

### 3.5 可观测性、错误处理与结果输出

本里程碑不新增 server 业务事件接口，统一使用以下输出面：

| 输出面 | 内容 | 用途 |
|--------|------|------|
| `stderr` 日志 | 阶段日志、失败原因、关键参数摘要 | CLI / Project Logs / WebUI Logs |
| 退出码 | `0=成功`，非 `0=失败` | Instance 状态、脚本判断 |
| `result_output_path` JSON | 扫描结果快照 | 下游消费、单测断言 |
| Project Runtime | 实例运行/完成状态 | Server / WebUI 可观测 |

错误处理策略：

| 错误场景 | 行为 | 退出码 | 额外动作 |
|---------|------|--------|---------|
| 配置非法 | 启动失败 | 非 `0` | 无 |
| `login` 失败 | 立即失败 | 非 `0` | 关闭已打开 driver |
| 任一 Crane `set_mode(auto)` 失败 | 立即失败 | 非 `0` | 可选切回 manual |
| Crane 等待超时 | 立即失败 | 非 `0` | 可选切回 manual |
| `vessel.command(scan)` 重试耗尽 | 立即失败 | 非 `0` | 可选切回 manual |
| `vessellog.last` 连续失败超限 | 立即失败 | 非 `0` | 可选切回 manual |
| `scan_timeout_ms` 到达 | 立即失败 | 非 `0` | 可选切回 manual |

结果 JSON 建议结构：

```json
{
  "vesselId": 15,
  "status": "success",
  "scanStartedAt": "2026-03-06T10:00:00.000Z",
  "scanCompletedAt": "2026-03-06T10:00:07.231Z",
  "scanDurationMs": 7231,
  "completionChannel": "poll",
  "visionLog": {
    "logTime": "2026-03-06 18:00:07",
    "pointCloudPath": "/data/pc/15.pcd",
    "volume": 12.34
  },
  "cranes": [
    { "id": "crane_a", "host": "127.0.0.1", "unitId": 1 }
  ]
}
```

## 4. 实现方案

### 4.1 实施顺序

1. 先完成 `manifest.json + config.schema.json`，先把 Service 契约、WebUI 表单字段和默认值固定下来。
2. 再实现 `index.js` 主流程，严格按已冻结的 schema 字段读取配置，不在实现阶段反向发明新字段。
3. 然后补 `manual` / `fixed_rate` Project 示例，验证 CLI 与 Server 使用的是同一份 service 产物。
4. 最后补黑盒服务测试与 Python 冒烟测试，测试夹具以本节约定的复用资产和新增 helper 为准。

### 4.2 Service 三件套目录

- 新增 `src/data_root/services/bin_scan_orchestrator/`
  - `manifest.json`
  - `config.schema.json`
  - `index.js`

关键片段：

```json
{
  "manifestVersion": "1",
  "id": "bin_scan_orchestrator",
  "name": "Bin Scan Orchestrator",
  "version": "1.0.0",
  "description": "One-shot bin scan orchestration service based on plc_crane and 3dvision drivers"
}
```

改动理由：
- 完全遵循当前 ServiceScanner 的真实约定，保证命令行、Server、WebUI 共用同一 service 目录。

验收方式：
- `GET /api/services` 能列出 `bin_scan_orchestrator`。
- `GET /api/services/bin_scan_orchestrator` 能返回 manifest 与 schema。

### 4.3 `config.schema.json`

- 新增 `src/data_root/services/bin_scan_orchestrator/config.schema.json`
- 使用项目自定义 schema，而不是 JSON Schema

完整文件建议如下：

```json
{
  "vessel_id": {
    "type": "int",
    "required": true,
    "description": "目标料仓 ID",
    "constraints": { "min": 1 }
  },
  "vision": {
    "type": "object",
    "required": true,
    "fields": {
      "addr": { "type": "string", "required": true, "description": "3DVision 地址，格式 host:port" },
      "user_name": { "type": "string", "required": true, "description": "登录用户名" },
      "password": { "type": "string", "required": true, "description": "登录密码" },
      "view_mode": { "type": "bool", "default": false, "description": "login.viewMode" }
    }
  },
  "cranes": {
    "type": "array",
    "required": true,
    "constraints": { "minItems": 1 },
    "items": {
      "type": "object",
      "fields": {
        "id": { "type": "string", "required": true },
        "host": { "type": "string", "required": true },
        "port": { "type": "int", "default": 502, "constraints": { "min": 1, "max": 65535 } },
        "unit_id": { "type": "int", "default": 1, "constraints": { "min": 1, "max": 247 } },
        "timeout_ms": { "type": "int", "default": 3000, "constraints": { "min": 100, "max": 60000 } }
      }
    }
  },
  "crane_poll_interval_ms": { "type": "int", "default": 1000, "constraints": { "min": 100, "max": 10000 } },
  "crane_wait_timeout_ms": { "type": "int", "default": 60000, "constraints": { "min": 1000, "max": 600000 } },
  "scan_request_timeout_ms": { "type": "int", "default": 8000, "constraints": { "min": 500, "max": 60000 } },
  "scan_start_retry_count": { "type": "int", "default": 2, "constraints": { "min": 0, "max": 10 } },
  "scan_start_retry_interval_ms": { "type": "int", "default": 1000, "constraints": { "min": 0, "max": 30000 } },
  "scan_poll_interval_ms": { "type": "int", "default": 3000, "constraints": { "min": 200, "max": 60000 } },
  "scan_poll_fail_limit": { "type": "int", "default": 5, "constraints": { "min": 1, "max": 100 } },
  "scan_timeout_ms": { "type": "int", "default": 120000, "constraints": { "min": 1000, "max": 1800000 } },
  "clock_skew_tolerance_ms": { "type": "int", "default": 2000, "constraints": { "min": 0, "max": 60000 } },
  "on_error_set_manual": { "type": "bool", "default": true },
  "result_output_path": { "type": "string", "default": "" }
}
```

改动理由：
- 让 `ProjectManager`、`ServiceConfigValidator`、WebUI 表单渲染共用一份真实契约。

验收方式：
- `stdiolink_service <service_dir> --dump-config-schema` 输出合法 schema。
- 无效 Project 会被现有校验链标记为 invalid。

### 4.4 `index.js`

- 新增 `src/data_root/services/bin_scan_orchestrator/index.js`
- 采用“函数拆分 + 顶层 IIFE”的 one-shot 结构

建议伪代码：

```js
(async () => {
  const cfg = getConfig();
  const logger = createLogger({ service: "bin_scan_orchestrator" });
  const { driver: visionDriver, token } = await openVision(cfg.vision);
  const cranes = await openCranes(cfg.cranes);

  try {
    await prepareCranes(cranes);
    await waitCranesReady(cranes, cfg);
    const scanStartedAt = await startScan(visionDriver, cfg, token);
    const visionLog = await pollScanCompleted(visionDriver, cfg, scanStartedAt);
    await writeResultFile(cfg, buildResult(cfg, scanStartedAt, visionLog));
    logger.info("scan completed", { vesselId: cfg.vessel_id });
  } catch (err) {
    if (cfg.on_error_set_manual) {
      await safeSetManual(cranes);
    }
    logger.error("scan failed", { message: String(err) });
    throw err;
  } finally {
    await closeAll(cranes, visionDriver);
  }
})();
```

具体要求：
- 使用 `resolveDriver("stdio.drv.plc_crane")` 和 `resolveDriver("stdio.drv.3dvision")`。
- 多 Crane 的 `set_mode(auto)` 和单轮 `read_status` 检查都按 `Promise.all(...)` 并发触发，不使用串行 `for-await` 作为基线实现。
- PLC 连接参数通过命令参数传入真实 driver。
- 扫描启动必须支持重试，且重试次数和间隔来自配置。
- 失败路径必须保证所有 driver `$close()` 都执行。
- 失败退出通过“记录日志后抛错”交给 `stdiolink_service` runtime 返回非 `0`，不把 `process.exitCode` 作为正式契约。
- `result_output_path` 非空时使用 `writeJson(path, result, { ensureParent: true })`。

改动理由：
- 这是里程碑的核心交付，且完全建立在现有 JS Service API 上。

验收方式：
- CLI / Server / WebUI 三条加载路径运行的是同一 `index.js`。
- 失败路径不残留失控 driver 进程。

### 4.5 Project 示例

- 新增 `src/data_root/projects/manual_bin_scan_orchestrator.json`
- 新增 `src/data_root/projects/fixed_rate_bin_scan_orchestrator.json`

关键片段：

```json
{
  "name": "Manual Bin Scan Orchestrator",
  "serviceId": "bin_scan_orchestrator",
  "enabled": false,
  "schedule": { "type": "manual" },
  "config": {
    "vessel_id": 15,
    "vision": {
      "addr": "127.0.0.1:6100",
      "user_name": "admin",
      "password": "123456",
      "view_mode": false
    },
    "cranes": [
      { "id": "crane_a", "host": "127.0.0.1", "port": 1502, "unit_id": 1, "timeout_ms": 3000 }
    ]
  }
}
```

```json
{
  "name": "Fixed Rate Bin Scan Orchestrator",
  "serviceId": "bin_scan_orchestrator",
  "enabled": true,
  "schedule": {
    "type": "fixed_rate",
    "intervalMs": 60000,
    "maxConcurrent": 1
  },
  "config": {
    "vessel_id": 15,
    "vision": {
      "addr": "127.0.0.1:6100",
      "user_name": "admin",
      "password": "123456",
      "view_mode": false
    },
    "cranes": [
      { "id": "crane_a", "host": "127.0.0.1", "port": 1502, "unit_id": 1, "timeout_ms": 3000 }
    ]
  }
}
```

改动理由：
- 明确 Server 调度和 WebUI 配置入口，避免实现后没有标准示例。

验收方式：
- `GET /api/projects` 可看到两个 Project。
- `manual` 可手动拉起实例，`fixed_rate` 示例在 `enabled=true` 且配置完整时可周期触发实例。

## 5. 文件变更清单

### 5.1 核心实现文件
- `src/data_root/services/bin_scan_orchestrator/manifest.json` - Service 元数据
- `src/data_root/services/bin_scan_orchestrator/config.schema.json` - 配置 schema
- `src/data_root/services/bin_scan_orchestrator/index.js` - one-shot 扫描编排逻辑
- `src/data_root/projects/manual_bin_scan_orchestrator.json` - 手动触发 Project 示例
- `src/data_root/projects/fixed_rate_bin_scan_orchestrator.json` - 定时触发 Project 示例
- `doc/milestone/milestone_0101_bin_scan_orchestrator.md` - 现有框架版实施计划

### 5.2 测试支撑与覆盖文件
- `src/tests/test_bin_scan_orchestrator_service.cpp` - 基于真实 `stdiolink_service` 进程的黑盒测试
- `src/tests/helpers/bin_scan_fake_vision_server.h` - 基于现有 `HttpTestServer` 的 3DVision fake server 夹具
- `src/tests/helpers/plc_crane_sim_handle.h` - 封装 `stdio.drv.plc_crane_sim` 子进程与状态断言
- `src/smoke_tests/m101_bin_scan_orchestrator.py` - 冒烟测试脚本

### 5.3 修改文件
- `src/smoke_tests/run_smoke.py` - 注册 m101_bin_scan_orchestrator 测试计划
- `src/smoke_tests/CMakeLists.txt` - 添加 CTest 目标 `smoke_m101_bin_scan_orchestrator`

### 5.4 复用的现有测试基础设施
- [`src/tests/helpers/http_test_server.h`](/D:/code/stdiolink/src/tests/helpers/http_test_server.h) - 作为 3DVision fake server 的 HTTP 底座
- `stdio.drv.plc_crane_sim` - 作为 PLC 侧黑盒/冒烟测试的现成模拟驱动

## 6. 测试与验收

### 6.1 单元测试

- **测试对象**: `index.js` 主流程在真实 `stdiolink_service` 进程中的行为
- **职责边界**: C++ 黑盒测试仅覆盖 CLI / service 进程本身（T01-T16）；Server `manual` / `fixed_rate` 主链路统一放到 Python 冒烟测试（S03-S04）
- **用例分层**: 正常路径、配置边界、轮询/超时异常、外部接口失败传播、资源清理与退出码契约
- **断言要点**: 退出码、日志、结果文件、重试次数、失败回退、副作用清理
- **桩替身策略**:
  - 3DVision 使用本地 fake HTTP server，基于现有 [`src/tests/helpers/http_test_server.h`](/D:/code/stdiolink/src/tests/helpers/http_test_server.h) 封装 `FakeVisionServer`，控制 `login` / `vessel.command` / `vessellog.last`
  - PLC 默认使用现有 `plc_crane_sim`，通过 `PlcCraneSimHandle` 封装启动、停止、寄存器读取和模式断言
  - 不依赖真实硬件或公网
- **测试文件**: `src/tests/test_bin_scan_orchestrator_service.cpp`

#### 测试工具能力说明

- `FakeVisionServer`
  - 复用 `HttpTestServer` 监听本地随机端口。
  - 提供 `enqueueLoginDone(token)`、`enqueueLoginError(msg)`、`enqueueScanDone()`、`enqueueScanError(msg)`、`enqueueLastLogNewerThanNow()`、`enqueueLastLogOlderThan(ts)` 等脚本化接口。
  - 提供 `scanCallCount()`、`lastLogCallCount()` 等计数接口，便于断言重试与轮询次数。
- `PlcCraneSimHandle`
  - 启动现有 `stdio.drv.plc_crane_sim` 子进程，自动分配端口并注入快速延时参数。
  - 支持读取关键寄存器/状态位，用于断言 `mode=manual`、`cylinder_down`、`valve_open` 等最终状态。
  - 支持通过短超时、未监听端口或第二台故障 endpoint 组合稳定触发 `set_mode`/等待失败场景。
- 基线方案不新增 `FakePlcCraneServer`。如果后续证明 `plc_crane_sim` 无法覆盖某个必须的按调用序列故障注入场景，再单独追加 follow-up 设计，不作为本里程碑前置阻塞。

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| 配置加载 | 必填字段齐全 | T01 |
| 配置加载 | `cranes=[]` 被拒绝 | T02 |
| 主流程 | 单 Crane 全流程成功 | T03 |
| Crane 准备 | 任一 `set_mode(auto)` 失败 | T04 |
| Crane 等待 | 多次轮询后成功 | T05 |
| Crane 等待 | 超时未到位 | T06 |
| 扫描启动 | 首次 `vessel.command(scan)` 成功 | T07 |
| 扫描启动 | 重试后成功 | T08 |
| 扫描启动 | 重试耗尽失败 | T09 |
| 日志轮询 | 命中新鲜日志成功 | T10 |
| 日志轮询 | 旧日志被忽略后命中新日志 | T11 |
| 日志轮询 | 连续失败达到上限 | T12 |
| 总超时 | 扫描总超时失败 | T13 |
| 失败回退 | `on_error_set_manual=true` 时切回 manual | T14 |
| 结果输出 | `result_output_path` 非空时写出 JSON | T15 |
| 退出码契约 | 成功 0 / 失败非 0 | T16 |

覆盖要求：核心功能场景 `100%` 对应用例；无公网依赖；“WS 事件先完成”不属于本里程碑可达路径。

#### 执行约束

- T03-T16 必须实际执行，不得禁用。
- 失败路径必须使用本地 fake/mock 稳定复现。
- 进程型测试必须在失败或超时后清理子进程、端口、临时目录。

#### 用例详情

**T01 — 正常初始化流程**
- 前置条件: 生成最小合法配置，包含 1 台 Crane、vision 地址、账号密码。
- 输入: 运行 `stdiolink_service <service_dir> --data-root=<temp_data_root> --config-file=<cfg.json>`。
- 预期: Service 能进入主流程。
- 断言: 进程启动成功，stderr 不包含配置校验错误。

**T02 — 空 Crane 列表被拒绝**
- 前置条件: `cranes=[]`。
- 输入: 启动 service。
- 预期: 配置校验失败。
- 断言: 退出码非 `0`，stderr 包含 `cranes` 错误语义。

**T03 — 单 Crane 成功扫描**
- 前置条件: fake 3DVision 返回 `login=done`、`scan=done`、`vessellog.last` 返回新鲜日志；PLC 模拟器可到位。
- 输入: 启动 service。
- 预期: 全流程成功。
- 断言: 退出码 `0`；stderr 含 `scan completed`；结果文件字段完整。

**T04 — 任一 Crane 切 auto 失败即终止**
- 前置条件: 两台 Crane，其中一台 `set_mode(auto)` 返回 error。
- 输入: 启动 service。
- 预期: 不再进入扫描阶段。
- 断言: 退出码非 `0`；`vessel.command(scan)` 未被调用。

**T05 — 多次轮询后到位**
- 前置条件: 前两次 `read_status` 未到位，第三次到位。
- 输入: 启动 service。
- 预期: 继续进入扫描阶段。
- 断言: `read_status` 调用次数 >= 3；最终退出成功。

**T06 — Crane 等待超时**
- 前置条件: `read_status` 始终未到位；`crane_wait_timeout_ms` 设置为小值。
- 输入: 启动 service。
- 预期: 在等待阶段失败。
- 断言: 退出码非 `0`；stderr 包含超时日志。

**T07 — 扫描首次启动成功**
- 前置条件: `vessel.command(scan)` 首次返回 done。
- 输入: 启动 service。
- 预期: 直接进入轮询阶段。
- 断言: `vessel.command(scan)` 调用次数为 1。

**T08 — 扫描重试后成功**
- 前置条件: 第一次 `vessel.command(scan)` 失败，第二次成功。
- 输入: 启动 service。
- 预期: 按配置重试后成功。
- 断言: 调用次数为 2；最终退出码 `0`。

**T09 — 扫描重试耗尽**
- 前置条件: `vessel.command(scan)` 始终失败。
- 输入: 启动 service。
- 预期: 扫描启动失败退出。
- 断言: 调用次数等于 `retry_count + 1`；退出码非 `0`。

**T10 — 轮询命中新鲜日志**
- 前置条件: `vessellog.last` 首次返回新鲜日志。
- 输入: 启动 service。
- 预期: 快速成功。
- 断言: 退出码 `0`；结果文件 `completionChannel=poll`。

**T11 — 旧日志被忽略**
- 前置条件: 第一次返回旧日志，第二次返回新日志。
- 输入: 启动 service。
- 预期: 第一次不误判，第二次成功。
- 断言: 轮询次数 >= 2；结果文件使用第二次日志内容。

**T12 — 轮询连续失败达上限**
- 前置条件: `vessellog.last` 连续返回 error。
- 输入: 启动 service。
- 预期: 达上限后失败。
- 断言: 退出码非 `0`；stderr 包含 `scan poll fail limit`。

**T13 — 总超时失败**
- 前置条件: `vessellog.last` 始终返回旧日志或空结果；`scan_timeout_ms` 设置为小值。
- 输入: 启动 service。
- 预期: 到达总超时。
- 断言: 退出码非 `0`；stderr 包含 `scan timeout`。

**T14 — 失败时切回 manual**
- 前置条件: `on_error_set_manual=true`，扫描阶段故意失败。
- 输入: 启动 service。
- 预期: 失败前对所有 Crane 执行 `set_mode(manual)`。
- 断言: `PlcCraneSimHandle` 观测到最终模式已切回 `manual`。

**T15 — 结果文件输出**
- 前置条件: `result_output_path` 指向临时文件。
- 输入: 成功执行 service。
- 预期: 输出结果 JSON。
- 断言: 文件存在；JSON 含 `vesselId/status/scanDurationMs/visionLog`。

**T16 — 退出码契约稳定**
- 前置条件: 分别准备成功与失败配置。
- 输入: 运行两次 service。
- 预期: 成功返回 `0`，失败返回非 `0`。
- 断言: 两次退出码分别满足契约。

#### 测试代码

```cpp
TEST_F(BinScanOrchestratorServiceTest, T08_ScanStartRetryThenSuccess) {
    FakeVisionServer vision;
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanError("busy");
    vision.enqueueScanDone();
    vision.enqueueLastLogNewerThanNow();

    PlcCraneSimHandle crane = startCraneSim();
    const QString configPath = writeConfig(makeConfig(vision, crane));

    RunResult r = runService(serviceDirPath(), {
        "--data-root=" + dataRootPath(),
        "--config-file=" + configPath
    });

    EXPECT_TRUE(r.finished);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_EQ(vision.scanCallCount(), 2);
    EXPECT_TRUE(r.stderrText.contains("scan start retry"));
}

TEST_F(BinScanOrchestratorServiceTest, T14_OnErrorSetManualCallsManualMode) {
    FakeVisionServer vision;
    vision.enqueueLoginDone("token-1");
    vision.enqueueScanError("fail");
    vision.enqueueScanError("fail");
    vision.enqueueScanError("fail");

    PlcCraneSimHandle crane = startCraneSim();
    const QString configPath = writeConfig(makeConfig(vision, crane, true));

    RunResult r = runService(serviceDirPath(), {
        "--data-root=" + dataRootPath(),
        "--config-file=" + configPath
    });

    EXPECT_NE(r.exitCode, 0);
    EXPECT_TRUE(crane.isManualMode());
}
```

### 6.2 冒烟测试脚本

- **脚本目录**: `src/smoke_tests/`
- **脚本文件**: `m101_bin_scan_orchestrator.py`
- **统一入口**: `python src/smoke_tests/run_smoke.py --plan m101_bin_scan_orchestrator`
- **CTest 接入**: `smoke_m101_bin_scan_orchestrator`
- **覆盖范围**: CLI 成功链路、CLI 失败链路、Server manual、Server fixed_rate
- **用例清单**:
  - `S01`: CLI 单次成功扫描 → `exit=0` 且结果文件存在
  - `S02`: CLI 扫描超时 → `exit!=0` 且日志含超时原因
  - `S03`: Server manual project → `POST /api/projects/{id}/start` 后 runtime/logs 可观测
  - `S04`: Server fixed_rate project → 周期触发至少两次实例
- **失败输出规范**: 必须输出退出码、候选可执行文件路径、关键日志摘要
- **前置条件与失败策略**:
  - 必须能定位 `stdiolink_service`、`stdiolink_server`、`stdio.drv.plc_crane_sim`
  - 若任一产物不存在，判定 FAIL，并输出缺失产物的候选路径
  - 端口冲突时脚本自动换随机端口
- **产物定位契约**:
  - `stdiolink_service`: `build/runtime_*/bin/stdiolink_service(.exe)`
  - `stdiolink_server`: `build/runtime_*/bin/stdiolink_server(.exe)`
  - `plc_crane_sim`: `build/runtime_*/data_root/drivers/stdio.drv.plc_crane_sim/stdio.drv.plc_crane_sim(.exe)`
- **跨平台运行契约**: 使用 Python subprocess，UTF-8 编码，Windows 需注入候选 `bin` 到 `PATH`

冒烟测试脚本框架：
```python
def run_s01_cli_success() -> bool:
    # 1. 启动 fake 3dvision server + plc_crane_sim
    # 2. 运行 stdiolink_service serviceDir --data-root=<temp_data_root> --config-file=...
    # 3. 断言 exit=0 且 result.json 存在
    return True

def run_s03_server_manual() -> bool:
    # 1. 启动临时 data_root 的 stdiolink_server
    # 2. POST /api/projects/{id}/start
    # 3. 轮询 /api/projects/{id}/runtime 与 /api/projects/{id}/logs
    return True
```

### 6.3 集成/端到端测试

- **CLI**: 直接运行 `stdiolink_service` 验证 service 目录自洽
- **Server**: 验证 Service 扫描、Project 校验、Instance 生命周期、Project Logs
- **WebUI**: 手动验收现有页面能读取 manifest/schema、创建 Project、点击 Start、查看 Runtime / Logs

### 6.4 验收标准

- [ ] `bin_scan_orchestrator` 能被 ServiceScanner 正确发现并在 `GET /api/services` 中返回。（S03）
- [ ] `config.schema.json` 可被 `--dump-config-schema` 正确导出，且 WebUI 可渲染表单。（T01, S03）
- [ ] 单 Crane 正常扫描主链路可在本地 fake 环境稳定通过。（T03, S01）
- [ ] 多 Crane 任一失败会立即中止，不会继续触发扫描。（T04）
- [ ] Crane 等待逻辑严格基于 `cylinder_down && valve_open`，超时失败可观测。（T05, T06）
- [ ] 扫描启动支持重试，成功与失败两条路径均有稳定测试覆盖。（T07, T08, T09）
- [ ] 扫描完成依赖 `vessellog.last` 新鲜日志判定，旧日志不会误判。（T10, T11）
- [ ] 轮询失败上限与总超时都能触发明确失败退出。（T12, T13, S02）
- [ ] 失败时可按配置把 Crane 切回 `manual`。（T14）
- [ ] 成功执行时可输出完整结果 JSON。（T15, S01）
- [ ] CLI / Server manual / Server fixed_rate 三种加载方式都可运行同一套 service 产物。（T16, S01, S03, S04）

## 7. 风险与控制

- 风险: 开发过程再次回到“命令型 service”抽象，导致实现偏离当前框架。
  - 控制: 以本文档为唯一实现基线；评审时逐项检查是否引入新底层能力。
  - 控制: 测试全部围绕 one-shot 模式设计。
  - 测试覆盖: T16, S03, S04

- 风险: 3DVision `logTime` 格式或时区处理不稳定，导致误判完成。
  - 控制: `isFreshLog()` 独立实现并覆盖“旧日志忽略”路径。
  - 控制: 结果文件保留原始 `visionLog` 便于排查。
  - 测试覆盖: T10, T11

- 风险: 失败路径未正确切回 manual，现场设备可能停留在 auto。
  - 控制: 失败路径统一进入 `safeSetManual()`。
  - 控制: `PlcCraneSimHandle` 读取模拟器状态并在测试中断言。
  - 测试覆盖: T14

- 风险: 周期调度下实例堆积。
  - 控制: `fixed_rate` 示例固定 `maxConcurrent=1`。
  - 控制: 冒烟脚本验证周期执行窗口中的实例数量。
  - 测试覆盖: S04

- 风险: 进程型测试残留子进程或占用端口。
  - 控制: 测试夹具统一封装子进程清理。
  - 控制: 超时分支执行 kill/wait + 临时目录回收。
  - 测试覆盖: T03-T16, S01-S04

## 8. 里程碑完成定义（DoD）

- [ ] `src/data_root/services/bin_scan_orchestrator/` 三件套已完成，且能被扫描发现。
- [ ] `manual` / `fixed_rate` 两个 Project 示例已完成。
- [ ] `index.js` 主流程只使用现有框架能力，无新增底层接口依赖。
- [ ] `src/tests/test_bin_scan_orchestrator_service.cpp` 已完成，T01-T16 全部通过。
- [ ] `src/smoke_tests/m101_bin_scan_orchestrator.py` 已完成并接入 `run_smoke.py` 与 CTest。
- [ ] `smoke_m101_bin_scan_orchestrator` 在目标环境执行通过，或有明确失败报告，不存在静默通过。
- [ ] WebUI 通过现有 Services / Projects / Runtime / Logs 页面可完成配置、触发与观察。
- [ ] 本文档与实际实现保持一致，不再引用旧版“命令型 service / WS 主完成通道 / driver_ref”设计。
