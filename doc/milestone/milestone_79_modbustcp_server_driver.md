# 里程碑 79：Modbus TCP Server 从站驱动

> **前置条件**: M09 (DriverMetaBuilder)、M15 (TypedEvent)、`tmp/modbustcpserver.h/cpp` 参考实现、`driver_modbusrtu/modbus_types.h/cpp` 数据类型与字节序转换
> **目标**: 实现 `driver_modbustcp_server` 从站驱动，作为 Modbus TCP Slave 监听 TCP 端口，通过 JSONL 命令管理 Unit 数据区并响应主站读写请求

## 1. 目标

- 新增 `driver_modbustcp_server` 驱动，输出可执行文件 `stdio.drv.modbustcp_server`
- 移植 `tmp/modbustcpserver.h/cpp` 参考实现，适配 stdiolink 框架（移除 `applog.h` 依赖）
- 实现 16 个 JSONL 命令：`status`、`start_server`/`stop_server`、`add_unit`/`remove_unit`/`list_units`、8 个数据访问命令、`set_registers_batch`/`get_registers_batch`
- 支持 KeepAlive 生命周期，通过 `event()` 推送 `client_connected`/`client_disconnected`/`data_written` 事件
- 完整元数据导出（`--export-meta`），DriverLab 可直接加载
- 单元测试覆盖所有命令的正常/边界/异常路径

## 2. 背景与问题

- 当前项目仅有 Modbus 主站驱动（`driver_modbustcp`、`driver_modbusrtu`），缺少从站模拟能力
- 开发和测试主站驱动时，需要外部 Modbus 模拟器，无法在 CI/CD 中自动化端到端测试
- `tmp/modbustcpserver.h/cpp` 已实现完整的 Modbus TCP 从站功能（多 Unit、四类数据区、8 种功能码、粘包处理），但依赖 `applog.h`，未适配 stdiolink JSONL 接口
- 本里程碑将参考实现封装为标准 stdiolink Driver，同时为后续 M80（RTU Over TCP Server）和 M81（RTU Serial Server）建立共享的数据区管理模式

**范围**:
- `ModbusTcpServer` 类：从 `tmp/` 移植并适配，移除 `applog.h`，改用 `qDebug`/`qWarning`
- `ModbusTcpServerHandler`：IMetaCommandHandler 实现，16 个命令的参数解析与分发
- `main.cpp`：DriverCore 初始化，KeepAlive 模式设置
- CMakeLists.txt：构建配置，链接 `Qt::Network`
- 单元测试：命令分发、数据区操作、批量寄存器类型转换

**非目标**:
- 不实现 RTU 帧格式（CRC16 校验）— 留给 M80
- 不实现串口通信（QSerialPort）— 留给 M81
- 不修改现有主站驱动代码
- 不修改 stdiolink 核心库或 Server 框架

## 3. 技术要点

### 3.1 架构：参考实现到 stdiolink Driver 的映射

```
  tmp/modbustcpserver.h/cpp          driver_modbustcp_server/
  ┌─────────────────────┐            ┌──────────────────────────────┐
  │ ModbusTcpServer     │  移植适配   │ modbus_tcp_server.h/cpp      │
  │ (QTcpServer 子类)   │ ─────────► │ (移除 applog.h，保留核心逻辑)  │
  │ - 多 Unit 管理      │            │ - 新增 dataWritten 信号参数    │
  │ - MBAP 解析/粘包    │            │                              │
  │ - 8 个 handle* 方法  │            │                              │
  └─────────────────────┘            └──────────────────────────────┘
                                              ▲
                                              │ 持有实例
                                     ┌────────┴───────────────────┐
                                     │ handler.h/cpp              │
                                     │ ModbusTcpServerHandler      │
                                     │ (IMetaCommandHandler)       │
                                     │ - 16 个命令分发             │
                                     │ - 事件推送 (event())        │
                                     │ - 批量操作 (modbus_types)   │
                                     │ - DriverMetaBuilder 元数据  │
                                     └────────────────────────────┘
```

### 3.2 命令分发与错误码策略

| 错误码 | 来源 | 含义 |
|--------|------|------|
| 0 | 驱动 | 成功 |
| 1 | 驱动 | 服务启动失败（端口占用等） |
| 3 | 驱动 | 业务逻辑校验失败 / 状态冲突 |
| 400 | 框架 | 元数据 Schema 校验失败 |
| 404 | 框架 | 未知命令 |

命令处理流程：
```
stdin JSONL → DriverCore → autoValidateParams() [400] → handle() → 业务逻辑 [0/1/3]
                                                                  ↓
                                                            resp.done() / resp.error()
```

### 3.3 事件推送机制

KeepAlive 模式下，通过 `IResponder::event()` 向 stdout 推送事件：

```cpp
// ModbusTcpServer 信号 → Handler 槽 → m_eventResponder.event() 推送
connect(m_server, &ModbusTcpServer::clientConnected,
        this, [this](const QString& addr, quint16 port) {
    m_eventResponder.event("client_connected", 0,
        QJsonObject{{"address", addr}, {"port", port}});
});
```

事件输出格式：
```json
{"status":"event","code":0,"data":{"event":"data_written","data":{"unit_id":1,"function_code":6,"address":100,"quantity":1}}}
```

### 3.4 批量寄存器操作与类型转换

`set_registers_batch` / `get_registers_batch` 复用 `driver_modbusrtu/modbus_types.h/cpp` 的 `ByteOrderConverter`：

```cpp
// get_registers_batch 示例：读取 4 个寄存器，按 float32 + big_endian 解析
QVector<quint16> raw = {0x4248, 0x0000, 0x42C8, 0x0000};
// ByteOrderConverter::registersToFloat32(raw[0..1], BigEndian) → 50.0f
// ByteOrderConverter::registersToFloat32(raw[2..3], BigEndian) → 100.0f
// values: [50.0, 100.0], raw: [0x4248, 0x0000, 0x42C8, 0x0000]
```

### 3.5 向后兼容

- 新增独立驱动，不修改任何现有功能代码（构建配置变更除外）
- 复用 `modbus_types.h/cpp` 通过 CMake `target_include_directories` 引用源文件，不修改原文件

## 4. 实现步骤

### 4.1 新增 `modbus_tcp_server.h` — 从站核心类声明

- 新增 `src/drivers/driver_modbustcp_server/modbus_tcp_server.h`：
  - 从 `tmp/modbustcpserver.h` 移植，移除 `#include "common/applog.h"`
  - 保留 `ModbusTCPHeader`、`ModbusDataArea`、`ClientInfo` 结构体
  - 保留 `ModbusTcpServer` 类（继承 `QTcpServer`），保留所有公共接口
  - 构造函数移除默认 `addUnit(1)` 调用（由 JSONL 命令显式添加）
  - 关键声明：
    ```cpp
    #pragma once
    #include <QMap>
    #include <QMutex>
    #include <QPointer>
    #include <QSharedPointer>
    #include <QTcpServer>
    #include <QTcpSocket>
    #include <QVector>

    struct ModbusDataArea {
        QVector<bool> coils;
        QVector<bool> discreteInputs;
        QVector<quint16> holdingRegisters;
        QVector<quint16> inputRegisters;
        explicit ModbusDataArea(int size = 10000);
    };

    class ModbusTcpServer : public QTcpServer {
        Q_OBJECT
    public:
        explicit ModbusTcpServer(QObject* parent = nullptr);
        bool startServer(quint16 port = 502);
        void stopServer();
        bool isRunning() const { return isListening(); }
        quint16 serverPort() const { return isListening() ? QTcpServer::serverPort() : 0; }

        bool addUnit(quint8 unitId, int dataAreaSize = 10000);
        bool removeUnit(quint8 unitId);
        bool hasUnit(quint8 unitId) const;
        QList<quint8> getUnits() const;

        // 数据访问接口（与 tmp/ 版本一致）
        bool setCoil(quint8 unitId, quint16 address, bool value);
        bool getCoil(quint8 unitId, quint16 address, bool& value);
        // ... 其余 get/set 方法签名不变

    signals:
        void clientConnected(QString address, quint16 port);
        void clientDisconnected(QString address, quint16 port);
        void dataWritten(quint8 unitId, quint8 functionCode,
                         quint16 address, quint16 quantity);
    };
    ```
  - 理由：保持与参考实现的接口一致性，便于后续 M80/M81 复用数据区管理逻辑

### 4.2 新增 `modbus_tcp_server.cpp` — 从站核心实现

- 新增 `src/drivers/driver_modbustcp_server/modbus_tcp_server.cpp`：
  - 从 `tmp/modbustcpserver.cpp` 移植全部实现
  - 替换 `LOGI`/`LOGW`/`LOGE` 为 `qInfo`/`qWarning`/`qCritical`
  - 构造函数不再默认调用 `addUnit(1)`：
    ```cpp
    ModbusTcpServer::ModbusTcpServer(QObject* parent)
        : QTcpServer(parent) {}
    ```
  - 保留所有功能码处理方法（`handleReadCoils` 等 8 个）、MBAP 解析、粘包处理、数据访问接口
  - 理由：核心协议逻辑已在参考实现中验证，移植时仅做依赖替换

### 4.3 新增 `handler.h` — Handler 类声明

- 新增 `src/drivers/driver_modbustcp_server/handler.h`：
  - `ModbusTcpServerHandler` 类实现 `IMetaCommandHandler`：
    ```cpp
    #pragma once
    #include "modbus_tcp_server.h"
    #include "stdiolink/driver/meta_command_handler.h"
    #include "stdiolink/driver/stdio_responder.h"

    class ModbusTcpServerHandler : public IMetaCommandHandler {
    public:
        ModbusTcpServerHandler();
        const DriverMeta& driverMeta() const override { return m_meta; }
        void handle(const QString& cmd, const QJsonValue& data,
                    IResponder& resp) override;
    private:
        void buildMeta();
        void connectEvents();
        DriverMeta m_meta;
        ModbusTcpServer m_server;
        StdioResponder m_eventResponder;
    };
    ```
  - 理由：Handler 声明独立为头文件，便于测试文件 include；使用持久 `StdioResponder` 成员避免 lambda 捕获 `IResponder&` 引用的生命周期问题

### 4.4 新增 `handler.cpp` — Handler 实现

- 新增 `src/drivers/driver_modbustcp_server/handler.cpp`：
  - 构造函数中调用 `buildMeta()` 和 `connectEvents()`：
    ```cpp
    ModbusTcpServerHandler::ModbusTcpServerHandler() {
        buildMeta();
        connectEvents();
    }
    ```
  - `handle()` 方法分发 16 个命令，关键逻辑：
    ```cpp
    void ModbusTcpServerHandler::handle(const QString& cmd,
            const QJsonValue& data, IResponder& resp) {
        QJsonObject p = data.toObject();

        if (cmd == "status") {
            QJsonArray units;
            for (auto id : m_server.getUnits()) units.append(id);
            resp.done(0, QJsonObject{
                {"status", "ready"},
                {"listening", m_server.isRunning()},
                {"port", m_server.isRunning() ? m_server.serverPort() : 0},
                {"units", units}});
        } else if (cmd == "start_server") {
            if (m_server.isRunning()) {
                resp.error(3, QJsonObject{{"message", "Server already running"}});
                return;
            }
            int port = p["listen_port"].toInt(502);
            if (!m_server.startServer(port)) {
                resp.error(1, QJsonObject{{"message",
                    QString("Failed to listen on port %1: %2")
                        .arg(port).arg(m_server.errorString())}});
                return;
            }
            resp.done(0, QJsonObject{{"started", true}, {"port", port}});
        }
        // ... 其余命令类似模式
    }
    ```
  - `connectEvents()` 连接 ModbusTcpServer 信号到 `m_eventResponder` 推送：
    ```cpp
    void ModbusTcpServerHandler::connectEvents() {
        connect(&m_server, &ModbusTcpServer::clientConnected,
                [this](const QString& addr, quint16 port) {
            m_eventResponder.event("client_connected", 0,
                QJsonObject{{"address", addr}, {"port", port}});
        });
        connect(&m_server, &ModbusTcpServer::clientDisconnected,
                [this](const QString& addr, quint16 port) {
            m_eventResponder.event("client_disconnected", 0,
                QJsonObject{{"address", addr}, {"port", port}});
        });
        connect(&m_server, &ModbusTcpServer::dataWritten,
                [this](quint8 unitId, quint8 fc, quint16 addr, quint16 qty) {
            m_eventResponder.event("data_written", 0, QJsonObject{
                {"unit_id", unitId}, {"function_code", fc},
                {"address", addr}, {"quantity", qty}});
        });
    }
    ```
  - `buildMeta()` 使用 DriverMetaBuilder 定义 16 个命令的参数 schema
  - 理由：遵循现有驱动的 Handler + ConnectionManager 模式；事件通过持久 `StdioResponder` 成员输出，避免捕获栈引用

### 4.5 新增 `main.cpp` — 入口函数

- 新增 `src/drivers/driver_modbustcp_server/main.cpp`：
    ```cpp
    #include "handler.h"
    #include "stdiolink/driver/driver_core.h"
    #include <QCoreApplication>

    int main(int argc, char* argv[]) {
        QCoreApplication app(argc, argv);
        ModbusTcpServerHandler handler;
        DriverCore core;
        core.setProfile(DriverCore::Profile::KeepAlive);
        core.setMetaHandler(&handler);
        return core.run(argc, argv);
    }
    ```
  - 理由：入口函数独立为单独文件，Handler 逻辑与 main 解耦，KeepAlive 模式需显式设置

### 4.6 新增 `CMakeLists.txt` — 构建配置

- 新增 `src/drivers/driver_modbustcp_server/CMakeLists.txt`：
    ```cmake
    cmake_minimum_required(VERSION 3.16)
    project(driver_modbustcp_server)
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

    set(MODBUSRTU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../driver_modbusrtu")

    add_executable(driver_modbustcp_server
        main.cpp
        handler.cpp
        modbus_tcp_server.cpp
        ${MODBUSRTU_DIR}/modbus_types.cpp
    )
    target_include_directories(driver_modbustcp_server PRIVATE
        ${MODBUSRTU_DIR}
    )
    target_link_libraries(driver_modbustcp_server PRIVATE
        stdiolink ${QT_LIBRARIES}
    )
    set_target_properties(driver_modbustcp_server PROPERTIES
        OUTPUT_NAME "stdio.drv.modbustcp_server"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    ```
  - 理由：引用 `driver_modbusrtu/modbus_types.cpp` 用于批量操作的类型转换

### 4.7 修改 `src/drivers/CMakeLists.txt` — 注册新驱动

- 修改 `src/drivers/CMakeLists.txt`：
  - 新增一行：
    ```cmake
    add_subdirectory(driver_modbustcp_server)
    ```
  - 理由：将新驱动纳入构建系统

## 5. 文件变更清单

### 5.1 新增文件
- `src/drivers/driver_modbustcp_server/CMakeLists.txt` — 构建配置
- `src/drivers/driver_modbustcp_server/modbus_tcp_server.h` — ModbusTcpServer 类声明
- `src/drivers/driver_modbustcp_server/modbus_tcp_server.cpp` — 从站核心实现（移植自 tmp/）
- `src/drivers/driver_modbustcp_server/handler.h` — Handler 类声明
- `src/drivers/driver_modbustcp_server/handler.cpp` — Handler 实现（16 个命令分发、事件推送、批量操作）
- `src/drivers/driver_modbustcp_server/main.cpp` — 入口函数

### 5.2 修改文件
- `src/drivers/CMakeLists.txt` — 新增 `add_subdirectory(driver_modbustcp_server)`
- `src/tests/CMakeLists.txt` — 将测试源文件及驱动源文件合并至 `stdiolink_tests` 主测试目标

### 5.3 测试文件
- `src/tests/test_modbustcp_server_handler.cpp` — Handler 命令分发单元测试

测试构建方式：在 `src/tests/CMakeLists.txt` 中将测试源文件 `test_modbustcp_server_handler.cpp` 及驱动源文件（`handler.cpp`、`modbus_tcp_server.cpp`、`modbus_types.cpp`）合并至 `stdiolink_tests` 主测试目标，通过 `--gtest_filter="*ModbusTcpServer*"` 筛选运行。（用户确认后由独立测试目标改为合并方案）

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: `ModbusTcpServerHandler::handle()` 的 16 个命令分发逻辑
- 用例分层: 正常路径（T01–T08）、边界值（T09–T11）、异常输入（T12–T16）、批量操作（T17–T19）
- 断言要点: 响应 code、data 字段值、错误 message 内容
- 桩替身策略: 使用 `MockResponder`（实现 `IResponder`，捕获 `done()`/`error()`/`event()` 调用）；直接构造 Handler 实例，不启动真实 TCP 监听（服务控制命令除外）
- 测试策略说明: 单元测试直接调用 Handler::handle()，绕过 DriverCore 的自动参数校验（code 400）。这是有意为之——单测聚焦 Handler 自身的业务逻辑；框架层校验由 DriverCore 既有测试覆盖
- 测试文件: `src/tests/test_modbustcp_server_handler.cpp`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `status`: 服务未启动 | 返回 listening=false, units=[] | T01 |
| `status`: 服务已启动 | 返回 listening=true, port, units 列表 | T02 |
| `start_server`: 正常启动 | 返回 started=true, port | T03 |
| `start_server`: 重复启动 | 返回 error code 3 | T12 |
| `stop_server`: 正常停止 | 返回 stopped=true | T04 |
| `stop_server`: 未启动时停止 | 返回 error code 3 | T13 |
| `add_unit`: 正常添加 | 返回 added=true, unit_id, data_area_size | T05 |
| `add_unit`: unit_id 已存在 | 返回 error code 3 | T14 |
| `remove_unit`: 正常移除 | 返回 removed=true | T06 |
| `list_units`: 列出所有 Unit | 返回 units 数组 | T07 |
| `set_coil` + `get_coil`: 正常读写 | 写入后读取值一致 | T08 |
| `set_holding_register` + `get_holding_register` | 写入后读取值一致 | T09 |
| `set_discrete_input` + `get_discrete_input` | 写入后读取值一致 | T10 |
| `set_input_register` + `get_input_register` | 写入后读取值一致 | T11 |
| 数据操作: unit_id 不存在 | 返回 error code 3 | T15 |
| 数据操作: address 越界 | 返回 error code 3 | T16 |
| `set_registers_batch`: float32 写入 | 写入 2 个寄存器，值正确 | T17 |
| `get_registers_batch`: float32 读取 | 按类型转换返回 values + raw | T18 |
| `get_registers_batch`: count 非类型整数倍 | 返回 error code 3 | T19 |

#### 用例详情

**T01 — status 服务未启动**
- 前置条件: 构造 Handler，不调用 start_server
- 输入: `handle("status", {}, resp)`
- 预期: `done(0, {"status":"ready","listening":false,"units":[]})`
- 断言: `resp.code == 0`; `resp.data["listening"] == false`; `resp.data["units"].toArray().isEmpty()`

**T02 — status 服务已启动且有 Unit**
- 前置条件: 调用 start_server(port=0 自动分配) + add_unit(unit_id=1)
- 输入: `handle("status", {}, resp)`
- 预期: `done(0, {"status":"ready","listening":true,"port":N,"units":[1]})`
- 断言: `resp.data["listening"] == true`; `resp.data["port"].toInt() > 0`; `resp.data["units"].toArray().size() == 1`

**T03 — start_server 正常启动**
- 前置条件: 构造 Handler
- 输入: `handle("start_server", {"listen_port": 0}, resp)`（端口 0 由 OS 自动分配）
- 预期: `done(0, {"started":true,"port":N})`
- 断言: `resp.code == 0`; `resp.data["started"] == true`

**T04 — stop_server 正常停止**
- 前置条件: 已调用 start_server
- 输入: `handle("stop_server", {}, resp)`
- 预期: `done(0, {"stopped":true})`
- 断言: `resp.code == 0`; `resp.data["stopped"] == true`

**T05 — add_unit 正常添加**
- 前置条件: 构造 Handler
- 输入: `handle("add_unit", {"unit_id":1, "data_area_size":100}, resp)`
- 预期: `done(0, {"added":true,"unit_id":1,"data_area_size":100})`
- 断言: `resp.code == 0`; `resp.data["unit_id"] == 1`

**T06 — remove_unit 正常移除**
- 前置条件: 已添加 unit_id=1
- 输入: `handle("remove_unit", {"unit_id":1}, resp)`
- 预期: `done(0, {"removed":true,"unit_id":1})`
- 断言: `resp.code == 0`

**T07 — list_units 列出所有 Unit**
- 前置条件: 添加 unit_id=1 和 unit_id=2
- 输入: `handle("list_units", {}, resp)`
- 预期: `done(0, {"units":[1,2]})`
- 断言: `resp.data["units"].toArray().size() == 2`

**T08 — set_coil + get_coil 正常读写**
- 前置条件: 添加 unit_id=1
- 输入: `handle("set_coil", {"unit_id":1,"address":0,"value":true}, resp)` 后 `handle("get_coil", {"unit_id":1,"address":0}, resp)`
- 预期: set 返回 `{"written":true}`，get 返回 `{"value":true}`
- 断言: set `resp.code == 0`; get `resp.data["value"] == true`

**T09 — set_holding_register + get_holding_register**
- 前置条件: 添加 unit_id=1
- 输入: set address=100, value=1234; get address=100
- 预期: get 返回 `{"value":1234}`
- 断言: `resp.data["value"] == 1234`

**T10 — set_discrete_input + get_discrete_input**
- 前置条件: 添加 unit_id=1
- 输入: set address=5, value=true; get address=5
- 预期: get 返回 `{"value":true}`
- 断言: `resp.data["value"] == true`

**T11 — set_input_register + get_input_register**
- 前置条件: 添加 unit_id=1
- 输入: set address=50, value=5678; get address=50
- 预期: get 返回 `{"value":5678}`
- 断言: `resp.data["value"] == 5678`

**T12 — start_server 重复启动**
- 前置条件: 已调用 start_server
- 输入: 再次 `handle("start_server", {"listen_port":0}, resp)`
- 预期: `error(3, {"message":"Server already running"})`
- 断言: `resp.code == 3`; `resp.data["message"]` 包含 "already running"

**T13 — stop_server 未启动时停止**
- 前置条件: 构造 Handler，不调用 start_server
- 输入: `handle("stop_server", {}, resp)`
- 预期: `error(3, {"message":"Server not running"})`
- 断言: `resp.code == 3`

**T14 — add_unit unit_id 已存在**
- 前置条件: 已添加 unit_id=1
- 输入: 再次 `handle("add_unit", {"unit_id":1}, resp)`
- 预期: `error(3, {"message":"Unit 1 already exists"})`
- 断言: `resp.code == 3`

**T15 — 数据操作 unit_id 不存在**
- 前置条件: 不添加任何 Unit
- 输入: `handle("get_coil", {"unit_id":99,"address":0}, resp)`
- 预期: `error(3, {"message":"Unit 99 not found"})`
- 断言: `resp.code == 3`

**T16 — 数据操作 address 越界**
- 前置条件: 添加 unit_id=1, data_area_size=100
- 输入: `handle("get_coil", {"unit_id":1,"address":200}, resp)`
- 预期: `error(3, {"message":...})` 包含地址越界信息
- 断言: `resp.code == 3`

**T17 — set_registers_batch float32 写入**
- 前置条件: 添加 unit_id=1
- 输入: `handle("set_registers_batch", {"unit_id":1,"area":"holding","address":0,"values":[50.0],"data_type":"float32","byte_order":"big_endian"}, resp)`
- 预期: `done(0, {"written":2})`（float32 占 2 个寄存器）
- 断言: `resp.code == 0`; `resp.data["written"] == 2`

**T18 — get_registers_batch float32 读取**
- 前置条件: T17 写入后
- 输入: `handle("get_registers_batch", {"unit_id":1,"area":"holding","address":0,"count":2,"data_type":"float32","byte_order":"big_endian"}, resp)`
- 预期: `done(0, {"values":[50.0],"raw":[...]})`
- 断言: `resp.data["values"].toArray()[0].toDouble()` 约等于 50.0; `resp.data["raw"].toArray().size() == 2`

**T19 — get_registers_batch count 非类型整数倍**
- 前置条件: 添加 unit_id=1
- 输入: `handle("get_registers_batch", {"unit_id":1,"area":"holding","address":0,"count":3,"data_type":"float32"}, resp)`
- 预期: `error(3, ...)` count=3 不是 float32 所需 2 寄存器的整数倍
- 断言: `resp.code == 3`

#### 测试代码

```cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

// MockResponder 捕获 done/error/event 调用
class MockResponder : public IResponder {
public:
    int lastCode = -1;
    QJsonObject lastData;
    QString lastStatus;
    QVector<QPair<QString, QJsonObject>> events;

    void done(int code, const QJsonValue& payload) override {
        lastStatus = "done"; lastCode = code; lastData = payload.toObject();
    }
    void error(int code, const QJsonValue& payload) override {
        lastStatus = "error"; lastCode = code; lastData = payload.toObject();
    }
    void event(int code, const QJsonValue& payload) override {
        Q_UNUSED(code); Q_UNUSED(payload);
    }
    void event(const QString& name, int code, const QJsonValue& data) override {
        Q_UNUSED(code);
        events.append({name, data.toObject()});
    }
    void reset() { lastCode = -1; lastData = {}; lastStatus.clear(); }
};

class ModbusTcpServerHandlerTest : public ::testing::Test {
protected:
    ModbusTcpServerHandler handler;
    MockResponder resp;

    void addUnit(int unitId, int size = 10000) {
        resp.reset();
        handler.handle("add_unit",
            QJsonObject{{"unit_id", unitId}, {"data_area_size", size}}, resp);
    }
    void startServer() {
        resp.reset();
        handler.handle("start_server",
            QJsonObject{{"listen_port", 0}}, resp);
    }
};

TEST_F(ModbusTcpServerHandlerTest, T01_StatusNotStarted) {
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["listening"].toBool(), false);
    EXPECT_TRUE(resp.lastData["units"].toArray().isEmpty());
}

TEST_F(ModbusTcpServerHandlerTest, T03_StartServer) {
    handler.handle("start_server",
        QJsonObject{{"listen_port", 0}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["started"].toBool(), true);
}

TEST_F(ModbusTcpServerHandlerTest, T05_AddUnit) {
    handler.handle("add_unit",
        QJsonObject{{"unit_id", 1}, {"data_area_size", 100}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["unit_id"].toInt(), 1);
}

TEST_F(ModbusTcpServerHandlerTest, T08_SetGetCoil) {
    addUnit(1);
    resp.reset();
    handler.handle("set_coil",
        QJsonObject{{"unit_id",1},{"address",0},{"value",true}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    resp.reset();
    handler.handle("get_coil",
        QJsonObject{{"unit_id",1},{"address",0}}, resp);
    EXPECT_EQ(resp.lastData["value"].toBool(), true);
}

TEST_F(ModbusTcpServerHandlerTest, T12_StartServerDuplicate) {
    startServer();
    resp.reset();
    handler.handle("start_server",
        QJsonObject{{"listen_port", 0}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
}

TEST_F(ModbusTcpServerHandlerTest, T15_DataOpUnitNotFound) {
    handler.handle("get_coil",
        QJsonObject{{"unit_id",99},{"address",0}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
}

TEST_F(ModbusTcpServerHandlerTest, T17_SetRegistersBatchFloat32) {
    addUnit(1);
    resp.reset();
    handler.handle("set_registers_batch", QJsonObject{
        {"unit_id",1},{"area","holding"},{"address",0},
        {"values", QJsonArray{50.0}},
        {"data_type","float32"},{"byte_order","big_endian"}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["written"].toInt(), 2);
}

TEST_F(ModbusTcpServerHandlerTest, T19_GetRegistersBatchCountMismatch) {
    addUnit(1);
    resp.reset();
    handler.handle("get_registers_batch", QJsonObject{
        {"unit_id",1},{"area","holding"},{"address",0},{"count",3},
        {"data_type","float32"}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
}
```

### 6.2 集成测试

- 启动 `stdio.drv.modbustcp_server` 进程，通过 stdin 发送 JSONL 命令序列
- 验证 `--export-meta` 输出合法 JSON，包含 16 个命令定义
- 使用外部 Modbus TCP 主站工具连接从站，验证读写操作和事件推送
- 验证 DriverLab 可加载该 Driver 并展示命令列表

### 6.3 验收标准

- [ ] `build.bat` 成功编译，输出 `stdio.drv.modbustcp_server.exe`，无编译警告（T01–T19）
- [ ] `--export-meta` 输出合法 JSON，包含 16 个命令定义
- [ ] status 命令正确反映服务状态（T01, T02）
- [ ] start_server / stop_server 正常工作，重复操作返回 error code 3（T03, T04, T12, T13）
- [ ] add_unit / remove_unit / list_units 正常工作（T05, T06, T07, T14）
- [ ] 8 个数据访问命令正常读写（T08–T11）
- [ ] unit_id 不存在和 address 越界返回 error code 3（T15, T16）
- [ ] set_registers_batch / get_registers_batch 类型转换正确（T17, T18, T19）
- [ ] KeepAlive 模式下事件推送正常
- [ ] 全量既有测试无回归

## 7. 风险与控制

- 风险: `tmp/modbustcpserver.cpp` 移植时遗漏 `applog.h` 宏替换，导致编译失败
  - 控制: 移植后全局搜索 `LOG[IWED]` 宏，确保全部替换为 `qInfo`/`qWarning`/`qCritical`
  - 测试覆盖: 编译通过即验证

- 风险: `modbus_types.h/cpp` 跨目录引用导致头文件路径冲突
  - 控制: 使用 `target_include_directories(PRIVATE)` 限制作用域，不污染全局
  - 测试覆盖: T17, T18

- 风险: 端口 0 自动分配在 CI 环境中可能冲突
  - 控制: 使用端口 0 让 OS 分配空闲端口，避免硬编码端口号
  - 测试覆盖: T03

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] `stdio.drv.modbustcp_server.exe` 可正常编译和运行
- [ ] 16 个命令全部实现，元数据导出正确
- [ ] KeepAlive 模式下事件推送正常
- [ ] 单元测试全部通过（T01–T19）
- [ ] 全量既有测试无回归
- [ ] 文档同步完成
- [ ] 向后兼容确认（不修改任何现有功能代码（构建配置变更除外））
