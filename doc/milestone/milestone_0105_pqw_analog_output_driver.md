# 里程碑 105：品全微模拟量模块 Driver

> **前置条件**: 已有 `DriverCore`、`DriverMetaBuilder`、`ModbusRtuSerialClient`
> **目标**: 新增一个可被 Host/Server/WebUI 扫描和消费的 `stdio.drv.pqw_analog_output` 串口驱动，并覆盖无硬件条件下可验证的核心测试

## 1. 目标

- 新增 `stdio.drv.pqw_analog_output`
- 只支持串口 RTU，不引入 TCP 分支
- 公开设备语义化命令，覆盖通信配置、单通道输出、连续批量输出、全部清零
- 元数据可被 `meta.describe`、`--export-meta`、DriverManagerScanner、DriverLab 消费
- 新增对应 GTest、Scanner 校验和 Smoke 脚本

## 2. 背景与问题

- 手册给出了稳定的 Modbus RTU 寄存器定义，但现有仓库还没有针对该模块的设备语义化 Driver。
- 直接使用通用 `modbusrtu_serial` 虽然可访问寄存器，但上层需要自行记忆地址和编码规则，不利于 Service、Server 和 WebUI 复用。

**范围**:

- 新增串口驱动目录、构建注册与 runtime 产物
- 实现通信配置读取/写入
- 实现输出读取、单点写入、连续批量写入、清零
- 补齐元数据、知识库、里程碑文档、GTest、Smoke

**非目标**:

- 不支持 TCP
- 不做按具体型号量程上限的强校验
- 不暴露输入寄存器读取
- 不实现真实硬件依赖的端到端 CI 测试

## 3. 技术方案

### 3.1 命令面与寄存器映射

```cpp
// 关键寄存器
constexpr quint16 kRegUnitId = 0x0000;
constexpr quint16 kRegBaudRate = 0x0001;
constexpr quint16 kRegCommWatchdog = 0x0003;
constexpr quint16 kRegParity = 0x0004;
constexpr quint16 kRegStopBits = 0x0005;
constexpr quint16 kRegRestoreDefaults = 0x000D;
constexpr quint16 kRegOutputBase = 0x0064;
constexpr quint16 kRegClearOutputs = 0x0076;
```

- `get_config`：读取 `0x0000..0x0005`
- `set_comm_config`：使用 `current_*` 参数建立当前连接，再逐项用 `0x06` 写目标配置寄存器
- `restore_defaults`：向 `0x000D` 写 `1`
- `read_outputs`：读 `0x0064 + channel - 1`
- `write_output`：单寄存器写
- `write_outputs`：连续多寄存器写
- `clear_outputs`：向 `0x0076` 写 `1`

### 3.2 编码与校验规则

```cpp
static bool engineeringValueToRaw(double value, quint16& rawValue, QString* errorMessage);
static bool commWatchdogMsToRaw(int watchdogMs, quint16& rawValue, QString* errorMessage);
static int commWatchdogRawToMs(quint16 rawValue);
```

- 输出值编码：`raw = round(value * 1000)`
- 输出值必须编码到 `0..65535`
- `comm_watchdog_ms = 0` 表示关闭
- `comm_watchdog_ms > 0` 时必须是 `10ms` 的整数倍，编码为 `raw = ms / 10 + 1`
- `channel` / `start_channel` 必须在 `1..18`
- 连续批量写不得跨出 `18` 通道边界
- `set_comm_config` 中：
  - `current_unit_id` / `current_baud_rate` / `current_parity` / `current_stop_bits` 只用于建立当前连接
  - `unit_id` / `baud_rate` / `parity` / `stop_bits` 只表示要写入设备的新配置

### 3.3 元数据与错误契约

| 命令 | 关键参数 | 关键返回 |
|------|----------|----------|
| `get_config` | `port_name` | `unit_id` `baud_rate` `parity` `stop_bits` `comm_watchdog_ms` |
| `set_comm_config` | `current_unit_id` `current_baud_rate` `current_parity` `current_stop_bits` + 可选目标 `unit_id` `baud_rate` `parity` `stop_bits` `comm_watchdog_ms` | `writes[]` `reboot_required` |
| `read_outputs` | `start_channel` `count` | `outputs[]` |
| `write_output` | `channel` `value` | `channel` `value` `raw_value` |
| `write_outputs` | `start_channel` `values[]` | `values[]` `raw_values[]` |

- 错误码：
  - `1` transport
  - `2` protocol
  - `3` 业务参数非法
  - `404` 未知命令
- 元数据统一通过 `DriverMetaBuilder` 构建，并调用 `ensureCommandExamples()` 自动补示例

## 4. 实现步骤

### 4.1 Driver 与构建

- 新增 `src/drivers/driver_pqw_analog_output/main.cpp`
- 新增 `src/drivers/driver_pqw_analog_output/handler.h`
- 新增 `src/drivers/driver_pqw_analog_output/handler.cpp`
- 新增 `src/drivers/driver_pqw_analog_output/CMakeLists.txt`

```cpp
PqwAnalogOutputHandler handler;
stdiolink::DriverCore core;
core.setMetaHandler(&handler);
```

- 复用 `driver_modbusrtu_serial/modbus_rtu_serial_client.cpp`
- 输出名固定为 `stdio.drv.pqw_analog_output`

### 4.2 Handler 行为

- 在 handler 内封装串口参数解析与共享连接缓存
- 为 `get_config` 实现配置寄存器解码
- 为 `set_comm_config` 实现逐项写单寄存器
- 为 `write_outputs` 实现连续通道批量写

```cpp
if (cmd == "write_outputs") {
    // 校验 start_channel / values[]
    // 将 values[] 逐项编码成 rawValues
    // writeMultipleRegisters(...)
}
```

- 所有业务校验失败统一返回 `resp.error(3, ...)`

### 4.3 测试与扫描集成

- 新增 `src/tests/test_pqw_analog_output.cpp`
- 更新 `src/tests/test_driver_manager_scanner.cpp`
- 新增 `src/smoke_tests/m105_pqw_analog_output.py`
- 更新 `src/smoke_tests/run_smoke.py`
- 更新 `src/smoke_tests/CMakeLists.txt`
- 更新 `src/smoke_tests/m95_driver_examples.py`，把新驱动纳入示例覆盖

## 5. 文件变更清单

### 5.1 新增文件

- `src/drivers/driver_pqw_analog_output/main.cpp`
- `src/drivers/driver_pqw_analog_output/handler.h`
- `src/drivers/driver_pqw_analog_output/handler.cpp`
- `src/drivers/driver_pqw_analog_output/CMakeLists.txt`
- `src/tests/test_pqw_analog_output.cpp`
- `src/smoke_tests/m105_pqw_analog_output.py`
- `doc/knowledge/02-driver/driver-pqw-analog-output.md`
- `doc/milestone/milestone_0105_pqw_analog_output_driver.md`

### 5.2 修改文件

- `src/drivers/CMakeLists.txt`
- `src/tests/CMakeLists.txt`
- `src/tests/test_driver_manager_scanner.cpp`
- `src/smoke_tests/run_smoke.py`
- `src/smoke_tests/CMakeLists.txt`
- `src/smoke_tests/m95_driver_examples.py`
- `doc/knowledge/02-driver/README.md`

## 6. 测试与验收

### 6.1 单元测试

- 测试对象：`PqwAnalogOutputHandler`
- 用例：
  - `status` 返回 `ready`
  - 缺失 `port_name`
  - `unit_id=256`
  - `channel=19`
  - `start_channel + count` 越界
  - `comm_watchdog_ms=125` 非法
  - `engineeringValueToRaw(12.345) -> 12345`
  - `set_comm_config` 连接解析优先使用 `current_*`
  - `set_comm_config` 目标寄存器编码使用目标 `unit_id` / `baud_rate` / `parity` / `stop_bits`
  - 元数据含目标命令与默认值

### 6.2 冒烟测试脚本

- 脚本：`src/smoke_tests/m105_pqw_analog_output.py`
- 入口：`python src/smoke_tests/run_smoke.py --plan m105_pqw_analog_output`
- 用例：
  - `S01` `meta.describe`
  - `S02` `--export-meta`
  - `S03` `status`
  - `S04` 不存在串口返回 `code=1`
  - `S05` 未知命令返回 `code=404`

### 6.3 验收标准

- [ ] Driver 可编译并输出 `stdio.drv.pqw_analog_output`
- [ ] `meta.describe` 与 `--export-meta` 暴露 8 个公开命令
- [ ] GTest 覆盖参数校验、编码规则和元数据
- [ ] Scanner 可导出 `driver.meta.json`
- [ ] Smoke 统一入口可执行

## 7. 风险与控制

- 风险: 不同型号量程上限不同，v1 无法做强约束
  - 控制: 仅做 `0..65535` 原始寄存器范围校验，并在文档中声明单位语义由型号决定
- 风险: 无真实硬件时无法验证成功写入链路
  - 控制: 用 GTest 锁定编码与参数校验，用 Smoke 锁定可执行与错误路径
- 风险: 新驱动未被示例或扫描链路发现
  - 控制: 同步更新 `m95_driver_examples`、`DriverManagerScanner` 测试和 `run_smoke.py`

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] 冒烟测试脚本已新增并接入统一入口与 CTest
- [ ] 文档同步完成
- [ ] 向后兼容策略确认：新驱动为增量能力，不影响现有 Driver
