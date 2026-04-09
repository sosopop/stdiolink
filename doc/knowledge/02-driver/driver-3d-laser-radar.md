# 3D Laser Radar Driver

## Overview

`stdio.drv.3d_laser_radar` 是基于协议文档《三维激光雷达_控制协议_V10_2》实现的 `OneShot` TCP Driver。

- 仅支持 `profile = oneshot`
- 仅覆盖文档正文明确约定的指令：`1/2/3/4/5/6/7/8/11/12/15/16`
- `v3dlaserproto.h` 中遗留但文档未展开的 `9/10/13/14` 不作为公开命令
- 扫描类数据只保存原始二进制文件，不在 JSON 响应里回传大块原始字节
- 不复刻旧业务层里的自动切模式、自动校准、点云重建或坐标变换

## Runtime Model

- 传输：`QTcpSocket`
- 设备地址：固定 `0`，不暴露 `device_addr`
- 连接参数：
  - `host = 127.0.0.1`
  - `port = 23`
  - `timeout_ms = 5000`
  - `query_interval_ms = 1000`
  - `scan_field` 未显式传 `query_interval_ms` 时，默认使用 `5000`
  - `task_timeout_ms = -1`
- 默认长任务超时：
  - `calib_x = 300000ms`
  - `calib_lidar = 60000ms`
  - `move_x = 180000ms`
  - `scan_field = 600000ms`

## Public Commands

- 本地命令：
  - `status`
- 通用命令：
  - `test`
  - `get_reg`
  - `set_reg`
  - `query`
  - `cancel`
  - `set_imaging_mode`
  - `reboot`
  - `get_data`
  - `scan_field`
- 语义化寄存器命令：
  - `get_work_mode`
  - `get_device_status`
  - `get_device_code`
  - `get_lidar_model_code`
  - `get_distance_unit`
  - `get_uptime_ms`
  - `get_firmware_version`
  - `get_data_block_size`
  - `get_x_axis_ratio`
  - `get_transfer_total_bytes`
- 高层长任务命令：
  - `calib_x`
  - `calib_lidar`
  - `move_x`

## Protocol Mapping

- 帧格式：
  - magic 固定 `LIDA`
  - 大端字段
  - `counter` 为 `uint16`
  - CRC 使用 STM32 同款 `CRC32`
- `read_reg` / `write_reg`
  - payload = `reg:uint16 + value:uint32`
  - 响应 `reg = 65535` 视为设备错误
- `query`
  - 请求 `op = 100 | 200`
  - 响应 = `last_counter:uint16 + last_cmd:uint8 + result_a:uint32 + result_b:uint32`
  - `200` 用于清空旧结果
- `cancel`
  - 响应 = `last_counter:uint16 + last_cmd:uint8 + result:uint8`
  - 语义映射：
    - `10 = can_stop`
    - `20 = cannot_stop`
    - `30 = already_stopped`
- 长任务：
  - `calib_x` / `calib_lidar` / `move_x` / `scan_field` 发送后不等待即时业务响应
  - 行为对齐旧项目 `checkNoRespCall`：发送后先等待一次“无返回超时”，再进入轮询
  - 执行前先调用 `query(200)` 清空旧状态
  - 再轮询 `query(100)`，并严格匹配“返回计数器 + 指令码”
- `scan_field`
  - 角度参数对外统一使用 `*_deg`
  - 接口层兼容 `end_x_deg` 与 `end_y_deg` 传到 `360`
  - 超出协议正文推荐范围时，设备仍可能直接返回任务失败
  - `step_x_deg` 与全部 `Y` 轴参数可省略；省略时编码为协议值 `0`
  - 完成后自动连续调用 `get_data(segment_index)` 聚合全部分段
- `get_data`
  - 只拉取单个分段，并把原始分段字节直接写入 `output`

## Output Rules

- `get_data`
  - 必填 `output`
  - 输出为单段原始二进制
- `scan_field`
  - 必填 `output`
  - 输出为聚合后的完整原始二进制字节流
- JSON 响应只返回摘要：
  - `segment_count`
  - `byte_count`
  - `result_a/result_b`
  - `has_blank_scanlines`
  - `output`

## Error Codes

| Code | Meaning |
|------|---------|
| `0` | 成功 |
| `1` | TCP 连接失败、读写失败、输出文件写入失败 |
| `2` | 协议解析失败、CRC 错误、设备返回失败、长任务失败或超时 |
| `3` | 参数非法 |
| `404` | 未知命令 |

## Key Source Paths

- `src/drivers/driver_3d_laser_radar/handler.cpp`
- `src/drivers/driver_3d_laser_radar/protocol_codec.cpp`
- `src/drivers/driver_3d_laser_radar/laser_transport.cpp`
- `src/drivers/driver_3d_laser_radar/laser_session.cpp`
- `src/tests/test_3d_laser_radar.cpp`
- `src/tests/test_driver_manager_scanner.cpp`
- `src/smoke_tests/m108_3d_laser_radar.py`

## Verification

- `meta.describe` 与 `--export-meta` 必须只暴露文档约定的命令集合
- 参数错误必须优先于 TCP 连接返回，避免因设备不可达掩盖校验失败
- `query(200)` 后必须清空旧任务状态
- 长任务轮询必须跳过“计数器或指令码不匹配”的旧结果
- `scan_field` 成功后必须聚合全量分段并保存原始字节流
- `4001` 必须作为成功态返回，并在摘要里标记 `has_blank_scanlines = true`
