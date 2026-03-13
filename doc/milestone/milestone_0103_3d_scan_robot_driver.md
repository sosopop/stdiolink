# 里程碑 103：3D 扫描机器人 Driver（协议基础接入版）

> **前置条件**: 现有 `DriverCore` / `IMetaCommandHandler` / runtime 组装链路可用；`src/smoke_tests/` 统一入口与 CTest 冒烟接入机制已稳定；知识库中 `02-driver` 与 `08-workflows/add-driver.md` 约束已遵守
> **目标**: 新增 `stdio.drv.3d_scan_robot` C++ Driver，在不扩展底层 runtime 能力的前提下，尽量兼容 MatLab 测试程序中的全部设备指令，并以当前框架真实契约支持 Host / Server / WebUI 扫描、调试和自动化验证；不包含点云成像和坐标系转换相关代码

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| `src/drivers/driver_3d_scan_robot` | 新增驱动、协议编解码、串口传输、命令元数据 |
| `src/tests` | 协议、传输、长任务轮询、分段聚合、handler、扫描器兼容性 GTest |
| `src/tests/helpers` | 单套 fake 3D 扫描机器人协议实现，供 GTest 与 smoke 共用 |
| `src/smoke_tests` | `m103_3d_scan_robot.py` 冒烟脚本、`run_smoke.py` 注册、CTest 接入 |
| `doc/knowledge` | Driver 接入说明、测试入口、`--export-meta` 扫描约定同步 |

- 交付新的生产 Driver 目录 `src/drivers/driver_3d_scan_robot/`，输出名为 `stdio.drv.3d_scan_robot`。
- 对外暴露的 JSONL 命令尽量覆盖 MatLab 测试程序中的全部设备指令，但明确排除 `quit`、`show`、`show_trans` 以及点云成像/坐标系转换相关代码。
- Driver 同时支持 `meta.describe` 与 `--export-meta=<path>` 两条链路。
- Driver 采用 oneshot 模式：每条命令都必须携带串口连接信息，命令之间不复用连接状态。
- 参数校验遵守当前框架真实契约：缺参和范围错误由自动参数校验返回 `400 / ValidationFailed`。
- 业务错误码分层固定为：IO/传输失败 `1`，协议错误/设备失败/分段不完整 `2`，未知命令 `404`。
- 自动化测试只连接 fake 串口设备或虚拟串口对，不对真实设备执行永久性配置写入。

## 2. 背景与问题

- 当前仓库已具备 `DriverCore`、元数据、自描述导出、Server 扫描、WebUI DriverLab 与 smoke 基础设施，但尚无“3D 扫描机器人”协议驱动。
- 用户提供的协议文档、C++ 上位机、C++ 简单模拟器与 MatLab 测试程序表明，该设备协议包含“启动动作 -> 周期查询 -> 分段取数”的复合链路。
- 上一版文档把范围拉得过大，混入了高层扫描结果语义、双套 fake 和模糊的公开 `query_task/get_data` 能力，和当前框架真实契约不完全一致。
- 本修订版里程碑只解决“稳定接入当前 stdiolink 框架”这一类核心问题。

**范围**:
- 新增 `stdio.drv.3d_scan_robot` 驱动目录、CMake、main、handler、协议与传输实现。
- 首版只支持 `serial` 串口参数，围绕真实设备和虚拟串口联调。
- 采用 oneshot 命令模型；每条命令都显式传入 `port`、`addr`，可选串口参数缺省值对齐 MatLab。
- 支持寄存器类命令、控制类命令、扫描类命令、中断式命令，以及 MatLab 层面的兼容包装命令。
- `get_line` / `get_frame` 在单命令内完成长任务轮询与分段拉取，返回原始聚合结果。
- 新增 GTest、smoke、Server 扫描兼容性测试与知识库同步点。

**非目标**:
- 不修改仓库外的 C++ 上位机、模拟器或 MatLab 程序。
- 不在本里程碑实现 WebUI 专用页面或自定义 Server API。
- 不实现 `quit` 这类 MatLab 交互式 shell 控制命令。
- 不实现 `show` / `show_trans` 这类 MatLab 本地绘图、点云成像和坐标系转换能力。
- 不公开 `query_task` 作为外部命令；由 `wait` / `res` 与扫描类命令在内部封装查询流程。
- 不在本里程碑实现协议文档中的 `forward debug`、`reboot` 等未出现在 MatLab 主测试流程中的扩展命令。
- 不在本里程碑实现扫描数据的点云/网格等高层业务语义解码。

## 3. 技术要点

### 3.1 Driver 边界与公开命令

公开命令以 MatLab 主命令为参考，但输入统一为结构化 JSON，并尽量覆盖 MatLab 设备指令：

| Driver 命令 | 类型 | 说明 |
|-------------|------|------|
| `status` | 本地虚拟命令 | 返回驱动存活状态 |
| `test_com` / `test` | 控制命令 | 测试主协议通信 |
| `get_addr` / `set_addr` | 寄存器读写 | 读取/设置设备地址 |
| `get_mode` / `set_mode` | 寄存器读写 | 读取/设置工作模式，支持 `10/20/30/40` 四种模式 |
| `get_temp` | 寄存器读 | 返回 MCU / 板卡温度的原始值与摄氏度语义值 |
| `get_state` | 寄存器读 | 返回状态字与已确认位拆解 |
| `get_fw_ver` | 寄存器读 | 返回固件版本 |
| `get_direction` | 寄存器读 | 返回 X/Y 当前角度 |
| `get_sw0` / `get_sw1` | 寄存器读 | 返回接近开关语义状态与原始状态码 |
| `get_calib0` / `get_calib1` | 寄存器读 | 返回校准语义状态与原始状态码 |
| `calib` / `calib0` / `calib1` | 长任务 | 全量/单轴校准 |
| `move_dist` | 长任务 | 移动到指定角度并返回该点测距结果 |
| `move` | 长任务 | 绝对角度移动 |
| `get_dist` | 测量命令 | 单点测距 |
| `get_reg` / `set_reg` | 原始寄存器读写 | 调试与兼容迁移用途 |
| `get_line` / `get_frame` | 扫描命令 | 单命令完成扫描、轮询和分段聚合 |
| `get_data` | 数据命令 | 按显式 `total_bytes` 拉取大量数据分段并聚合返回 |
| `res` | 查询命令 | 返回最近一次长任务的执行结果摘要 |
| `wait` | 查询命令 | 轮询等待最近一次长任务完成并返回结果摘要 |
| `insert_test` | 中断式命令 | 测试中断式通信 |
| `insert_state` | 中断式命令 | 获取帧扫描状态 |
| `insert_stop` | 中断式命令 | 停止当前帧扫描 |
| `radar_get_response_time` / `rgrt` | 诊断命令 | 获取雷达响应时间统计 |

公开别名与兼容包装：

```text
test        -> test_com
dist        -> get_dist
state       -> get_state
get_ver     -> get_fw_ver
get_dir     -> get_direction
gr          -> get_reg
sr          -> set_reg
rgrt        -> radar_get_response_time
```

串口输入契约：
- every request 必须显式传 `port` 与 `addr`
- 可选串口参数默认值按 MatLab `main.m` 对齐：
  - `baud_rate = 115200`
  - `timeout_ms = 5000`
- 轮询与等待默认值按 MatLab 相关脚本对齐：
  - `query_interval_ms = 1000`
  - `inter_command_delay_ms = 250`
  - `task_timeout_ms` 缺省时按命令类型决定：
    - `move` / `move_dist` / `calib*` / `wait` / `res` 默认 `180000`
    - `get_line` 默认 `100000`
    - `get_frame` 默认 `1000000`
- 其余命令执行参数仍按各自命令 schema 单独定义
- Driver 不保存上一次命令的串口配置；下一条命令必须重新携带连接信息

内部实现细节：

```cpp
bool waitTaskCompleted(quint8 expectedCommand,
                       QueryTaskResult* result,
                       QString* errorMessage);

bool collectScanData(ScanAggregateResult* result,
                     QString* errorMessage);
```

示例输入：

```json
{"cmd":"test_com","data":{"port":"COM3","addr":1,"value":1000}}
{"cmd":"set_mode","data":{"port":"COM3","addr":1,"mode":"imaging"}}
{"cmd":"set_mode","data":{"port":"COM3","addr":1,"mode":"sleep"}}
{"cmd":"move","data":{"port":"COM3","addr":1,"x_deg":90.5,"y_deg":45.25}}
{"cmd":"move_dist","data":{"port":"COM3","addr":1,"x_deg":90.0,"y_deg":45.0}}
{"cmd":"get_line","data":{"port":"COM3","addr":1,"angle_x_deg":97.0,"begin_y_mm":1.0,"end_y_mm":100.0,"step_y_mm":1.0,"sample_count":10}}
{"cmd":"insert_state","data":{"port":"COM3","addr":1}}
```

### 3.2 协议模型、传输抽象与长任务状态机

```cpp
struct RadarFrame {
    quint8 counter = 0;
    quint8 option = 0;
    quint8 addr = 0;
    quint8 command = 0;
    QByteArray payload;
    quint32 crc32 = 0;
};

struct RadarTransportParams {
    QString port;
    int baudRate = 115200;
    quint8 addr = 1;
    int timeoutMs = 5000;
    int taskTimeoutMs = -1;   // omitted -> derive from command defaults
    int queryIntervalMs = 1000;
    int interCommandDelayMs = 250;
};

struct ScanAggregateResult {
    quint8 taskCounter = 0;
    quint8 taskCommand = 0;
    quint32 resultCode = 0;
    int segmentCount = 0;
    int byteCount = 0;
    QByteArray data;
};
```

```cpp
class IRadarTransport {
public:
    virtual ~IRadarTransport() = default;
    virtual bool open(const RadarTransportParams& params, QString* errorMessage) = 0;
    virtual bool writeFrame(const QByteArray& frame, int timeoutMs, QString* errorMessage) = 0;
    virtual bool readSome(QByteArray& chunk, int timeoutMs, QString* errorMessage) = 0;
    virtual void close() = 0;
};
```

长任务状态机：

```text
参数校验（DriverCore + meta 自动完成）
  ↓
打开 transport
  ↓
发送启动命令（calib/move/get_line/get_frame）
  ↓
等待首个协议响应
  ├─ 立即成功 -> done
  ├─ 明确失败 -> error(code=2)
  └─ 进入 query 循环
         ↓
    校验 lastCounter / lastCommand 是否匹配当前任务
         ↓
    resultCode == 0 ?
      ├─ 否 -> error(code=2)
      ├─ 超时 -> error(code=2)
      └─ 是
          ├─ 无扫描数据 -> done
          └─ 拉取分段数据
                ├─ 完整 -> done
                └─ 缺段/乱序 -> error(code=2)
```

协议与时序约束：
- 帧头固定 `JD3D`，整型按 Big Endian。
- CRC 使用 CRC32-STM32。
- 连续两条真实设备指令之间至少间隔 `50ms`。
- `get_line` / `get_frame` 不要求调用方自行编排 query 与分段拉取。
- 中断式命令使用 `JD3I` 头，与常规 `JD3D` 计数器独立递增。
- `insert_state`、`insert_test`、`insert_stop` 直接走中断式通道，不与主通道长任务轮询共用计数器。

### 3.3 当前框架真实契约：参数校验、元数据与扫描导出

| 场景 | 当前框架/Driver 行为 | 错误码 |
|------|----------------------|--------|
| 缺参、类型错误、范围越界 | 自动参数校验失败，返回 `ValidationFailed` | `400` |
| 串口/网络打开失败，连接断开，读写超时 | handler 返回结构化传输错误 | `1` |
| 帧头错误、CRC 错误、地址/命令不匹配、分段不完整、设备结果码失败 | handler 返回结构化协议错误 | `2` |
| 未知命令 | `Unknown command` | `404` |

`meta.describe` 与 `--export-meta=<path>` 分工如下：

| 能力 | 入口 | 用途 |
|------|------|------|
| `meta.describe` | `{"cmd":"meta.describe"}` 或 `--cmd=meta.describe` | DriverLab / 手工调试 / 文档导出 |
| `--export-meta=<path>` | 驱动进程 CLI | Server 扫描导出 `driver.meta.json` |

元数据构建策略：

```cpp
m_meta = DriverMetaBuilder()
    .schemaVersion("1.0")
    .info("stdio.drv.3d_scan_robot", QString::fromUtf8("3D 扫描机器人"), "1.0.0",
          QString::fromUtf8("3D 扫描机器人协议基础驱动"))
    .vendor("stdiolink")
    .command(buildMoveCommand())
    .command(buildGetLineCommand())
    .build();
stdiolink::meta::ensureCommandExamples(m_meta);
```

### 3.4 返回契约与兼容边界

扫描类命令与 `get_data` 只返回原始聚合结果：

```json
{
  "status": "done",
  "code": 0,
  "data": {
    "task_counter": 17,
    "task_command": "get_frame",
    "result_code": 0,
    "segment_count": 12,
    "byte_count": 4096,
    "data_base64": "<base64>"
  }
}
```

控制类命令继续返回语义化字段 + 原始码值；MatLab 层兼容命令返回与其语义匹配的包装结果：

```json
{
  "status": "done",
  "code": 0,
  "data": {
    "mode": "imaging",
    "mode_code": 20
  }
}
```

向后兼容与边界：
- 该 Driver 为新增能力，不影响现有 Driver 行为。
- 命令名优先贴近 MatLab 现场习惯，但输入格式统一为 JSON。
- `get_state` 仅拆解已确认状态位；无法确认的位只保留在 `raw_state` 中。
- `wait` / `res` 为 MatLab 兼容包装命令，本质上复用同一套最近任务结果查询能力。
- `get_data` 继续保持 oneshot 无状态接口，调用方必须显式提供 `total_bytes`；同时 `get_line/get_frame` 仍保留单命令返回结果的便捷模式。
- `get_temp`、`get_sw0/get_sw1`、`get_calib0/get_calib1` 返回语义字段，并保留原始状态码或原始值。
- 后续若补高层点云语义，应在新里程碑中扩展，不修改本里程碑字段名。

## 4. 实现步骤

### 4.1 `src/drivers/driver_3d_scan_robot/`

- 新增 `main.cpp`：

```cpp
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    ThreeDScanRobotHandler handler;
    stdiolink::DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
```

- 新增 `handler.h/.cpp`：

```cpp
class ThreeDScanRobotHandler final : public stdiolink::IMetaCommandHandler {
public:
    ThreeDScanRobotHandler();
    void handle(const QString& cmd, const QJsonValue& data, stdiolink::IResponder& responder) override;
    const stdiolink::meta::DriverMeta& driverMeta() const override;
};
```

- 新增 `protocol_codec.h/.cpp`、`radar_transport.h/.cpp`、`radar_session.h/.cpp`，分别负责主协议与中断式协议编解码、串口传输、长任务轮询与扫描聚合。

改动理由：
- 让协议编解码、传输与长任务状态机可独立测试。

验收方式：
- `stdio.drv.3d_scan_robot --cmd=meta.describe` 退出码为 `0`。
- `test_com`、`move_dist`、`get_data`、`insert_state`、`radar_get_response_time` 等 MatLab 兼容命令出现在元数据里。

### 4.2 `src/drivers/CMakeLists.txt`

- 修改 `src/drivers/CMakeLists.txt`：

```cmake
add_subdirectory(driver_3d_scan_robot)
```

- 新增 `src/drivers/driver_3d_scan_robot/CMakeLists.txt`，依赖 `Qt Core + SerialPort`，输出名为 `stdio.drv.3d_scan_robot`，并注册到 `STDIOLINK_EXECUTABLE_TARGETS`。

改动理由：
- 与 `driver_plc_crane`、`driver_modbusrtu_serial` 的构建方式保持一致。

验收方式：
- `cmake --build build --target driver_3d_scan_robot` 可编译通过。

### 4.3 `src/tests/test_3d_scan_robot.cpp`

- 新增 `src/tests/test_3d_scan_robot.cpp`，覆盖：
  - 编解码对称性与非法帧
  - 串口参数分支
  - 长任务轮询与旧任务污染
  - 分段完整性
  - 参数校验 `400`
  - MatLab 兼容包装命令
  - 中断式命令
  - 资源释放与重复关闭安全性

```cpp
TEST(ThreeDScanRobotProtocolTest, T01_DecodeValidFrame);
TEST(ThreeDScanRobotSessionTest, T11_QuerySuccessMatchesCounterAndCommand);
TEST(ThreeDScanRobotHandlerTest, T18_MetaValidationReturns400);
TEST(ThreeDScanRobotHandlerTest, T22_GetLineReturnsAggregatedRawResult);
TEST(ThreeDScanRobotHandlerTest, T24_MoveDistReturnsDistanceAtTargetAngles);
TEST(ThreeDScanRobotHandlerTest, T27_InsertStateReturnsFrameProgress);
```

改动理由：
- 这是协议驱动的主验证入口，必须覆盖边界与异常路径。

验收方式：
- `stdiolink_tests --gtest_filter=ThreeDScanRobot*` 全部通过。

### 4.4 `src/tests/helpers/`

- 新增 `src/tests/helpers/fake_3d_scan_robot_device.h/.cpp`：

```cpp
class Fake3DScanRobotDevice {
public:
    bool start();
    void stop();
    quint16 port() const;
    void enqueueReadRegisterSuccess(quint16 regId, quint32 value);
    void enqueueTaskSuccess(quint8 cmd, quint8 counter, quint32 resultCode = 0);
    void enqueueTaskFailure(quint8 cmd, quint8 counter, quint32 resultCode);
    void enqueueScanSegments(const QList<QByteArray>& segments);
    void enqueueInterruptProgress(quint16 currentLine, quint16 totalLines);
    void enqueueInterruptStopReply(quint32 value);
    void enqueueInterruptTestReply(quint32 value);
    void enqueueResponseTimeReply(quint16 tMin, quint16 tMax, quint16 tAve, quint16 goodCounter);
};
```

- 新增 `src/tests/test_3d_scan_robot_fake_main.cpp`，把同一套 fake 封装成可执行程序供 smoke 启动。

改动理由：
- 只维护一套协议 fake，避免 GTest 与 smoke 各自实现一份二进制协议逻辑。

验收方式：
- GTest 直接链接 fake helper。
- smoke 通过启动 fake helper 可执行程序完成 S02-S04 场景。

### 4.5 `src/smoke_tests/` 与扫描器兼容性测试

- 新增 `src/smoke_tests/m103_3d_scan_robot.py`
- 修改 `src/smoke_tests/run_smoke.py`
- 修改 `src/smoke_tests/CMakeLists.txt`
- 修改 `src/tests/test_driver_manager_scanner.cpp`，新增 `--export-meta` 扫描兼容性用例

关键逻辑：

```python
S01: driver --export-meta=<tmp_meta_path>
S02: fake helper + driver get_mode/set_mode/get_reg/test_com
S03: fake helper + driver get_line 或 get_frame 或 get_data -> segment_count/byte_count/data_base64
S04: fake helper + driver insert_state/insert_stop/rgrt
S05: fake helper 返回设备失败 -> status:error, code=2
```

改动理由：
- `meta.describe` 只能证明 DriverLab/手工调试能力。
- Server 扫描真正依赖的是 `--export-meta=<path>` 导出链路。

验收方式：
- `python src/smoke_tests/run_smoke.py --plan m103_3d_scan_robot` 可独立运行。
- `test_driver_manager_scanner.cpp` 新增用例通过。

## 5. 文件变更清单

### 5.1 新增文件
- `src/drivers/driver_3d_scan_robot/CMakeLists.txt` - 驱动构建定义
- `src/drivers/driver_3d_scan_robot/main.cpp` - 驱动入口
- `src/drivers/driver_3d_scan_robot/handler.h` - handler 声明
- `src/drivers/driver_3d_scan_robot/handler.cpp` - 命令分派与元数据
- `src/drivers/driver_3d_scan_robot/protocol_codec.h` - 报文类型与编解码声明
- `src/drivers/driver_3d_scan_robot/protocol_codec.cpp` - 报文编解码实现
- `src/drivers/driver_3d_scan_robot/radar_transport.h` - 传输抽象声明
- `src/drivers/driver_3d_scan_robot/radar_transport.cpp` - 串口传输实现
- `src/drivers/driver_3d_scan_robot/radar_session.h` - 长任务与扫描聚合声明
- `src/drivers/driver_3d_scan_robot/radar_session.cpp` - 长任务与扫描聚合实现
- `src/tests/test_3d_scan_robot.cpp` - 单元测试主文件
- `src/tests/helpers/fake_3d_scan_robot_device.h` - fake 设备声明
- `src/tests/helpers/fake_3d_scan_robot_device.cpp` - fake 设备实现
- `src/tests/test_3d_scan_robot_fake_main.cpp` - fake 可执行程序入口
- `src/smoke_tests/m103_3d_scan_robot.py` - 冒烟脚本

### 5.2 修改文件
- `src/drivers/CMakeLists.txt` - 注册新驱动
- `src/tests/CMakeLists.txt` - 接入 fake helper executable 与测试源文件
- `src/tests/test_driver_manager_scanner.cpp` - 增加 `--export-meta` 扫描兼容性测试
- `src/smoke_tests/run_smoke.py` - 注册 `m103_3d_scan_robot`
- `src/smoke_tests/CMakeLists.txt` - 注册 smoke CTest
- `doc/knowledge/02-driver/README.md` - 增加新驱动接入说明
- `doc/knowledge/08-workflows/add-driver.md` - 增加协议型 Driver 接入补充说明
- `doc/knowledge/07-testing-build/test-matrix.md` - 增加 M103 测试入口

### 5.3 测试文件
- `src/tests/test_3d_scan_robot.cpp` - 单元测试主文件
- `src/tests/test_driver_manager_scanner.cpp` - 扫描器兼容性测试
- `src/tests/helpers/fake_3d_scan_robot_device.h` - fake 设备声明
- `src/tests/helpers/fake_3d_scan_robot_device.cpp` - fake 设备实现
- `src/tests/test_3d_scan_robot_fake_main.cpp` - fake smoke 入口
- `src/smoke_tests/m103_3d_scan_robot.py` - runtime 冒烟

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: `protocol_codec`、`RadarTransport`、`RadarSession`、`ThreeDScanRobotHandler`、`DriverManagerScanner`
- 用例分层: 正常路径、边界值、异常输入、错误传播、兼容回归、资源释放
- 断言要点: 返回状态、错误码、错误消息、返回字段、分段聚合字节数、导出元数据文件、连接关闭行为
- 桩替身策略: 使用单套 `Fake3DScanRobotDevice`；通过虚拟串口对接入自动化测试，真实串口路径不作为自动化测试前提
- 测试文件:
  - `src/tests/test_3d_scan_robot.cpp`
  - `src/tests/test_driver_manager_scanner.cpp`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `tryDecodeFrame()` | 常规合法帧 | T01 |
| `tryDecodeFrame()` | 半包 | T02 |
| `tryDecodeFrame()` | magic 错误 | T03 |
| `tryDecodeFrame()` | CRC 错误 | T04 |
| `tryDecodeFrame()` | 地址不匹配 | T05 |
| `tryDecodeFrame()` | 命令不匹配 | T06 |
| `tryDecodeInterruptFrame()` | 中断式合法帧 | T07 |
| 参数校验 | 缺 `port` -> `400` | T08 |
| 参数默认值 | 缺省串口参数时使用 MatLab 对齐默认值 | T09 |
| `RadarSerialTransport::open()` | 串口打开失败 -> `1` | T10 |
| `RadarSerialTransport::readSome()` | 串口读超时 -> `1` | T11 |
| `waitTaskCompleted()` | query 成功命中当前任务 | T12 |
| `waitTaskCompleted()` | query 超时 | T13 |
| `waitTaskCompleted()` | 先返回旧 counter/cmd | T14 |
| `waitTaskCompleted()` | 设备失败 result code | T15 |
| `collectScanData()` | 分段完整 | T16 |
| `collectScanData()` | 分段缺失或乱序 | T17 |
| `RadarSession` 关闭 | 执行后连接关闭与资源释放 | T18 |
| handler | 参数错误 -> `400 / ValidationFailed` | T19 |
| handler | 传输错误 -> `1` | T20 |
| handler | 协议/设备错误 -> `2` | T21 |
| handler | 未知命令 -> `404` | T22 |
| handler | `get_line` 返回原始聚合结果 | T23 |
| handler | `get_frame` 返回原始聚合结果 | T24 |
| handler | `move_dist` 返回目标角度距离结果 | T25 |
| handler | `get_data` 独立拉取最近一次扫描数据 | T26 |
| handler | `insert_state` 返回帧扫描进度 | T27 |
| handler | `insert_stop` 返回停止确认 | T28 |
| handler | `test_com` / `test` / `insert_test` 通信测试成功 | T29 |
| handler | `radar_get_response_time` / `rgrt` 返回响应时间统计 | T30 |
| handler | `wait` / `res` 返回最近一次任务结果摘要 | T31 |
| `DriverManagerScanner` | `--export-meta=<path>` 成功导出并可扫描加载 | T32 |
| `RadarSerialTransport::open()` | 串口真实成功路径 | 未覆盖 — 依赖真实硬件；补测计划 H01 |

覆盖要求（硬性）:
- T01-T32 全部必须执行。
- 串口真实成功路径不计入自动化通过项，联机验收单独记录为 H01。
- 实现落地时若出现不可达路径，需补证明说明。

#### 执行约束

- 被验收标准直接引用的 T01-T32 与 S01-S05 必须实际执行，不得禁用。
- 被验收标准直接引用的测试不得 `skip`。
- 每条失败路径都由本地 fake 或可控环境触发。
- H01 不计入自动化“通过”，仅作为补充联机验收记录。

#### 用例详情

**T01 — 协议合法帧可正确解码**
- 前置条件: 构造合法 `JD3D` 帧
- 输入: `tryDecodeFrame(buffer, 1, 1, &frame, &error)`
- 预期: 返回 `DecodeStatus::Ok`
- 断言: `frame.command == 1`，`error` 为空

**T08 — 缺 `port` 触发框架参数校验**
- 前置条件: handler 已初始化
- 输入: `get_mode` 缺少 `port`
- 预期: 返回 `ValidationFailed`
- 断言: `code == 400`

**T09 — 缺省串口参数时使用 MatLab 对齐默认值**
- 前置条件: handler 已初始化
- 输入: `get_mode` 只传 `port` 与 `addr`
- 预期: 使用默认波特率、超时和轮询节奏继续执行
- 断言: transport 收到 `baudRate == 115200`、`timeoutMs == 5000`、`queryIntervalMs == 1000`

**T10 — 串口打开失败返回 code=1**
- 前置条件: fake 或串口桩返回打开失败
- 输入: handler 执行任一需要打开串口的命令
- 预期: 失败
- 断言: `code == 1`

**T11 — 串口读超时返回 code=1**
- 前置条件: fake 或串口桩在超时时间内不返回任何字节
- 输入: handler 执行任一等待响应的命令
- 预期: 失败
- 断言: `code == 1`

**T12 — query 成功且命中当前任务**
- 前置条件: fake 对启动命令返回“需要查询”，随后返回匹配的 `counter/cmd`
- 输入: `RadarSession::moveTo(...)`
- 预期: 成功
- 断言: `lastCommand` 命中当前命令

**T17 — 分段缺失或乱序时聚合失败**
- 前置条件: fake 故意跳过中间分段或打乱顺序
- 输入: `RadarSession::scanFrame(...)`
- 预期: 失败
- 断言: handler 返回 `code == 2`

**T18 — 执行后连接正确关闭并释放资源**
- 前置条件: fake 已启动
- 输入: 成功执行一次命令后销毁 `RadarSession`
- 预期: transport 关闭
- 断言: fake 观察到连接断开；重复关闭不崩溃

**T19 — 参数错误返回 400 / ValidationFailed**
- 前置条件: handler 已初始化
- 输入: `move` 缺关键参数或参数越界
- 预期: 框架参数校验失败
- 断言: `code == 400`，`name == ValidationFailed`

**T23 — get_line 返回原始聚合结果**
- 前置条件: fake 脚本化返回 `scanLine -> query -> segments`
- 输入: handler 执行 `get_line`
- 预期: 成功
- 断言: 返回体含 `task_counter`、`segment_count`、`byte_count`、`data_base64`

**T24 — get_frame 返回原始聚合结果**
- 前置条件: fake 脚本化返回 `scanFrame -> query -> segments`
- 输入: handler 执行 `get_frame`
- 预期: 成功
- 断言: 返回体含 `task_counter`、`segment_count`、`byte_count`、`data_base64`

**T25 — move_dist 返回目标角度距离结果**
- 前置条件: fake 支持命令 5 或等价流程
- 输入: handler 执行 `move_dist`
- 预期: 成功
- 断言: 返回体含目标角度与距离值

**T26 — get_data 独立拉取最近一次扫描数据**
- 前置条件: fake 已就绪，调用方明确提供 `total_bytes`
- 输入: handler 执行 `get_data`
- 预期: 成功
- 断言: 返回体含 `segment_count`、`byte_count`、`data_base64`

**T27 — insert_state 返回帧扫描进度**
- 前置条件: fake 返回中断式扫描进度
- 输入: handler 执行 `insert_state`
- 预期: 成功
- 断言: 返回体含当前线号与总线号

**T28 — insert_stop 返回停止确认**
- 前置条件: fake 返回中断式停止确认
- 输入: handler 执行 `insert_stop`
- 预期: 成功
- 断言: 返回体含确认值

**T29 — test_com / test / insert_test 通信测试成功**
- 前置条件: fake 分别为主协议与中断式通道返回成功探测结果
- 输入: handler 执行 `test_com`、`test` 或 `insert_test`
- 预期: 成功
- 断言: 返回反馈值正确，并区分主协议与中断式通道

**T30 — radar_get_response_time / rgrt 返回响应时间统计**
- 前置条件: fake 返回四段响应时间数据
- 输入: handler 执行 `radar_get_response_time`
- 预期: 成功
- 断言: 返回体含 `t_min`、`t_max`、`t_ave`、`good_counter`

**T31 — wait / res 返回最近一次任务结果摘要**
- 前置条件: fake 已存在最近一次任务结果
- 输入: handler 执行 `wait` 或 `res`
- 预期: 成功
- 断言: 返回体含最近任务 `counter`、`command`、`result`

**T32 — 扫描器通过 export-meta 成功加载新 driver**
- 前置条件: 将新 driver 复制到临时 driver 目录
- 输入: `DriverManagerScanner::scan(driversDir, false, &stats)`
- 预期: `driver.meta.json` 被生成且扫描结果包含新 driver
- 断言: `stats.scanned == 1`

#### 测试代码

```cpp
TEST(ThreeDScanRobotProtocolTest, T01_DecodeValidFrame) {
    // 前置准备
    // 执行
    // 断言
}

TEST(ThreeDScanRobotHandlerTest, T23_GetLineReturnsAggregatedRawResult) {
    // 前置准备
    // 执行
    // 断言
}

TEST(ThreeDScanRobotHandlerTest, T27_InsertStateReturnsFrameProgress) {
    // 前置准备
    // 执行
    // 断言
}
```

### 6.2 冒烟测试脚本

- 脚本目录: `src/smoke_tests/`
- 脚本文件: `m103_3d_scan_robot.py`
- 统一入口: `python src/smoke_tests/run_smoke.py --plan m103_3d_scan_robot`
- CTest 接入: `smoke_m103_3d_scan_robot`
- 覆盖范围: driver 产物发现、`--export-meta`、控制类命令、扫描类命令、中断式命令、失败输出格式
- 用例清单:
  - `S01`: `--export-meta=<tmp>` -> 生成 `driver.meta.json` 且包含关键命令
  - `S02`: fake helper + `get_mode` / `set_mode` / `get_reg` / `test_com` / `move_dist` -> 返回结构化 JSON
  - `S03`: fake helper + `get_line` / `get_frame` / `get_data` -> 返回 `segment_count`、`byte_count`、`data_base64`
  - `S04`: fake helper + `insert_state` / `insert_stop` / `rgrt` -> 返回中断式与诊断数据
  - `S05`: fake helper 返回设备失败 -> 输出 `status:error` 且 `code=2`
- 失败输出规范: 缺少可执行文件时输出候选路径并返回 `FAIL`；JSON 解析失败时打印原始 stdout/stderr；超时打印 case 和等待时长
- 环境约束与跳过策略: 不依赖外部 Python 包；driver 或 fake helper 缺失时直接 `FAIL`；自动化测试仅连接 fake 串口 helper 或虚拟串口对
- 产物定位契约: 依次检查 `runtime_debug`、`runtime_release`、`build/debug`、`build/release`
- 跨平台运行契约: Windows 自动注入 `PATH`；文本解码统一 `utf-8`
- 可执行文件不存在时必须判定 `FAIL` 并输出候选路径，禁止静默通过

### 6.3 集成/端到端测试

- 驱动进程黑盒测试:
  - 启动 `stdio.drv.3d_scan_robot`
  - 通过 stdin 发送 JSONL，stdout 读取 `done/error`
  - 验证连续发 `get_mode -> move -> get_dist` 时，每条命令都必须显式传 `port/addr`，且命令间不共享串口状态
- Server 扫描链路测试:
  - 通过 `DriverManagerScanner` 调用 `--export-meta=<path>`
  - 验证生成 `driver.meta.json` 并成功加载

### 6.4 验收标准

- [ ] `stdio.drv.3d_scan_robot` 成功构建并进入 runtime 组装路径，对应用例：S01
- [ ] `meta.describe` 能列出 MatLab 兼容命令与示例，对应用例：T23、T24、T25、T27、T29、T30、S02
- [ ] `--export-meta=<path>` 成功生成 `driver.meta.json` 且可被扫描器识别，对应用例：T32、S01
- [ ] oneshot 模式约束生效：每条命令都必须显式传 `port/addr`，默认串口参数对齐 MatLab，对应用例：T08、T09、6.3
- [ ] 缺参和范围错误按当前框架返回 `400 / ValidationFailed`，对应用例：T08、T19
- [ ] 传输错误返回 `code=1`，对应用例：T10、T11、T20
- [ ] 协议错误、设备失败、分段不完整返回 `code=2`，对应用例：T13、T15、T17、T21、S05
- [ ] `get_line` / `get_frame` / `get_data` 能返回原始聚合结果，对应用例：T16、T23、T24、T26、S03
- [ ] `test_com`、`move_dist`、`wait`、`res`、`insert_test`、`insert_state`、`insert_stop`、`radar_get_response_time` 已纳入首批 MatLab 兼容命令，对应用例：T25、T27、T28、T29、T30、T31、S02、S04
- [ ] 资源释放路径已覆盖，对应用例：T18
- [ ] 冒烟脚本已接入 `run_smoke.py` 与 CTest，对应用例：S01-S05

## 7. 风险与控制

- 风险: 协议文档与参考代码对字段含义存在局部差异
  - 控制: 先以协议文档定义报文边界，再以参考 C++ 校准命令码与长任务时序
  - 控制: 对无法确认的状态位只返回原始值
  - 测试覆盖: T01-T06、T10-T16

- 风险: 长任务查询误吸收旧 counter/cmd
  - 控制: `waitTaskCompleted()` 强制比较 `lastCounter` 与 `lastCommand`
  - 控制: fake 提供“先返回旧任务结果”的异常场景
  - 测试覆盖: T11-T14

- 风险: 分段数据只做 happy path 会掩盖真实设备掉段问题
  - 控制: `collectScanData()` 对段号连续性、段数量和总字节数做显式校验
  - 控制: 返回体保留 `segment_count`、`byte_count`、`data_base64`
  - 测试覆盖: T15-T16、S03-S04

- 风险: 串口真实成功路径在自动化环境不可重复
  - 控制: 自动化主链路走 fake 串口 helper 或虚拟串口对
  - 控制: 串口路径自动覆盖参数校验、默认参数、打开失败与读超时
  - 控制: 真实串口成功路径通过 H01 联机验收单独记录

- 风险: `set_addr` / `set_mode` / `set_reg` 可能对真实设备写入永久状态
  - 控制: 自动化测试只对 fake 串口 helper 或虚拟串口设备执行写命令
  - 控制: 联机验收前记录原值，测试结束后写回
  - 控制: 如遇写入失败或设备状态异常，停止后续写命令联调，仅保留只读命令验收
  - 测试覆盖: T20、T21、S02、S05

- 风险: MatLab 兼容包装命令与底层协议命令语义不一致，导致“命令名兼容但结果不兼容”
  - 控制: `move_dist`、`wait`、`res`、`rgrt` 在文档和元数据中显式声明为包装命令
  - 控制: 对包装命令单独补 GTest 与 smoke，不仅复用底层命令测试
  - 测试覆盖: T25、T29、T30、T31、S02、S04

- 风险: 元数据与 `--export-meta` 导出行为不一致
  - 控制: `buildMeta()` 只有一个元数据源
  - 控制: 同时覆盖 `meta.describe` 与 `--export-meta` 两条链路
  - 测试覆盖: T24、S01

## 8. 里程碑完成定义（DoD）

- [ ] `src/drivers/driver_3d_scan_robot/` 代码完成并可编译
- [ ] `src/drivers/CMakeLists.txt` 已接入新驱动
- [ ] `src/tests/test_3d_scan_robot.cpp` 完成，T01-T32 实际执行通过
- [ ] `src/tests/test_driver_manager_scanner.cpp` 新增扫描兼容性测试并通过
- [ ] `src/tests/helpers/fake_3d_scan_robot_device.*` 与 `src/tests/test_3d_scan_robot_fake_main.cpp` 完成，GTest 与 smoke 共用同一套 fake
- [ ] `src/smoke_tests/m103_3d_scan_robot.py` 已新增并接入 `run_smoke.py` 与 CTest
- [ ] `python src/smoke_tests/run_smoke.py --plan m103_3d_scan_robot` 通过
- [ ] `meta.describe` 与 `--export-meta=<path>` 两条链路均通过
- [ ] 文档同步完成
- [ ] 向后兼容/迁移策略确认：命令名采用 MatLab 兼容命名，输入格式统一为 JSON，扫描类命令首版只返回原始聚合结果，明确排除 `show/show_trans/quit`

## 9. 依赖关系

### 9.1 外部参考依赖

| 资产 | 路径 | 用途 |
|------|------|------|
| 协议文档 | `D:\doc\产品\扫描机器人\resource\doc` | 确认报文格式、寄存器与结果码 |
| C++ 上位机 | `D:\doc\产品\扫描机器人\resource\cpp\radar_master` | 确认命令码、query 时序、分段取数 |
| C++ 模拟器 | `D:\doc\产品\扫描机器人\resource\cpp\radar_simulation` | 设计 fake 场景与联调验证 |
| MatLab 测试程序 | `D:\doc\产品\扫描机器人\resource\matlab\3D SCANNER CONFIG V615` | 确认命令集与参数习惯 |

### 9.2 仓库内强耦合依赖

| 子系统 | 依赖点 | 影响 |
|--------|--------|------|
| `src/stdiolink/driver/` | `DriverCore`、`IMetaCommandHandler`、`DriverMetaBuilder`、`ensureCommandExamples()` | 决定命令生命周期、参数校验与元数据行为 |
| `src/drivers/` | 现有 CMake 组装与 runtime 发布方式 | 决定目录结构与构建方式 |
| `src/tests/` | `stdiolink_tests`、辅助 stub executable 模式 | 决定 fake helper 的接入方式 |
| `src/stdiolink_server/scanner/` | `DriverManagerScanner` | 决定 `--export-meta` 验收口径 |
| `src/smoke_tests/` | `run_smoke.py`、`CMakeLists.txt` | 决定统一冒烟入口 |

## 10. 已确认实现决策

### D1. 传输方式
- 已确认: **首版只支持 `serial` 串口**
- 依据: 现有 MatLab 程序通过串口对象与设备交互，未使用 TCP 作为主链路。
- 实施约束:
  - 驱动只实现串口参数和串口收发逻辑。
  - 自动化测试使用 fake 串口设备或虚拟串口对。
  - 不实现 TCP 传输层，也不把 TCP 纳入验收范围。

### D2. `get_line` / `get_frame` / `get_data` 返回策略
- 已确认: **按现有 MatLab 工作流实现，Driver 内部完成查询与分段取数，并返回原始聚合结果**
- 依据:
  - MatLab 的 `get_line` / `get_frame` 会在启动扫描后继续查询任务状态。
  - MatLab 的 `get_data` 负责拉取大量数据分段，再交给本地显示逻辑处理。
  - 本里程碑明确不实现 `show` / `show_trans`、点云成像和坐标系转换，因此只返回原始聚合数据，不增加高层成像语义。
- 实施约束:
  - `get_line` / `get_frame` / `get_data` 返回 `task_counter`、`task_command`、`segment_count`、`byte_count`、`data_base64`、可选 `result_code`。
  - `get_data` 额外要求调用方显式传入 `total_bytes`，不跨命令保存最近一次扫描上下文。
  - 不在首版返回点云、坐标系变换结果、图像或渲染数据结构。

### D3. 命令号与时序兼容策略
- 已确认: **优先遵循现有 MatLab 代码的实际实现**
- 依据:
  - MatLab 中 `move_dist`、`radar_get_response_time`、`insert_test`、`insert_state`、`insert_stop` 已形成现场可用的调用路径。
  - 若协议文档、参考 C++ 枚举与 MatLab 实际实现存在局部差异，首版以 MatLab 行为为准，保证替换和联调可行。
- 实施约束:
  - 代码中保留注释，记录“MatLab 实际命令号/流程”与“协议文档编号”的映射关系。
  - 测试和 smoke 以 MatLab 现有命令流程作为验收基准。

### D4. 命令名与别名策略
- 已确认: **命令名严格沿用现有 MatLab 命名，并保留 MatLab 中已存在的缩写/别名**
- 依据:
  - 现场操作与参考脚本已经围绕 `get_fw_ver`、`get_dist`、`get_reg`、`rgrt` 等名称形成习惯。
  - 里程碑目标是尽量兼容 MatLab 程序，而不是重新设计一套新 API。
- 实施约束:
  - 主命令名沿用 MatLab 名称。
  - 保留 `test/dist/state/get_ver/get_dir/gr/sr/rgrt` 等兼容别名。
  - 不新增与 MatLab 风格无关的新命名体系。

## 11. 指令号与寄存器号对比结论

### 11.1 当前里程碑涉及的主命令号

| 能力 | MatLab | 参考 C++ | 结论 |
|------|--------|----------|------|
| `reg_read` | `1` | `CmdReadReg = 1` | 一致 |
| `reg_write` | `2` | `CmdWriteReg = 2` | 一致 |
| `calib*` | `4` | `CmdCalibration = 4` | 一致 |
| `query` / `wait` / `res` 底层查询 | `6` | `CmdQuery = 6` | 一致 |
| `move` | `7` | `CmdMove = 7` | 一致 |
| `get_dist` | `8` | `CmdGetDistance = 8` | 一致 |
| `get_line` | `10` | `CmdScanLine = 10` | 一致 |
| `get_frame` | `11` | `CmdScan = 11` | 一致 |
| `get_data` | `12` | `CmdGetData = 12` | 一致 |
| `insert_state` | 中断 `3` | `CmdInterruptGetScanProgress = 3` | 一致 |
| `insert_test` | 中断 `6` | `CmdInterruptTest = 6` | 一致 |
| `insert_stop` | 中断 `8` | `CmdInterruptScanCancel = 8` | 一致 |

### 11.2 已确认存在差异的命令号

| 能力 | MatLab 实现 | 参考 C++ | 结论 |
|------|-------------|----------|------|
| `get_addr` | 直接发主命令 `3` 到广播地址 `255`，通过回包中的设备地址识别当前地址 | 无对应 `CmdGetAddr`；C++ 倾向于通过 `regRead(RegSlaveAddress=13)` 获取地址 | **存在差异**，首版按 MatLab 实现 |
| `move_dist` | 主命令 `5` | `v3dradarproto.h` 未定义命令 `5` | **存在差异**，首版按 MatLab 实现；并在代码中注释其对应协议文档“获取指定角度距离” |
| `radar_get_response_time` / `rgrt` | 主命令 `17` | `v3dradarproto.h` 无对应命令定义 | **存在差异**，首版按 MatLab 实现 |

说明:
- `radar_get_response_time = 17` 与 `RegRadarMeasurementWaitTime = 17` 不是同一个概念；前者是 MatLab 使用的主命令号，后者是 C++ 枚举中的寄存器号，命名空间不同但数值相同，开发时必须避免混淆。
- 当前里程碑中凡是“MatLab 有而 C++ 枚举没有”的命令，都以 MatLab 现有脚本行为作为实现和测试基准。

### 11.3 当前里程碑涉及的寄存器号

| 能力 | MatLab | 参考 C++ `RegId` | 结论 |
|------|--------|------------------|------|
| `get_mode` / `set_mode` | `0` | `RegWorkMode = 0` | 一致 |
| `get_state` | `1` | `RegDeviceStatus = 1` | 一致 |
| `get_temp` MCU | `4` | `RegMicrocontrollerTemperature = 4` | 一致 |
| `get_temp` 板卡 | `5` | `RegDeviceTemperature = 5` | 一致 |
| `get_sw0` | `6` | `RegXAxisProximitySwitchState = 6` | 一致 |
| `get_sw1` | `7` | `RegYAxisProximitySwitchState = 7` | 一致 |
| `get_calib0` | `8` | `RegXAxisMotorCalibrationState = 8` | 一致 |
| `get_calib1` | `9` | `RegYAxisMotorCalibrationState = 9` | 一致 |
| `get_direction` | `10` | `RegRadarDirectionAngle = 10` | 一致 |
| `get_fw_ver` | `12` | `RegFirmwareVersion = 12` | 一致 |
| `set_addr` | `13` | `RegSlaveAddress = 13` | 一致 |

### 11.4 寄存器号对比结论

- 当前里程碑要实现的寄存器型能力，在 MatLab 与参考 C++ `RegId` 之间**未发现编号冲突**。
- 主要差异集中在“少数主命令号”的实现路径，而不是寄存器号。
- 因此首版实现策略为：
  - 寄存器读写按 C++ `RegId` 与 MatLab 一致编号实现。
  - `get_addr`、`move_dist`、`radar_get_response_time` 按 MatLab 实际命令流实现，并在代码和测试中保留差异说明。
