# 里程碑 103：3D 扫描机器人 Driver 协议对齐

> 当前状态：已按协议文档与参考 C++ 完成首轮实现对齐，本文作为当前实现约定与后续收尾基线。

## 1. 目标

- 新增并维护 `stdio.drv.3d_scan_robot`
- 以协议文档、`radar_master`、`radar_simulation` 为唯一真源
- 移除旧 MatLab 风格公开命令名和参数名
- 统一扫描参数单位语义：
  - 接口层使用 `deg` / `deg/s`
  - 协议层发送前统一 `×100`
- 不实现点云成像、坐标系转换和外部图形化处理

## 2. 当前实现范围

### 2.1 公开命令

- `status`
- `test`
- `get_addr` / `set_addr`
- `get_mode` / `set_mode`
- `get_temp`
- `get_state`
- `get_version`
- `get_angles`
- `get_switch_x` / `get_switch_y`
- `get_calib_x` / `get_calib_y`
- `calib` / `calib_x` / `calib_y`
- `move`
- `get_distance`
- `get_distance_at`
- `get_reg` / `set_reg`
- `scan_line`
- `scan_frame`
- `get_data`
- `query`
- `interrupt_test`
- `scan_progress`
- `scan_cancel`

### 2.2 已移除旧名

- `test_com`
- `get_fw_ver`
- `get_direction`
- `get_sw0` / `get_sw1`
- `get_calib0` / `get_calib1`
- `calib0` / `calib1`
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
- 旧 alias：`dist`、`state`、`get_ver`、`get_dir`、`gr`、`sr`、`rgrt`

### 2.3 非目标

- 协议 9 调试转发
- 协议 16 重启设备
- 点云成像
- 坐标系转换
- WebUI 专用扩展 API

## 3. 参数与单位约定

### 3.1 连接模型

- `oneshot`
- 每条命令显式传 `port`、`addr`
- 串口默认值：
  - `baud_rate = 115200`
  - `timeout_ms = 5000`
  - `query_interval_ms = 1000`
  - `inter_command_delay_ms = 250`

### 3.2 扫描命令

`scan_line`

- `angle_x`: `0..186`, default `0`
- `begin_y`: `1..100`, default `1`
- `end_y`: `1..100`, default `100`
- `step_y`: `0.25..99`, default `1`
- `speed_y`: `0.1..10`, default `10`

`scan_frame`

- `begin_x`: `0..186`, default `0`
- `end_x`: `0..186`, default `180`
- `step_x`: `0.25..186`, default `5`
- `begin_y`: `1..100`, default `1`
- `end_y`: `1..100`, default `100`
- `step_y`: `0.25..99`, default `1`
- `speed_y`: `0.1..10`, default `10`

### 3.3 编码规则

- `angle_x / begin_x / end_x / step_x / begin_y / end_y / step_y`：
  - 输入单位 `deg`
  - 发包前 `qRound(value * 100)`
- `speed_y`：
  - 输入单位 `deg/s`
  - 发包前 `qRound(value * 100)`

## 4. 返回约定

### 4.1 扫描结果

`scan_line` / `scan_frame` / `get_data` 返回原始聚合结果：

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

### 4.2 查询结果

`query` 返回：

```json
{
  "counter": 5,
  "command": 7,
  "result": 10
}
```

- `op = 100` 查询最近一次结果
- `op = 200` 重置最近一次结果

## 5. 错误码约定

| Code | 含义 |
|------|------|
| `0` | 成功 |
| `1` | 串口打开、读写、超时等 transport 错误 |
| `2` | CRC、地址/命令不匹配、协议解析失败、设备失败、分段不完整等 protocol 错误 |
| `400` | 自动参数校验失败 |
| `404` | 未知命令 |

## 6. 实现入口

- `src/drivers/driver_3d_scan_robot/handler.cpp`
- `src/drivers/driver_3d_scan_robot/protocol_codec.h`
- `src/drivers/driver_3d_scan_robot/protocol_codec.cpp`
- `src/drivers/driver_3d_scan_robot/radar_session.cpp`
- `src/tests/helpers/fake_3d_scan_robot_device.h`
- `src/tests/test_3d_scan_robot.cpp`
- `src/tests/test_driver_manager_scanner.cpp`
- `src/smoke_tests/m103_3d_scan_robot.py`

## 7. 测试与验收

### 7.1 单元测试

必须覆盖：

- 主/中断通道错配
- transport 错误码 `1`
- protocol 错误码 `2`
- `get_addr` 回包按位取反校验
- `scan_line` / `scan_frame` 原始聚合返回
- `scan_line` / `scan_frame` 参数编码 `×100`
- `query(op=100)` / `query(op=200)`
- 旧命令名返回 `404`
- 元数据中只保留新命令名和新参数名

### 7.2 Scanner / Export Meta

- `--export-meta` 导出的 `driver.meta.json` 必须只包含新命令名
- `DriverManagerScanner` 测试必须校验新命令存在、旧命令不存在

### 7.3 Smoke

- `S01`：`--export-meta`
- `S02`：`status`
- `S03`：缺失串口时 `test` 返回 transport 错误
- `S04`：未知命令返回 `404`
- `S05`：meta schema 字段完整

## 8. 后续可选项

如果后续需要继续扩展，应单独立项评估：

- 协议 9 调试转发
- 协议 16 重启设备
- 更深的状态字语义拆解
- 扫描数据结构化解析
- 点云成像和坐标系转换
