# 里程碑 97：PLC 升降装置仿真驱动（单命令版）

> **前置条件**: M79（`driver_modbustcp_server` 的 `run` 命令语义与事件流）
> **目标**: 实现 `driver_plc_crane_sim`，仅保留一条 `run` 命令；其余行为由 `run` 参数配置，启动后只通过 ModbusTCP 通讯

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| 仿真驱动 (C++) | `stdio.drv.plc_crane_sim` 可执行文件，包含状态机、寄存器映射、ModbusTCP 监听、单一 `run` 命令 |
| 仿真 Service (JS) | `services/plc_crane_sim/`（manifest + schema + index.js），启动时注入参数并调用一次 `run` |
| Demo 配置 | `demo/projects/` 下可直接导入的单机/多实例模板 |

- 新增 `driver_plc_crane_sim` 驱动，输出可执行文件 `stdio.drv.plc_crane_sim`
- 驱动仅暴露一条 stdio 命令：`run`
- 运行参数（端口、unit id、延迟、事件模式）全部通过 `run` 命令参数传入
- `run` 语义对齐 `driver_modbustcp_server`：启动后不返回 `done`，改为持续事件输出
- 启动后不再依赖 stdio 业务接口；外部仅通过 ModbusTCP 读写寄存器通讯
- 单元测试、冒烟测试覆盖 `run` 生命周期、参数校验、Modbus 寄存器映射与跨进程通信主链路

## 2. 背景与问题

- 现有方案文档包含大量业务命令/仿真命令，接口面过大，实施与维护成本高
- 对于调度联调场景，本质需求是“启动一个可通过 ModbusTCP 访问的仿真 PLC”，而非运行期命令交互
- 多命令控制路径会引入一致性风险（stdio 状态与 Modbus 寄存器状态竞争）

**范围**:
- `sim_device.h/cpp`：状态机、寄存器模型、寄存器写入到状态转换逻辑
- `handler.h/cpp`：仅实现 `run` 命令与事件流
- `main.cpp`：标准 `DriverCore` 入口
- `modbus_tcp_server` 对接：监听、读写回调、事件上报
- `services/plc_crane_sim/`：Service 组装 `run` 参数并一次性调用 `run`
- `demo/projects/`：Project 配置模板
- 单元测试 + 冒烟测试 + 集成测试

**非目标**:
- 不提供 `status/read_status/cylinder_control/...` 等业务命令
- 不提供 `sim_get_state/sim_set_state/sim_reset/sim_inject_fault` 等运行期仿真命令
- 不提供运行期通过 stdio 修改配置参数的能力
- 不修改 `driver_plc_crane` 现有实现
- 不做电气级高精度仿真（PWM/噪声/温漂）

## 3. 技术要点

### 3.1 仿真设备模型与寄存器映射

关键数据结构：

```cpp
// sim_device.h
#pragma once
#include <QObject>
#include <QTimer>

class SimPlcCraneDevice : public QObject {
    Q_OBJECT
public:
    struct Config {
        int cylinderUpDelayMs = 2500;
        int cylinderDownDelayMs = 2000;
        int valveOpenDelayMs = 1500;
        int valveCloseDelayMs = 1200;
    };

    explicit SimPlcCraneDevice(const Config& cfg, QObject* parent = nullptr);

    // Modbus 写 HR 回调入口
    bool writeHoldingRegister(quint16 address, quint16 value, QString& err);

    // Modbus 读寄存器快照
    QJsonObject snapshot() const;

private:
    void applyCylinderAction(quint16 value);  // HR[0]
    void applyValveAction(quint16 value);     // HR[1]
    void applyRunAction(quint16 value);       // HR[2]
    void applyModeAction(quint16 value);      // HR[3]
};
```

寄存器契约（对外 Modbus 协议）：

| 地址 | 类型 | 含义 | 写入值 |
|------|------|------|--------|
| HR[0] | holding | 气缸控制 | `0=stop,1=up,2=down` |
| HR[1] | holding | 阀门控制 | `0=stop,1=open,2=close` |
| HR[2] | holding | 运行控制 | `0=stop,1=start` |
| HR[3] | holding | 模式 | `0=manual,1=auto` |
| HR[9] | holding | 气缸上到位 | 只读 |
| HR[10] | holding | 气缸下到位 | 只读 |
| HR[13] | holding | 阀门开到位 | 只读 |
| HR[14] | holding | 阀门关到位 | 只读 |

状态机（气缸）流程：

```text
AT_BOTTOM --(HR0=1)--> MOVING_UP --(T_up)--> AT_TOP
AT_TOP    --(HR0=2)--> MOVING_DOWN --(T_down)--> AT_BOTTOM
MOVING_*  --(HR0=0)--> IDLE
```

### 3.2 单一 `run` 命令契约（对齐 modbustcp server）

错误码策略：

| 错误码 | 含义 | 触发场景 |
|--------|------|----------|
| 0 | 成功 | `run` 成功启动（通过 `event` 上报） |
| 1 | 启动失败 | 端口监听失败/初始化失败 |
| 3 | 参数错误 | `run` 重复调用、Driver 侧参数检查失败 |
| 400 | 参数校验失败 | Meta 自动校验失败（越界/类型不匹配） |
| 404 | 未知命令 | 非 `run` 命令 |

Handler 分发（仅 `run`）：

```cpp
void SimPlcCraneHandler::handle(const QString& cmd, const QJsonValue& data, IResponder& resp) {
    QJsonObject p = data.toObject();
    if (cmd != "run") {
        resp.error(404, QJsonObject{{"message", "Unknown command: " + cmd}});
        return;
    }
    handleRun(p, resp);
}
```

`run` 行为约定（与 `driver_modbustcp_server` 一致）：
- 首次调用：启动 ModbusTCP 监听与状态机调度，发送 `event("started", ...)`
- 不调用 `resp.done(...)`，进程保持运行并持续输出事件（如心跳、读写事件）
- 再次调用：返回 `error(3, "Server already running")`

事件示例：

```json
{"event":{"code":0,"data":{"event":"started","data":{"listen_port":1502,"unit_id":1}}}}
```

```json
{"event":{"code":0,"data":{"event":"sim_heartbeat","data":{"uptime_s":3}}}}
```

### 3.3 `run` 参数契约与校验

`run` 参数通过 meta 定义并由框架自动填默认值；调用侧通过 `driver.$rawRequest("run", runParams)` 传入：

| 参数 | 默认值 | 校验 |
|------|--------|------|
| `listen_address` | `""` | 字符串 |
| `listen_port` | `1502` | `1..65535` |
| `unit_id` | `1` | `1..247` |
| `data_area_size` | `256` | `32..65536` |
| `cylinder_up_delay` | `2500` | `0..30000` |
| `cylinder_down_delay` | `2000` | `0..30000` |
| `valve_open_delay` | `1500` | `0..30000` |
| `valve_close_delay` | `1200` | `0..30000` |
| `tick_ms` | `50` | `10..1000` |
| `heartbeat_ms` | `1000` | `100..10000` |
| `event_mode` | `write` | `write/read/all/none` |

关键入口：

```cpp
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    SimPlcCraneHandler handler;
    stdiolink::DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
```

### 3.4 生命周期与通信边界

```text
Service 启动实例
  -> openDriver(driverPath, [])
  -> rawRequest("run", runParams)
  -> 驱动启动 ModbusTCP server 并输出 started event
  -> 外部系统仅通过 ModbusTCP 读写
  -> 驱动按 event_mode 输出读写事件/心跳
  -> 进程退出时统一释放 socket 与定时器
```

边界约束：
- `run` 之后不要求、也不依赖任何后续 stdio 命令
- 业务状态以 Modbus 寄存器写入为唯一输入源，避免双写冲突
- Service 层不暴露旧业务命令透传接口

### 3.5 向后兼容与影响面

- 新增独立驱动与 Service，不破坏现有 `driver_plc_crane` 与 `driver_modbustcp_server`
- 对外协议变化限定在新 Service `plc_crane_sim`，老项目不受影响
- 文档与配置模板升级为“单命令 + run 参数”模式

## 4. 实现步骤

### 4.1 新增 `sim_device.h/.cpp`（状态机与寄存器）

- 新增 `src/drivers/driver_plc_crane_sim/sim_device.h`
- 新增 `src/drivers/driver_plc_crane_sim/sim_device.cpp`
- 关键改动：
  - 实现 `writeHoldingRegister(address, value, err)`
  - 实现状态机定时推进与 DI 派生
  - 提供 `snapshot()` 供事件与调试使用

关键伪代码：

```cpp
bool SimPlcCraneDevice::writeHoldingRegister(quint16 address, quint16 value, QString& err) {
    switch (address) {
    case 0: applyCylinderAction(value); return true;
    case 1: applyValveAction(value); return true;
    case 2: applyRunAction(value); return true;
    case 3: applyModeAction(value); return true;
    default:
        err = QString("Unsupported holding register address: %1").arg(address);
        return false;
    }
}
```

改动理由：业务行为与协议映射集中到单一设备层，便于测试与复用。
验收方式：单元测试 T04-T08 覆盖所有映射分支。

### 4.2 新增 `handler.h/.cpp`（仅 `run` 命令）

- 新增 `src/drivers/driver_plc_crane_sim/handler.h`
- 新增 `src/drivers/driver_plc_crane_sim/handler.cpp`
- 关键改动：
  - `buildMeta()` 仅声明 1 个命令 `run`
  - `handle()` 仅分发 `run`
  - `handleRun()` 启动 ModbusTCP server，并连接读写事件到 `StdioResponder`
  - 增加 `m_heartbeatTimer` 周期发送 `sim_heartbeat`

关键声明：

```cpp
class SimPlcCraneHandler : public IMetaCommandHandler {
public:
    SimPlcCraneHandler();
    const DriverMeta& driverMeta() const override { return m_meta; }
    void handle(const QString& cmd, const QJsonValue& data, IResponder& resp) override;
private:
    void handleRun(const QJsonObject& data, IResponder& resp);
    void buildMeta();

    DriverMeta m_meta;
    SimRunConfig m_cfg;
    SimPlcCraneDevice m_device;
    ModbusTcpServer m_server;
    StdioResponder m_eventResponder;
    QTimer m_heartbeatTimer;
    bool m_running = false;
};
```

改动理由：与需求“单命令、启动后只走 ModbusTCP”完全一致。
验收方式：T01-T03、T09-T10、S01-S03。

### 4.3 新增 `main.cpp`（DriverCore 入口）

- 新增 `src/drivers/driver_plc_crane_sim/main.cpp`
- 关键改动：
  - 不做业务参数解析，仅作为 `DriverCore` 标准入口
  - 构造 `SimPlcCraneHandler()` 后进入 `DriverCore::run(argc, argv)`

改动理由：参数定义与校验统一收敛到 `run` meta + `handleRun`，对齐同类驱动模式。
验收方式：S04、T11。

### 4.4 新增 `CMakeLists.txt` 与驱动注册

- 新增 `src/drivers/driver_plc_crane_sim/CMakeLists.txt`
- 修改 `src/drivers/CMakeLists.txt`，追加：

```cmake
add_subdirectory(driver_plc_crane_sim)
```

改动理由：纳入统一构建与发布产物。
验收方式：构建后存在 `stdio.drv.plc_crane_sim(.exe)`，且 `--export-meta` 可执行。

### 4.5 新增 Service 与 Project 模板

- 新增 `src/demo/server_manager_demo/data_root/services/plc_crane_sim/manifest.json`
- 新增 `src/demo/server_manager_demo/data_root/services/plc_crane_sim/config.schema.json`
- 新增 `src/demo/server_manager_demo/data_root/services/plc_crane_sim/index.js`
- 新增 `src/demo/server_manager_demo/data_root/projects/plc_crane_sim_single.json`
- 新增 `src/demo/server_manager_demo/data_root/projects/plc_crane_sim_cluster.json`

`index.js` 约束：
- 仅在启动时调用一次 `driver.$rawRequest("run", runParams)`
- 不暴露业务命令调用（无 `driver.status()/driver.cylinder_control()` 等）
- 所有设备行为通过 ModbusTCP 客户端访问驱动监听地址
- `config.schema.json` 与 project 配置使用 snake_case 字段，直接映射 `run` 参数名

改动理由：确保 Service 行为与驱动接口严格一致，避免“文档支持但实现不支持”的漂移。
验收方式：S02、S03、E2E-01。

## 5. 文件清单

### 5.1 新增文件

- `src/drivers/driver_plc_crane_sim/CMakeLists.txt` - 新驱动构建配置
- `src/drivers/driver_plc_crane_sim/sim_device.h` - 仿真设备声明（状态机/寄存器）
- `src/drivers/driver_plc_crane_sim/sim_device.cpp` - 仿真设备实现
- `src/drivers/driver_plc_crane_sim/handler.h` - 单命令 Handler 声明
- `src/drivers/driver_plc_crane_sim/handler.cpp` - 单命令 Handler 实现
- `src/drivers/driver_plc_crane_sim/main.cpp` - DriverCore 入口
- `src/demo/server_manager_demo/data_root/services/plc_crane_sim/manifest.json` - Service 描述
- `src/demo/server_manager_demo/data_root/services/plc_crane_sim/config.schema.json` - Service 参数 Schema
- `src/demo/server_manager_demo/data_root/services/plc_crane_sim/index.js` - Service 运行脚本（一次性 run）
- `src/demo/server_manager_demo/data_root/projects/plc_crane_sim_single.json` - 单实例模板
- `src/demo/server_manager_demo/data_root/projects/plc_crane_sim_cluster.json` - 多实例模板
- `src/tests/test_plc_crane_sim.cpp` - 单元测试
- `src/smoke_tests/m97_plc_crane_sim_smoke.py` - 冒烟测试

### 5.2 修改文件

- `src/drivers/CMakeLists.txt` - 注册 `driver_plc_crane_sim`
- `src/tests/CMakeLists.txt` - 纳入 `test_plc_crane_sim.cpp`
- `src/smoke_tests/run_smoke.py` - 注册 `m97_plc_crane_sim`
- `src/smoke_tests/CMakeLists.txt` - 注册 CTest 入口

### 5.3 说明

- 本里程碑接口面收敛为 1 条命令；不再新增其它 Handler 命令文件。

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: `SimPlcCraneDevice`、`SimPlcCraneHandler`
- 用例分层: 正常路径（T01-T05）、边界值（T06-T08）、异常路径（T09-T11）、回归（R01）
- 断言要点: 错误码、事件输出、寄存器映射、状态机落点、生命周期幂等性
- 桩替身策略: `MockResponder` 捕获 `event/error/done`；使用本地回环端口与短定时器避免外部依赖
- 测试文件: `src/tests/test_plc_crane_sim.cpp`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `handle(cmd)` | `cmd==run` | T01 |
| `handle(cmd)` | `cmd!=run` -> `404` | T09 |
| `run` | 首次启动成功 -> `started event` | T01 |
| `run` | 重复调用 -> `error(3)` | T02 |
| `run` | 监听失败（端口占用）-> `error(1)` | T10 |
| `writeHoldingRegister` | 地址 `0..3` 合法 | T03-T05 |
| `writeHoldingRegister` | 地址越界 -> false + err | T06 |
| `applyCylinderAction` | 值非法 -> 拒绝并保持状态 | T07 |
| `run` 参数校验 | 参数越界 -> 进程错误退出（3/400） | T11 |
| 心跳事件 | 运行中周期输出 | T08 |
| 回归：无额外 stdio 命令依赖 | 仅 `run` 后 Modbus 可用 | R01 |

覆盖要求（硬性）: 核心决策路径 `100%`（`run` 生命周期、命令分发、寄存器映射）有用例；非核心路径按风险分级覆盖；不可达路径在代码注释与矩阵中明确证明。

#### 执行约束

- 验收标准引用用例必须执行，禁止 `DISABLED_` 与 `skip`
- 失败路径均由本地可控条件触发（占用端口、非法参数、非法地址）

#### 用例详情

**T01 — run 首次启动成功**
- 前置条件: 使用空闲端口构造 Handler
- 输入: `handle("run", runData, resp)`
- 预期: 输出 `started` 事件；不返回 `done`
- 断言: `resp.events` 包含 `started`；`resp.doneCalled == false`

**T02 — run 重复调用失败**
- 前置条件: 已执行 T01
- 输入: 再次 `handle("run", runData, resp)`
- 预期: 返回 `error(3, "Server already running")`
- 断言: `resp.lastErrorCode == 3`

**T03 — HR[0] 写入 up 触发上升状态机**
- 前置条件: 运行中，设备初始 AT_BOTTOM
- 输入: `writeHoldingRegister(0, 1)`
- 预期: 状态进入 `MOVING_UP`，到期后 `AT_TOP`
- 断言: 两阶段状态断言均满足

**T04 — HR[1] 写入 open 触发阀门动作**
- 前置条件: 运行中
- 输入: `writeHoldingRegister(1, 1)`
- 预期: 阀门状态 `MOVING_OPEN -> OPEN`
- 断言: 到位后 `HR[13]==1`

**T05 — HR[2]/HR[3] 写入合法值**
- 前置条件: 运行中
- 输入: `writeHoldingRegister(2,1)`、`writeHoldingRegister(3,1)`
- 预期: 写入成功并更新内部寄存器
- 断言: `snapshot().hr_run==1`、`snapshot().hr_mode==1`

**T06 — 写入越界地址失败**
- 前置条件: 运行中
- 输入: `writeHoldingRegister(99, 1)`
- 预期: 返回 false，错误信息非空
- 断言: `ok==false && !err.isEmpty()`

**T07 — HR[0] 非法值拒绝**
- 前置条件: 运行中，当前状态记为 S
- 输入: `writeHoldingRegister(0, 7)`
- 预期: 拒绝写入，不改变状态
- 断言: `stateAfter == S`

**T08 — 心跳事件持续输出**
- 前置条件: 已执行 run
- 输入: 等待 2 个心跳周期
- 预期: 至少收到 2 条 `sim_heartbeat`
- 断言: `count(heartbeat) >= 2`

**T09 — 未知命令返回 404**
- 前置条件: Handler 已构造
- 输入: `handle("status", {}, resp)`
- 预期: `error(404)`
- 断言: `resp.lastErrorCode == 404`

**T10 — 端口占用导致 run 失败**
- 前置条件: 本地先占用目标端口
- 输入: `handle("run", runData, resp)`
- 预期: `error(1)`
- 断言: `resp.lastErrorCode == 1`

**T11 — run 参数越界返回错误退出码**
- 前置条件: 子进程模式运行驱动
- 输入: `--mode=console --cmd=run --listen_port=70000`
- 预期: 进程退出码为参数错误（`3` 或 `400`）
- 断言: exit code 命中允许集合

**R01 — 单命令回归路径**
- 前置条件: 启动 Service，调用一次 `run`
- 输入: 通过 ModbusTCP 客户端执行 HR 写入/HR 读取
- 预期: 无需任何额外 stdio 命令即可完成控制与反馈
- 断言: 读写链路全部成功

#### 测试代码（示意）

```cpp
TEST(PlcCraneSimHandler, T01_RunStartsAndStreamsEvents) {
    SimPlcCraneHandler handler;
    MockResponder resp;
    QJsonObject runData{{"listen_port", findFreePort()}, {"unit_id", 1}};

    handler.handle("run", runData, resp);

    EXPECT_FALSE(resp.doneCalled);
    EXPECT_TRUE(resp.hasEvent("started"));
}

TEST(PlcCraneSimHandler, T09_UnknownCommand404) {
    SimPlcCraneHandler handler;
    MockResponder resp;

    handler.handle("status", QJsonObject{}, resp);

    EXPECT_EQ(resp.lastErrorCode, 404);
}
```

### 6.2 冒烟测试脚本

- 脚本目录: `src/smoke_tests/`
- 脚本文件: `m97_plc_crane_sim_smoke.py`
- 统一入口: `python src/smoke_tests/run_smoke.py --plan m97_plc_crane_sim`
- CTest 接入: `smoke_m97_plc_crane_sim`
- 覆盖范围: 可执行产物、meta、run 生命周期、ModbusTCP 主链路、关键失败路径
- 用例清单:
  - `S01`: `--export-meta` 仅包含 `run` 命令 -> 断言命令数量为 1
  - `S02`: 启动并发送 `run` -> 断言收到 `started`，且无 `done`
  - `S03`: 用 Modbus 客户端写 `HR[0]=1`，读 `HR[9]` -> 断言状态变化生效
  - `S04`: 非法 `run` 参数（`--listen_port=70000`）-> 断言退出码 `3` 或 `400`
  - `S05`: 端口冲突 -> 断言 `run` 返回 `error(1)`
- 失败输出规范: 打印 stdout/stderr、退出码、超时点位
- 环境约束与跳过策略: 无公网依赖；若本地缺少可执行文件则直接 FAIL
- 产物定位契约: `build/debug/` 或 `build/runtime_debug/data_root/drivers/stdio.drv.plc_crane_sim/`
- 跨平台运行契约: UTF-8 编码，路径通过构建目录动态拼接

### 6.3 集成/端到端测试

- E2E-01: Server Manager 发现 `plc_crane_sim` Service，启动项目后自动调用 `run`
- E2E-02: 3 实例并行运行，端口互不冲突，寄存器状态互相隔离
- E2E-03: WebUI 不再出现旧业务命令按钮（仅展示运行状态与连接信息）

### 6.4 验收标准

- [ ] `--export-meta` 输出合法 JSON，且仅 1 条命令 `run`（S01）
- [ ] `run` 成功后输出 `started` 事件且不返回 `done`（T01, S02）
- [ ] `run` 重复调用返回 `error(3)`（T02）
- [ ] 启动后仅通过 ModbusTCP 读写即可驱动状态机（T03-T05, R01, S03）
- [ ] 非法寄存器地址和非法动作值被拒绝并可观测（T06, T07）
- [ ] 非法 `run` 参数返回错误退出码（T11, S04）
- [ ] 端口冲突时 `run` 返回 `error(1)`（T10, S05）

### 6.5 回归测试

- 运行 `ctest --test-dir build --output-on-failure`
- 运行 `python src/smoke_tests/run_smoke.py --plan m97_plc_crane_sim`
- 验证现有 `driver_modbustcp_server` 冒烟计划不受影响

## 7. 风险与控制

- 风险: 仅 `run` 命令导致调试可观测性下降
  - 控制: 增加 `event_mode=all` 与 `sim_heartbeat`，保证运行态可观测
- 风险: Modbus 寄存器映射与既有 PLC 约定偏差
  - 控制: 在单元测试中固化地址/枚举映射断言（T03-T07）
- 风险: 端口/多实例管理冲突
  - 控制: Project 模板强制端口显式配置；增加端口冲突测试（T10/S05）
- 风险: Service 误调用旧命令
  - 控制: `index.js` 删除旧命令路径；集成测试验证（E2E-03）

## 8. 里程碑完成定义（DoD）

- [ ] 驱动构建成功并产出 `stdio.drv.plc_crane_sim`
- [ ] Meta 导出仅含 `run` 命令
- [ ] `run` 参数校验完整，非法输入按约定失败
- [ ] `run` 行为与 `driver_modbustcp_server` 对齐（启动事件 + 持续运行）
- [ ] 启动后仅通过 ModbusTCP 完成设备交互
- [ ] 单元测试与冒烟测试全部通过
- [ ] Demo Service/Project 可被 Server Manager 发现并启动

## 9. 决策状态

### 9.1 已确认决策

- **D01**：仿真驱动对外 stdio 接口仅保留 `run`（A）
- **D02**：除 `run` 外的行为配置全部通过 `run` 参数传入（A）
- **D03**：`run` 语义参考 `driver_modbustcp_server`，启动成功后发送事件并保持持续输出，不返回 `done`（A）
- **D04**：驱动启动后不再进行其他 stdio 接口交互，外部仅通过 ModbusTCP 通讯（A）

## 10. 依赖关系（附录）

- 依赖里程碑: `M79`（`driver_modbustcp_server`）
  - 复用内容: `run` 生命周期语义、`StdioResponder` 事件流、ModbusTCP server 组件
- 依赖模块: `stdiolink::driver`（`DriverCore`、`DriverMetaBuilder`、`IMetaCommandHandler`）
  - 兼容约束: 遵守 JSONL 协议与 meta 导出规范
