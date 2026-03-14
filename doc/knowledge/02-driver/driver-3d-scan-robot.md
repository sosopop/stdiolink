# 3D Scan Robot Driver

## Overview

`stdio.drv.3d_scan_robot` 是基于 JD3D / JD3I 二进制协议的串口驱动。

当前实现以协议文档和参考 C++ 为准，不再沿用旧 MatLab 命名。公开接口层统一使用人类可读单位：

- 角度参数用 `deg`
- 角速度参数用 `deg/s`
- 协议编码前统一 `×100`，因为线上的单位是 `0.01°` / `0.01°/s`

## Runtime Model

- `oneshot` 模式
- 每条命令必须显式携带 `port` 和 `addr`
- 不复用上一次连接状态
- 默认串口参数：
  - `baud_rate = 115200`
  - `timeout_ms = 5000`
  - `query_interval_ms = 1000`
  - `inter_command_delay_ms = 250`

## Public Commands

### Local

- `status`

### Main Channel (`JD3D`)

- `test`
- `get_addr`
- `set_addr`
- `get_mode`
- `set_mode`
- `get_temp`
- `get_state`
- `get_version`
- `get_angles`
- `get_switch_x`
- `get_switch_y`
- `get_calib_x`
- `get_calib_y`
- `calib`
- `calib_x`
- `calib_y`
- `move`
- `get_distance`
- `get_distance_at`
- `get_reg`
- `set_reg`
- `scan_line`
- `scan_frame`
- `get_data`
- `query`

### Interrupt Channel (`JD3I`)

- `interrupt_test`
- `scan_progress`
- `scan_cancel`

## Removed Legacy Names

以下旧命令名已移除，不再兼容：

- `test_com`
- `get_fw_ver`
- `get_direction`
- `get_sw0`
- `get_sw1`
- `get_calib0`
- `get_calib1`
- `calib0`
- `calib1`
- `get_dist`
- `move_dist`
- `get_line`
- `get_frame`
- `res`
- `wait`
- `insert_test`
- `insert_state`
- `insert_stop`
- `radar_get_response_time`
- 所有旧 alias，如 `dist`、`get_ver`、`get_dir`、`gr`、`sr`、`rgrt`

## Scan Parameters

### `scan_line`

- `angle_x`: `0..186`, default `0`
- `begin_y`: `1..100`, default `1`
- `end_y`: `1..100`, default `100`
- `step_y`: `0.25..99`, default `1`
- `speed_y`: `0.1..10`, default `10`

### `scan_frame`

- `begin_x`: `0..186`, default `0`
- `end_x`: `0..186`, default `180`
- `step_x`: `0.25..186`, default `5`
- `begin_y`: `1..100`, default `1`
- `end_y`: `1..100`, default `100`
- `step_y`: `0.25..99`, default `1`
- `speed_y`: `0.1..10`, default `10`

## Return Model

### Scan Commands

`scan_line` / `scan_frame` / `get_data` 返回原始聚合结果，不做点云成像或坐标系转换：

```json
{
  "task_counter": 17,
  "task_command": "scan_frame",
  "result_code": 128,
  "segment_count": 2,
  "byte_count": 128,
  "data_base64": "..."
}
```

### Query Command

`query` 返回：

```json
{
  "counter": 5,
  "command": 7,
  "result": 10
}
```

- `op = 100`: 查询最近一次结果
- `op = 200`: 重置最近一次结果

## Error Codes

| Code | Meaning |
|------|---------|
| `0` | 成功 |
| `1` | 串口打开、读写、超时等传输错误 |
| `2` | CRC、地址/命令不匹配、协议解析失败、设备失败、分段不完整等协议错误 |
| `400` | 元数据自动参数校验失败 |
| `404` | 未知命令 |

## Key Source Paths

- `src/drivers/driver_3d_scan_robot/handler.cpp`
- `src/drivers/driver_3d_scan_robot/protocol_codec.h`
- `src/drivers/driver_3d_scan_robot/protocol_codec.cpp`
- `src/drivers/driver_3d_scan_robot/radar_session.cpp`
- `src/tests/test_3d_scan_robot.cpp`
- `src/tests/test_driver_manager_scanner.cpp`
- `src/tests/helpers/fake_3d_scan_robot_device.h`
- `src/smoke_tests/m103_3d_scan_robot.py`

## Verification

- `meta.describe` 必须只暴露新命令名
- `--export-meta` / `driver.meta.json` 必须和 `meta.describe` 一致
- 扫描命令测试必须校验参数在线上编码前已经做了 `×100`
- 二进制流式协议至少覆盖：
  - 分包
  - 粘包
  - 通道错配
  - CRC / 地址 / 命令不匹配
