# 里程碑 83：PLC 升降装置 Modbus TCP 驱动

> **前置条件**: `driver_modbustcp/modbus_client.h/cpp` Modbus TCP 客户端
> **目标**: 实现 `driver_plc_crane` 专用驱动，将 PLC 寄存器地址映射为语义化命令（气缸升降、阀门开关、模式切换），复用现有 Modbus TCP 协议栈

## 1. 目标

- 新增 `driver_plc_crane` 驱动，输出可执行文件 `stdio.drv.plc_crane`
- 复用 `driver_modbustcp/modbus_client.h/cpp` 的 Modbus TCP 协议栈，不重复实现
- 实现 6 个语义化命令：status、read_status、set_mode、set_run、cylinder_control、valve_control
- 将原始寄存器地址和功能码封装为业务语义，屏蔽底层协议细节
- 参数值域受限于协议文档定义的合法枚举（如 action: up/down/stop）
- 单元测试覆盖命令分发、参数校验、寄存器映射

## 2. 背景与问题

- 高炉 PLC 升降装置通过 Modbus TCP 通信，使用固定的寄存器地址映射
- 通用 `driver_modbustcp` 需要用户了解寄存器地址和功能码，操作门槛高
- 本驱动将寄存器操作封装为语义化命令，降低使用复杂度
- 复用现有 Modbus TCP 客户端，仅新增业务层封装

**范围**:
- `handler.h/cpp`：Handler 实现 6 个命令，寄存器地址映射，连接管理
- `main.cpp`：入口函数
- 复用 `driver_modbustcp/modbus_client.h/cpp`
- 单元测试：命令分发、参数校验

**非目标**:
- 不修改 `driver_modbustcp` 的代码
- 不实现通用 Modbus 读写命令（由 `driver_modbustcp` 提供）
- 不修改 stdiolink 核心库

## 3. 技术要点

### 3.1 寄存器地址映射

| 命令 | Modbus 操作 | 地址 | 值映射 |
|------|-------------|------|--------|
| `read_status` | FC 0x02 读离散输入 | 9, 10, 13, 14 | cylinder_up/down, valve_open/closed |
| `set_mode` | FC 0x06 写单寄存器 | 3 | manual→0, auto→1 |
| `set_run` | FC 0x06 写单寄存器 | 2 | start→1, stop→0 |
| `cylinder_control` | FC 0x06 写单寄存器 | 0 | up→1, down→2, stop→0 |
| `valve_control` | FC 0x06 写单寄存器 | 1 | open→1, close→2, stop→0 |

### 3.2 复用架构

```
  driver_modbustcp/                    driver_plc_crane/
  ┌─────────────────────┐             ┌──────────────────────────┐
  │ modbus_client.h/cpp │  直接引用    │ handler.h/cpp            │
  │ - TCP 连接管理      │ ◄────────── │ PlcCraneHandler          │
  │ - MBAP 帧构建/解析  │             │ - 6 个语义化命令          │
  │ - 8 个功能码方法    │             │ - 寄存器地址映射          │
  │                     │             │ - 内部连接缓存            │
  └─────────────────────┘             │ main.cpp — 入口函数       │
                                      └──────────────────────────┘
```

- Handler 直接调用 `ModbusClient` 的 `readDiscreteInputs()` 和 `writeSingleRegister()` 方法
- Handler 内部维护连接缓存，复用 `ModbusClient` 的 TCP 连接

### 3.3 错误码策略

| 错误码 | 来源 | 含义 |
|--------|------|------|
| 0 | 驱动 | 成功 |
| 1 | 驱动 | TCP 连接失败 |
| 2 | 驱动 | Modbus 通讯错误（超时、异常码） |
| 3 | 驱动 | 参数校验失败（非法 action/mode） |
| 400 | 框架 | 元数据 Schema 校验失败 |
| 404 | 框架 | 未知命令 |

### 3.4 向后兼容

- 新增独立驱动，不修改任何现有功能代码（构建配置变更除外）
- 复用 `modbus_client.h/cpp` 通过 CMake `target_include_directories` 引用

## 4. 实现步骤

### 4.1 新增 `handler.h` — Handler 类声明

- 新增 `src/drivers/driver_plc_crane/handler.h`：
    ```cpp
    class PlcCraneHandler : public IMetaCommandHandler {
    public:
        PlcCraneHandler() { buildMeta(); }
        const DriverMeta& driverMeta() const override { return m_meta; }
        void handle(const QString& cmd, const QJsonValue& data,
                    IResponder& resp) override;
    private:
        void buildMeta();
        ModbusClient* getClient(const QString& host, int port, int timeout, IResponder& resp);
        DriverMeta m_meta;
        QHash<modbus::ConnectionKey, std::shared_ptr<modbus::ModbusClient>> m_connections;
    };
    ```

### 4.2 新增 `handler.cpp` — Handler 实现（6 个命令 + 寄存器映射）

- 新增 `src/drivers/driver_plc_crane/handler.cpp`：
  - `getClient()` 方法管理连接缓存：
    ```cpp
    ModbusClient* PlcCraneHandler::getClient(const QString& host, int port,
            int timeout, IResponder& resp) {
        modbus::ConnectionKey key{host, port};
        auto it = m_connections.find(key);
        if (it != m_connections.end() && it->get()->isConnected()) {
            it->get()->setTimeout(timeout);
            return it->get();
        }
        auto client = std::make_shared<modbus::ModbusClient>(timeout);
        if (!client->connectToServer(host, port)) {
            resp.error(1, QJsonObject{{"message",
                QString("Failed to connect to %1:%2").arg(host).arg(port)}});
            return nullptr;
        }
        m_connections[key] = client;
        return client.get();
    }
    ```
  - `handle()` 方法分发 6 个命令：
    ```cpp
    void PlcCraneHandler::handle(const QString& cmd,
            const QJsonValue& data, IResponder& resp) {
        QJsonObject p = data.toObject();

        if (cmd == "status") {
            resp.done(0, QJsonObject{{"status", "ready"}});
            return;
        }

        // 提取连接参数
        QString host = p["host"].toString();
        int port = p["port"].toInt(502);
        int unitId = p["unit_id"].toInt(1);
        int timeout = p["timeout"].toInt(3000);

        auto* client = getClient(host, port, timeout, resp);
        if (!client) return;
        client->setUnitId(unitId);
        // 分发到具体命令...
    }
    ```
  - `read_status` 命令：一次读取地址 9-14 并映射为语义字段：
    ```cpp
    if (cmd == "read_status") {
        // 一次读取地址 9-14（6 个离散输入），覆盖气缸和阀门状态
        auto result = client->readDiscreteInputs(9, 6);
        if (!result.success) {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
            return;
        }
        resp.done(0, QJsonObject{
            {"cylinder_up", result.coils[0]},     // 地址 9
            {"cylinder_down", result.coils[1]},   // 地址 10
            {"valve_open", result.coils[4]},      // 地址 13
            {"valve_closed", result.coils[5]}});  // 地址 14
    }
    ```
  - 写命令统一模式（以 `cylinder_control` 为例）：
    ```cpp
    if (cmd == "cylinder_control") {
        QString action = p["action"].toString();
        quint16 value;
        if (action == "up") value = 1;
        else if (action == "down") value = 2;
        else if (action == "stop") value = 0;
        else {
            resp.error(3, QJsonObject{{"message",
                QString("Invalid action: '%1', expected: up, down, stop")
                    .arg(action)}});
            return;
        }
        auto result = client->writeSingleRegister(0, value);
        if (!result.success) {
            resp.error(2, QJsonObject{{"message", result.errorMessage}});
            return;
        }
        resp.done(0, QJsonObject{{"written", true}, {"action", action}});
    }
    ```
  - `valve_control`（地址 1）、`set_mode`（地址 3）、`set_run`（地址 2）同理，仅地址和枚举值不同
  - `buildMeta()` 使用 DriverMetaBuilder 定义 6 个命令：
    ```cpp
    void PlcCraneHandler::buildMeta() {
        DriverMetaBuilder b;
        b.setName("plc_crane")
         .setVersion("1.0.0")
         .setDescription("PLC Crane Controller (Modbus TCP)");

        // status — 无参数
        b.addCommand("status", "Driver health check");

        // read_status — 读取气缸和阀门状态
        auto& readStatus = b.addCommand("read_status", "Read cylinder and valve status");
        readStatus.addParam("host", "string", "PLC IP address").required();
        readStatus.addParam("port", "integer", "Modbus TCP port").defaultValue(502);
        readStatus.addParam("unit_id", "integer", "Modbus unit ID").defaultValue(1);
        readStatus.addParam("timeout", "integer", "Timeout in ms").defaultValue(3000);

        // cylinder_control — 气缸升降
        auto& cylinder = b.addCommand("cylinder_control", "Control cylinder up/down/stop");
        cylinder.addParam("host", "string", "PLC IP address").required();
        cylinder.addParam("port", "integer", "Modbus TCP port").defaultValue(502);
        cylinder.addParam("unit_id", "integer", "Modbus unit ID").defaultValue(1);
        cylinder.addParam("timeout", "integer", "Timeout in ms").defaultValue(3000);
        cylinder.addParam("action", "string", "Action: up, down, stop")
            .required().enumValues({"up", "down", "stop"});

        // valve_control、set_mode、set_run 类似，仅 action 枚举不同
    }
    ```

### 4.3 新增 `main.cpp` — 入口函数

- 新增 `src/drivers/driver_plc_crane/main.cpp`：
    ```cpp
    int main(int argc, char* argv[]) {
        QCoreApplication app(argc, argv);
        PlcCraneHandler handler;
        DriverCore core;
        core.setMetaHandler(&handler);
        return core.run(argc, argv);
    }
    ```
  - 理由：OneShot 模式，每次命令独立执行，无需 KeepAlive

### 4.4 新增 `CMakeLists.txt` — 构建配置

- 新增 `src/drivers/driver_plc_crane/CMakeLists.txt`：
    ```cmake
    cmake_minimum_required(VERSION 3.16)
    project(driver_plc_crane)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    find_package(Qt6 COMPONENTS Core Network QUIET)
    if(NOT Qt6_FOUND)
        find_package(Qt5 COMPONENTS Core Network REQUIRED)
        set(QT_LIBRARIES Qt5::Core Qt5::Network)
    else()
        set(QT_LIBRARIES Qt6::Core Qt6::Network)
    endif()

    if(NOT TARGET stdiolink)
        find_package(stdiolink REQUIRED)
    endif()

    set(MODBUSTCP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../driver_modbustcp")

    add_executable(driver_plc_crane
        main.cpp
        handler.cpp
        ${MODBUSTCP_DIR}/modbus_client.cpp
    )
    target_include_directories(driver_plc_crane PRIVATE
        ${MODBUSTCP_DIR}
    )
    target_link_libraries(driver_plc_crane PRIVATE
        stdiolink ${QT_LIBRARIES}
    )
    set_target_properties(driver_plc_crane PROPERTIES
        OUTPUT_NAME "stdio.drv.plc_crane"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    ```
  - 理由：引用 `driver_modbustcp/modbus_client.cpp`，编译 `handler.cpp` 实现业务逻辑

### 4.5 修改 `src/drivers/CMakeLists.txt` — 注册新驱动

- 修改 `src/drivers/CMakeLists.txt`：
  - 新增一行：`add_subdirectory(driver_plc_crane)`

## 5. 文件变更清单

### 5.1 新增文件
- `src/drivers/driver_plc_crane/CMakeLists.txt` — 构建配置
- `src/drivers/driver_plc_crane/handler.h` — Handler 类声明
- `src/drivers/driver_plc_crane/handler.cpp` — Handler 实现（6 个命令 + 寄存器映射 + 连接管理）
- `src/drivers/driver_plc_crane/main.cpp` — 入口函数

### 5.2 修改文件
- `src/drivers/CMakeLists.txt` — 新增 `add_subdirectory(driver_plc_crane)`
- `src/tests/CMakeLists.txt` — 新增测试文件

### 5.3 测试文件
- `src/tests/test_plc_crane.cpp` — 命令分发 + 参数校验测试

测试构建方式：在 `src/tests/CMakeLists.txt` 中将测试源文件 `test_plc_crane.cpp` 及驱动源文件（`handler.cpp`、`modbus_client.cpp`、`modbus_types.cpp`）合并至 `stdiolink_tests` 主测试目标，通过 `--gtest_filter="*PlcCrane*"` 筛选运行。（用户确认后由独立测试目标改为合并方案）

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: `PlcCraneHandler::handle()` 的 6 个命令分发逻辑、参数枚举校验
- 用例分层: 正常路径（T01–T04）、参数校验（T05–T08）
- 断言要点: 响应 code、data 字段值、错误 message 内容
- 桩替身策略: `MockResponder` 同 M79；连接失败通过不可达端口（如 59999）触发
- 测试策略说明: 单元测试直接调用 Handler::handle()，绕过 DriverCore 的自动参数校验（code 400）。单测聚焦 Handler 业务逻辑；框架层校验由 DriverCore 既有测试覆盖
- 测试文件: `src/tests/test_plc_crane.cpp`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| Handler: status | 返回 ready | T01 |
| Handler: read_status 连接失败 | 返回 error code 1 | T02 |
| Handler: cylinder_control 参数校验 | action=up/down/stop 合法 | T03 |
| Handler: cylinder_control 非法 action | 返回 error code 3 | T04 |
| Handler: valve_control 非法 action | 返回 error code 3 | T05 |
| Handler: set_mode 非法 mode | 返回 error code 3 | T06 |
| Handler: set_run 非法 action | 返回 error code 3 | T07 |
| Handler: read_status 连接参数提取 | host/port/unit_id 正确解析 | T08 |

#### 用例详情

**T01 — Handler status 命令**
- 前置条件: 构造 Handler
- 输入: `handle("status", {}, resp)`
- 预期: `done(0, {"status":"ready"})`
- 断言: `resp.code == 0`; `resp.data["status"] == "ready"`

**T02 — Handler read_status 连接失败**
- 前置条件: 构造 Handler
- 输入: `handle("read_status", {"host":"127.0.0.1","port":59999,"unit_id":1,"timeout":100}, resp)`
- 预期: `error(1, ...)`（连接失败）
- 断言: `resp.code == 1`

**T03 — Handler cylinder_control 合法 action**
- 前置条件: 构造 Handler
- 输入: `handle("cylinder_control", {"host":"127.0.0.1","port":59999,"action":"up","timeout":100}, resp)`
- 预期: 参数解析通过，因连接失败返回 error code 1（action 枚举校验已通过）
- 断言: `resp.code == 1`

**T04 — Handler cylinder_control 非法 action**
- 前置条件: 构造 Handler
- 输入: `handle("cylinder_control", {"host":"127.0.0.1","action":"invalid"}, resp)`
- 预期: `error(3, {"message":"Invalid action: 'invalid', expected: up, down, stop"})`
- 断言: `resp.code == 3`; message 包含 "invalid"

**T05 — Handler valve_control 非法 action**
- 前置条件: 构造 Handler
- 输入: `handle("valve_control", {"host":"127.0.0.1","action":"invalid"}, resp)`
- 预期: `error(3, ...)` action 不在 open/close/stop 中
- 断言: `resp.code == 3`; message 包含 "open, close, stop"

**T06 — Handler set_mode 非法 mode**
- 前置条件: 构造 Handler
- 输入: `handle("set_mode", {"host":"127.0.0.1","mode":"invalid"}, resp)`
- 预期: `error(3, ...)` mode 不在 manual/auto 中
- 断言: `resp.code == 3`; message 包含 "manual, auto"

**T07 — Handler set_run 非法 action**
- 前置条件: 构造 Handler
- 输入: `handle("set_run", {"host":"127.0.0.1","action":"invalid"}, resp)`
- 预期: `error(3, ...)` action 不在 start/stop 中
- 断言: `resp.code == 3`; message 包含 "start, stop"

**T08 — Handler read_status 连接参数提取**
- 前置条件: 构造 Handler
- 输入: `handle("read_status", {"host":"10.0.0.1","port":59999,"unit_id":5,"timeout":100}, resp)`
- 预期: 参数正确提取（因连接失败返回 error code 1），message 包含 "10.0.0.1:59999"
- 断言: `resp.code == 1`; message 包含 host 和 port 信息

#### 测试代码

```cpp
#include <gtest/gtest.h>
#include <QJsonObject>

// MockResponder 同 M79
class PlcCraneHandlerTest : public ::testing::Test {
protected:
    PlcCraneHandler handler;
    MockResponder resp;
};

TEST_F(PlcCraneHandlerTest, T01_Status) {
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["status"].toString(), "ready");
}

TEST_F(PlcCraneHandlerTest, T04_CylinderControlInvalidAction) {
    handler.handle("cylinder_control", QJsonObject{
        {"host","127.0.0.1"},{"action","invalid"}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("invalid"));
}

TEST_F(PlcCraneHandlerTest, T05_ValveControlInvalidAction) {
    handler.handle("valve_control", QJsonObject{
        {"host","127.0.0.1"},{"action","invalid"}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("open, close, stop"));
}

TEST_F(PlcCraneHandlerTest, T06_SetModeInvalidMode) {
    handler.handle("set_mode", QJsonObject{
        {"host","127.0.0.1"},{"mode","invalid"}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("manual, auto"));
}

TEST_F(PlcCraneHandlerTest, T07_SetRunInvalidAction) {
    handler.handle("set_run", QJsonObject{
        {"host","127.0.0.1"},{"action","invalid"}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("start, stop"));
}
```

### 6.2 集成测试

- 启动 `stdio.drv.plc_crane` 进程，验证 `--export-meta` 输出合法 JSON
- 验证 6 个命令定义包含正确的参数 schema 和枚举约束
- 通过 `driver_modbustcp_server`（M79）模拟 PLC，验证端到端读写操作
- 验证 DriverLab 可加载该 Driver 并展示命令列表

### 6.3 验收标准

- [ ] `build.bat` 成功编译，输出 `stdio.drv.plc_crane.exe`，无编译警告
- [ ] `--export-meta` 输出合法 JSON，包含 6 个命令定义
- [ ] 6 个命令参数 schema 正确，枚举约束完整
- [ ] status 命令返回 ready（T01）
- [ ] 连接失败正确返回 error code 1（T02, T08）
- [ ] 4 个写命令的参数枚举校验正确（T04–T07）
- [ ] 合法参数不触发校验错误（T03）
- [ ] 全量既有测试无回归

## 7. 风险与控制

- 风险: `driver_modbustcp/modbus_client.cpp` 跨目录引用导致编译路径问题
  - 控制: 使用 `target_include_directories(PRIVATE)` 限制作用域
  - 测试覆盖: 编译通过即验证

- 风险: PLC 寄存器地址映射与实际设备不一致
  - 控制: 地址映射集中在 `handle()` 方法中，修改成本低
  - 控制: 寄存器地址和枚举值均来自协议文档，有明确定义

- 风险: 测试环境无真实 PLC 设备，无法验证端到端通信
  - 控制: 单元测试聚焦参数校验和命令分发逻辑
  - 控制: 集成测试使用 M79 的 `driver_modbustcp_server` 模拟 PLC

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] `stdio.drv.plc_crane.exe` 可正常编译和运行
- [ ] 6 个语义化命令全部实现，元数据导出正确
- [ ] 寄存器地址映射与协议文档一致
- [ ] 参数枚举校验完整（action、mode）
- [ ] 单元测试全部通过（T01–T08）
- [ ] 全量既有测试无回归
- [ ] 文档同步完成
- [ ] 向后兼容确认（不修改任何现有功能代码（构建配置变更除外））

