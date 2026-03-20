# 3D Temp Scanner Driver

## Overview

`stdio.drv.3d_temp_scanner` 是按 `v3dtemserialpproto` 实际支持范围收口后的 `OneShot` Driver。

- 只支持 `serial`
- 只覆盖 `60000` 启动/状态轮询与 `0..767` 温度帧读取
- 不实现 `60001`、`60002`、`60003`、`60004` 相关能力
- 对外命令面只保留 `status` 和 `capture`

## Runtime Model

- `profile = oneshot`
- 串口参数：
  - `port_name`
  - `baud_rate = 115200`
  - `parity = none`
  - `stop_bits = 1`
  - `device_addr = 1`
  - `timeout_ms = 3000`
  - `scan_timeout_ms = 25000`
  - `poll_interval_ms = 1000`

## Public Commands

- `status`
- `capture`

## Protocol Mapping

- `60000`
  - 写 `100` 启动测温
  - 读 `10` 表示成功
  - 读 `20` 表示失败
- `0..767`
  - 固定 32x24 温度图像原始寄存器
  - 每个像素 `UINT16`，大端
  - 温度换算：`raw * 0.01 - 273.15`

## Output Formats

- `png`
  - 默认格式
  - 伪彩热力图
  - 输出尺寸固定 `320x240`
- `json`
  - 保存宽高、温度范围和 768 个温度值
- `csv`
  - 保存 24 行 x 32 列温度矩阵
- `raw`
  - 保存 1536 字节原始 `UINT16` 数据，大端

格式优先级：

- 显式 `format`
- `output` 文件后缀
- 默认 `png`

## Special Note

- 该设备读取 768 个寄存器时响应 `byte_count = 0`，这是 `v3dtemserialpproto::ReadTempData::parse()` 明确支持的私有扩展
- 当前实现固定按 768 点整帧读取，不读取 `60001` 数据长度寄存器
- 当前实现是串口协议子集封装，不是通用 Modbus/TCP 驱动

## Error Codes

| Code | Meaning |
|------|---------|
| `0` | 成功 |
| `1` | 串口打开失败、读写失败、输出文件写入失败 |
| `2` | 协议校验失败、设备返回失败、测温超时 |
| `3` | 参数非法 |
| `404` | 未知命令 |

## Key Source Paths

- `src/drivers/driver_3d_temp_scanner/handler.cpp`
- `src/drivers/driver_3d_temp_scanner/protocol_codec.cpp`
- `src/drivers/driver_3d_temp_scanner/thermal_transport.cpp`
- `src/drivers/driver_3d_temp_scanner/thermal_session.cpp`
- `src/tests/test_3d_temp_scanner.cpp`
- `src/tests/test_driver_manager_scanner.cpp`
- `src/smoke_tests/m106_3d_temp_scanner.py`

## Verification

- `meta.describe` 与 `--export-meta` 必须只暴露 `status` 和 `capture`
- `capture` 必须覆盖 `output` + `format` 保存链路
- `png` 输出必须可被 `QImage` 打开且尺寸为 `320x240`
- `DriverManagerScanner` 必须能导出 `driver.meta.json`
