# 3D Scan Robot Driver

## Overview

`stdio.drv.3d_scan_robot` — 用于控制基于 JD3D/JD3I 二进制协议的 3D 扫描机器人雷达设备。
支持 30+ 命令：设备通信测试、寄存器读写、运动控制、距离测量、扫描（单线/全帧）、中断查询等。

## Protocol

- **帧格式**：`[magic(4)][counter(1)][option(1)][addr(1)][cmd(1)][payload(N)][CRC32(4)]`
- **魔数**：`JD3D`（标准命令）、`JD3I`（中断命令）
- **CRC**：CRC32-STM32（MPEG-2 变体，按字节反序处理每个 32-bit 字）
- **字节序**：Big Endian
- **角度单位**：0.01° (×100)

## Command Model

- OneShot 模式：每条命令携带 port/addr，不复用连接。
- 长任务通过 Query (cmd=6) 轮询等待结果，匹配 counter + cmd。
- 扫描任务完成后通过分段读取 (GetData cmd=12) 聚合完整数据。

## Source Paths

- `src/drivers/driver_3d_scan_robot/`：Driver 实现
  - `protocol_codec.h/.cpp`：帧编解码、CRC、payload 构建
  - `radar_transport.h/.cpp`：串口传输层 + IRadarTransport 接口
  - `radar_session.h/.cpp`：会话管理、长任务轮询、扫描数据聚合
  - `handler.h/.cpp`：命令分发、元数据
  - `main.cpp`：入口
- `src/tests/test_3d_scan_robot.cpp`：GTest 单元测试 (40 cases)
- `src/tests/helpers/fake_3d_scan_robot_device.h/.cpp`：测试用 Fake 设备
- `src/smoke_tests/m103_3d_scan_robot.py`：Smoke 测试 (S01-S05)

## Error Codes

| Code | Meaning |
|------|---------|
| 0    | 成功 |
| 1    | 传输错误（串口打开/读写失败） |
| 2    | 协议/设备错误（CRC 失败、任务失败等） |
| 404  | 未知命令 |

## Test Injection

测试通过 `handler.setTransportFactory()` 注入 `Fake3DScanRobotDevice`，
完全替换真实串口层，实现全覆盖且无硬件依赖的测试。
