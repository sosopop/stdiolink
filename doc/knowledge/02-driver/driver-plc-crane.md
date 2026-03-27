# PLC Crane Driver

## Overview

`stdio.drv.plc_crane` 是面向高炉 PLC 升降装置的 Modbus TCP 设备语义 Driver，`stdio.drv.plc_crane_sim` 提供对应仿真从站。

- 真实驱动对外仍是 `status` / `read_status` / `cylinder_control` / `valve_control` / `set_run` / `set_mode`
- 控制面继续走保持寄存器 `HR[0..3]`
- 状态面已经切到离散量输入 `DI[9/10/13/14]`
- 只有 `cylinder_up` 是低电平有效，其余状态位都是高电平有效

## Protocol Mapping

- 控制寄存器：
  - `HR[0]`：气缸动作，`0=stop` / `1=up` / `2=down`
  - `HR[1]`：阀门动作，`0=stop` / `1=open` / `2=close`
  - `HR[2]`：运行位，`0=stop` / `1=start`
  - `HR[3]`：模式位，`0=manual` / `1=auto`
- 状态输入：
  - `DI[9]`：气缸上到位，`0=到位`、`1=未到位`
  - `DI[10]`：气缸下到位，`1=到位`
  - `DI[13]`：阀门开到位，`1=到位`
  - `DI[14]`：阀门关到位，`1=到位`

## Public Semantics

- `read_status`
  - 真实驱动读取 `FC 0x02`
  - 返回字段保持为：
    - `cylinder_up = (DI9 == 0)`
    - `cylinder_down = (DI10 == 1)`
    - `valve_open = (DI13 == 1)`
    - `valve_closed = (DI14 == 1)`
- `plc_crane_sim`
  - 只在 `DI[9/10/13/14]` 上暴露状态
  - 不再维护旧的 `HR[9/10/13/14]` 状态镜像
  - 默认状态：
    - `DI9=0`
    - `DI10=0`
    - `DI13=0`
    - `DI14=1`

## Simulation Notes

- 自动模式链路仍是“阀门先开，再气缸下降”
- 到底后原始状态位应为：
  - `DI9=1`
  - `DI10=1`
  - `DI13=1`
  - `DI14=0`
- 中途停止时不要按语义布尔理解原始位，尤其 `DI9` 是低电平有效

## Key Source Paths

- `src/drivers/driver_plc_crane/handler.cpp`
- `src/drivers/driver_plc_crane_sim/handler.cpp`
- `src/drivers/driver_plc_crane_sim/sim_device.cpp`
- `src/tests/test_plc_crane.cpp`
- `src/tests/test_plc_crane_sim.cpp`
- `src/smoke_tests/m97_plc_crane_sim_smoke.py`

## Verification

- `test_plc_crane.cpp` 需要覆盖 `read_status` 对混合极性 DI 的映射
- `test_plc_crane_sim.cpp` 需要区分控制寄存器测试和离散量状态测试
- `m97_plc_crane_sim_smoke.py --case s03_modbus_rw` 需要验证默认 `DI9=0` 与下降后 `DI9/DI10=1`
