# PQW Analog Output Driver

## Overview

`stdio.drv.pqw_analog_output` 是基于 Modbus RTU 串口的品全微模拟量输出模块驱动。

- `OneShot` 模式
- 只支持串口，不支持 TCP
- 公开接口按“设备语义”暴露通信配置与输出控制，不要求上层自己记寄存器地址
- 输出值统一按 3 位小数工程量解释，驱动只负责 `×1000` 编码；实际单位由模块型号决定，为 `V` 或 `mA`

## Runtime Model

- 每条命令显式传 `port_name`
- 默认串口参数：
  - `baud_rate = 9600`
  - `parity = none`
  - `stop_bits = 1`
  - `unit_id = 1`
  - `timeout = 2000`
- `data_bits` 固定为 `8`，不在公开参数里暴露
- `set_comm_config` 例外：
  - `current_unit_id` / `current_baud_rate` / `current_parity` / `current_stop_bits` 表示当前连接参数
  - `unit_id` / `baud_rate` / `parity` / `stop_bits` 表示准备写入设备的新配置

## Public Commands

- `status`
- `get_config`
- `set_comm_config`
- `restore_defaults`
- `read_outputs`
- `write_output`
- `write_outputs`
- `clear_outputs`

## Register Mapping

- 通信配置：
  - `0x0000`：站号
  - `0x0001`：波特率编码
  - `0x0003`：通信检测时间
  - `0x0004`：校验位
  - `0x0005`：停止位
  - `0x000D`：恢复默认参数
- 输出通道：
  - `0x0064` 到 `0x0075`：1 到 18 通道输出值
  - `0x0076`：全部输出清零

## Encoding Rules

- 输出工程量：
  - `raw = round(value * 1000)`
  - `read value = raw / 1000.0`
- 通信检测时间：
  - `0` 表示关闭
  - `>0` 时要求是 `10ms` 的整数倍
  - 编码：`raw = ms / 10 + 1`
  - 解码：`ms = (raw - 1) * 10`

## Special Note

- `set_comm_config` 已将“当前连接参数”和“目标配置参数”彻底拆开，避免改站号或改串口参数时先按新参数通信

## Output Field Semantics

- `channel`
  - 通道号，范围固定为 `1..18`
  - `write_output` 里表示单个目标通道
  - `read_outputs` 返回结果里表示当前这条读数属于哪个通道
- `value`
  - 表示“工程量值”，不是寄存器整数
  - 驱动写入时会执行 `raw = round(value * 1000)`
  - 驱动读取时会执行 `value = raw / 1000.0`
  - 这就是“按 3 位小数编码”的含义：寄存器以千分之一为最小分辨率保存数据
  - 例子：
    - `0.000 -> raw 0`
    - `1.234 -> raw 1234`
    - `2.500 -> raw 2500`
    - `12.345 -> raw 12345`
- `raw_value`
  - 表示设备寄存器里的原始整数
  - 适合排查设备手册、抓包或和上位机寄存器表核对
  - 一般不建议业务侧直接传 `raw_value`，因为驱动对外统一用 `value`
- `values`
  - `write_outputs` 的批量写入数组
  - `values[0]` 对应 `start_channel`
  - `values[1]` 对应 `start_channel + 1`
  - 依次顺延，必须是连续通道
- `raw_values`
  - 与 `values` 一一对应的寄存器整数数组
  - 每个元素都由对应的 `value` 通过 `round(value * 1000)` 得到

## Unit Meaning

- 驱动本身不强行限定单位，只负责“工程量 <-> 寄存器整数”的换算
- 实际单位由你手上的模块型号决定，常见有两类：
  - 电压输出模块：单位是 `V`
  - 电流输出模块：单位是 `mA`
- 例子：
  - 如果型号是 `0-10V` 输出模块，那么 `value = 2.500` 表示 `2.500V`
  - 如果型号是 `4-20mA` 输出模块，那么 `value = 12.000` 表示 `12.000mA`
- 所以接手人判断 `value` 含义时，要先确认当前硬件型号和量程，而不是只看驱动字段名

## Error Codes

| Code | Meaning |
|------|---------|
| `0` | 成功 |
| `1` | 串口打开失败等 transport 错误 |
| `2` | Modbus 超时、CRC、异常响应等 protocol 错误 |
| `3` | 业务参数非法 |
| `400` | 框架元数据自动参数校验失败 |
| `404` | 未知命令 |

## Key Source Paths

- `src/drivers/driver_pqw_analog_output/handler.cpp`
- `src/drivers/driver_pqw_analog_output/handler.h`
- `src/tests/test_pqw_analog_output.cpp`
- `src/tests/test_driver_manager_scanner.cpp`
- `src/smoke_tests/m105_pqw_analog_output.py`

## Verification

- `meta.describe` 与 `--export-meta` 都必须暴露 8 个公开命令
- `write_output` / `write_outputs` 必须覆盖 `value -> raw` 编码
- `set_comm_config` 必须覆盖 `comm_watchdog_ms` 的 `10ms` 约束
- `DriverManagerScanner` 必须能导出 `driver.meta.json`
