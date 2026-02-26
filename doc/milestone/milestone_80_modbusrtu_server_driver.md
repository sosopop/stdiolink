# 里程碑 80：Modbus RTU Over TCP Server 从站驱动

> **前置条件**: M79 (Modbus TCP Server 从站驱动)、`driver_modbusrtu/modbus_rtu_client.cpp` CRC16 算法
> **目标**: 实现 `driver_modbusrtu_server` 从站驱动，使用 RTU 帧格式（CRC16 校验）通过 TCP 通信，命令集与 M79 完全一致

## 1. 目标

- 新增 `driver_modbusrtu_server` 驱动，输出可执行文件 `stdio.drv.modbusrtu_server`
- 复用 M79 的数据区管理（`ModbusDataArea`、Unit 管理、8 个功能码处理方法）
- 协议层从 MBAP 头解析改为 RTU 帧 CRC16 校验，响应帧也带 CRC16
- 帧边界检测：超时定时器（50ms）+ 迭代 CRC16 验证
- 16 个 JSONL 命令、3 种事件推送、KeepAlive 生命周期、完整元数据导出
- 单元测试覆盖 CRC16 校验、RTU 帧解析、命令分发

## 2. 背景与问题

- M79 实现了 Modbus TCP（MBAP 头）从站，但部分工控场景使用 RTU Over TCP 协议（TCP 传输 + RTU 帧格式）
- RTU Over TCP 无 MBAP 头，帧格式为 `[Unit ID][FC][Data][CRC16]`，需通过 CRC16 校验数据完整性
- 帧边界无法通过 Length 字段确定，需使用超时 + CRC16 组合策略
- 本驱动与 M79 共享命令集和数据区管理，仅协议层不同

**范围**:
- `ModbusRtuServer` 类：基于 `QTcpServer`，RTU 帧解析 + CRC16 校验
- 复用 M79 的 `ModbusDataArea` 结构和 8 个功能码处理方法（复制为独立副本，不修改 M79 代码）
- `main.cpp`：Handler + DriverCore 集成
- 单元测试：CRC16、RTU 帧解析、命令分发

**非目标**:
- 不实现串口通信（QSerialPort）— 留给 M81
- 不修改 M79 的 Modbus TCP Server 代码
- 不修改 stdiolink 核心库

## 3. 技术要点

### 3.1 RTU 帧格式与 TCP 帧的差异

```
Modbus TCP (M79):
┌──────────────────────────────────────────────────┐
│ Transaction ID (2) │ Protocol ID (2) │ Length (2) │ Unit ID (1) │ FC (1) │ Data (N) │
└──────────────────────────────────────────────────┘
帧边界: Length 字段

RTU Over TCP (本里程碑):
┌──────────────────────────────────────┐
│ Unit ID (1) │ FC (1) │ Data (N) │ CRC16 (2) │
└──────────────────────────────────────┘
帧边界: 超时 (50ms) + CRC16 验证
```

### 3.2 帧边界检测策略

TCP 流中无天然帧边界，采用超时 + 迭代 CRC16 解析策略：

- 超时触发后，从缓冲区头部迭代尝试解析帧
- 对每次尝试：检查缓冲区 ≥4 字节，从 4 字节到 min(buffer.size(), 256) 字节逐步尝试 CRC16 匹配
- 找到有效帧后处理并从缓冲区移除，继续解析剩余数据
- 若无有效帧匹配，丢弃首字节重新同步

```cpp
// 伪代码
void onReadyRead(QTcpSocket* socket) {
    buffer.append(socket->readAll());
    frameTimer.start(50); // 50ms 超时
}

void onFrameTimeout() {
    while (buffer.size() >= 4) {
        bool found = false;
        // 尝试从 4 字节到 min(buffer.size(), 256) 字节匹配有效帧
        for (int len = 4; len <= qMin(buffer.size(), 256); ++len) {
            QByteArray candidate = buffer.left(len);
            uint16_t received = (uint8_t(candidate[len-1]) << 8)
                              | uint8_t(candidate[len-2]);
            uint16_t calculated = calculateCRC16(candidate.left(len-2));
            if (received == calculated) {
                processRtuRequest(candidate);
                buffer.remove(0, len);
                found = true;
                break;
            }
        }
        if (!found) {
            buffer.remove(0, 1); // 丢弃首字节，重新同步
        }
    }
}
```

### 3.3 CRC16 算法复用

复用 `driver_modbusrtu/modbus_rtu_client.cpp` 中的 `calculateCRC16` 静态查找表实现：

```cpp
// 多项式 0xA001 (Modbus 标准)
static const uint16_t crcTable[256] = { /* ... */ };

uint16_t calculateCRC16(const QByteArray& data) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < data.size(); i++) {
        crc = (crc >> 8) ^ crcTable[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}
```

### 3.4 Unit ID 不存在时的处理差异

RTU Over TCP 视为网关语义，与串口 RTU 从站行为不同：

| 场景 | RTU Over TCP Server (本驱动) | RTU Serial Server (M81) |
|------|------------------------------|-------------------------|
| Unit ID 不存在 | 返回异常响应（异常码 0x0B） | 静默不响应（RTU 标准） |

理由：TCP 网关需向主站明确报告目标设备不可达。

### 3.5 向后兼容

- 新增独立驱动，不修改 M79 或其他现有功能代码（构建配置变更除外）
- 命令集、参数 schema、错误码与 M79 完全一致

## 4. 实现步骤

### 4.1 新增 `modbus_rtu_server.h` — RTU 从站核心类声明

- 新增 `src/drivers/driver_modbusrtu_server/modbus_rtu_server.h`：
  - 复制 M79 的 `ModbusDataArea` 结构体定义（独立副本，不修改 M79 代码）
  - `ModbusRtuServer` 继承 `QTcpServer`，与 M79 的 `ModbusTcpServer` 接口一致：
    ```cpp
    #pragma once
    #include <QMap>
    #include <QMutex>
    #include <QPointer>
    #include <QSharedPointer>
    #include <QTcpServer>
    #include <QTcpSocket>
    #include <QTimer>
    #include <QVector>

    struct ModbusDataArea { /* 同 M79 */ };

    struct RtuClientInfo {
        QByteArray recvBuffer;
        QTimer* frameTimer;  // 50ms 帧超时定时器
        QString address;
        quint16 port;
    };

    class ModbusRtuServer : public QTcpServer {
        Q_OBJECT
    public:
        explicit ModbusRtuServer(QObject* parent = nullptr);
        bool startServer(quint16 port = 502);
        void stopServer();
        bool isRunning() const;

        // Unit 管理 + 数据访问接口（与 M79 一致）
        bool addUnit(quint8 unitId, int dataAreaSize = 10000);
        bool removeUnit(quint8 unitId);
        QList<quint8> getUnits() const;
        // ... set/get 方法签名同 M79

    signals:
        void clientConnected(QString address, quint16 port);
        void clientDisconnected(QString address, quint16 port);
        void dataWritten(quint8 unitId, quint8 functionCode,
                         quint16 address, quint16 quantity);

    public:
        static uint16_t calculateCRC16(const QByteArray& data);
        static QByteArray buildRtuResponse(quint8 unitId, const QByteArray& pdu);

    private:
        QByteArray processRtuRequest(const QByteArray& frame);
        QByteArray createRtuExceptionResponse(quint8 unitId, quint8 fc,
                                               quint8 exceptionCode);
    };
    ```
  - 理由：与 M79 保持接口一致，便于 Handler 层代码复用

### 4.2 新增 `modbus_rtu_server.cpp` — RTU 帧解析与 CRC16 校验

- 新增 `src/drivers/driver_modbusrtu_server/modbus_rtu_server.cpp`：
  - CRC16 静态查找表（复制自 `driver_modbusrtu/modbus_rtu_client.cpp`）
  - 帧边界检测：每个客户端连接持有独立的 `QTimer`（50ms 超时）和接收缓冲区
  - `onReadyRead()` 追加数据到缓冲区并重置定时器：
    ```cpp
    void ModbusRtuServer::onReadyRead() {
        auto* socket = qobject_cast<QTcpSocket*>(sender());
        QPointer<QTcpSocket> ptr(socket);
        if (!m_clients.contains(ptr)) return;

        auto& info = m_clients[ptr];
        info.recvBuffer.append(socket->readAll());
        info.frameTimer->start(50); // 重置 50ms 超时
    }
    ```
  - `onFrameTimeout()` 迭代解析缓冲区中的帧：
    ```cpp
    void ModbusRtuServer::onFrameTimeout(QTcpSocket* socket) {
        QPointer<QTcpSocket> ptr(socket);
        if (!m_clients.contains(ptr)) return;

        QByteArray& buffer = m_clients[ptr].recvBuffer;
        while (buffer.size() >= 4) {
            bool found = false;
            for (int len = 4; len <= qMin(buffer.size(), 256); ++len) {
                QByteArray candidate = buffer.left(len);
                uint16_t received = (uint8_t(candidate[len-1]) << 8)
                                  | uint8_t(candidate[len-2]);
                uint16_t calculated = calculateCRC16(candidate.left(len-2));
                if (received == calculated) {
                    QByteArray response = processRtuRequest(candidate);
                    buffer.remove(0, len);
                    if (!response.isEmpty()) {
                        socket->write(response);
                        socket->flush();
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                buffer.remove(0, 1); // 丢弃首字节，重新同步
            }
        }
    }
    ```
  - `processRtuRequest()` 解析 RTU 帧，分发到 8 个 handle* 方法（逻辑同 M79）
  - `buildRtuResponse()` 构建响应帧并附加 CRC16：
    ```cpp
    QByteArray ModbusRtuServer::buildRtuResponse(quint8 unitId,
                                                  const QByteArray& pdu) {
        QByteArray frame;
        frame.append(static_cast<char>(unitId));
        frame.append(pdu);
        uint16_t crc = calculateCRC16(frame);
        frame.append(static_cast<char>(crc & 0xFF));       // CRC low
        frame.append(static_cast<char>((crc >> 8) & 0xFF)); // CRC high
        return frame;
    }
    ```
  - 理由：RTU 帧格式无 MBAP 头，CRC16 为小端序（低字节在前）

### 4.3 新增 `handler.h` — Handler 类声明

- 新增 `src/drivers/driver_modbusrtu_server/handler.h`：
  - `ModbusRtuServerHandler` 类声明，与 M79 的 `ModbusTcpServerHandler` 接口一致
  - 持有 `ModbusRtuServer m_server` 实例替代 `ModbusTcpServer`
  - 持有 `StdioResponder m_eventResponder` 成员（用于事件推送）
  - 构造函数中调用 `connectEvents()` 完成事件连接
  - `handle()` / `buildMeta()` 方法声明
  - 理由：Handler 独立头文件便于测试文件 include；使用持久 `StdioResponder` 成员避免 lambda 捕获 `IResponder&` 引用的生命周期问题

### 4.4 新增 `handler.cpp` — Handler 实现

- 新增 `src/drivers/driver_modbusrtu_server/handler.cpp`：
  - 构造函数中调用 `buildMeta()` 和 `connectEvents()`
  - 16 个命令的 `handle()` 分发逻辑与 M79 相同（start_server 参数为 listen_port）
  - `connectEvents()` 连接 ModbusRtuServer 信号到 `m_eventResponder` 推送（模式同 M79）：
    ```cpp
    void ModbusRtuServerHandler::connectEvents() {
        connect(&m_server, &ModbusRtuServer::clientConnected,
                [this](const QString& addr, quint16 port) {
            m_eventResponder.event("client_connected", 0,
                QJsonObject{{"address", addr}, {"port", port}});
        });
        connect(&m_server, &ModbusRtuServer::clientDisconnected,
                [this](const QString& addr, quint16 port) {
            m_eventResponder.event("client_disconnected", 0,
                QJsonObject{{"address", addr}, {"port", port}});
        });
        connect(&m_server, &ModbusRtuServer::dataWritten,
                [this](quint8 unitId, quint8 fc, quint16 addr, quint16 qty) {
            m_eventResponder.event("data_written", 0, QJsonObject{
                {"unit_id", unitId}, {"function_code", fc},
                {"address", addr}, {"quantity", qty}});
        });
    }
    ```
  - `buildMeta()` 元数据定义与 M79 一致（命令集相同）
  - 理由：命令集与 M79 完全一致，仅底层协议不同

### 4.5 新增 `main.cpp` — 入口函数

- 新增 `src/drivers/driver_modbusrtu_server/main.cpp`：
  - `main()` 函数：
    ```cpp
    int main(int argc, char* argv[]) {
        QCoreApplication app(argc, argv);
        ModbusRtuServerHandler handler;
        DriverCore core;
        core.setProfile(DriverCore::Profile::KeepAlive);
        core.setMetaHandler(&handler);
        return core.run(argc, argv);
    }
    ```

### 4.6 新增 `CMakeLists.txt` — 构建配置

- 新增 `src/drivers/driver_modbusrtu_server/CMakeLists.txt`：
    ```cmake
    cmake_minimum_required(VERSION 3.16)
    project(driver_modbusrtu_server)
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

    add_executable(driver_modbusrtu_server
        main.cpp
        handler.cpp
        modbus_rtu_server.cpp
        ${MODBUSRTU_DIR}/modbus_types.cpp
    )
    target_include_directories(driver_modbusrtu_server PRIVATE
        ${MODBUSRTU_DIR}
    )
    target_link_libraries(driver_modbusrtu_server PRIVATE
        stdiolink ${QT_LIBRARIES}
    )
    set_target_properties(driver_modbusrtu_server PROPERTIES
        OUTPUT_NAME "stdio.drv.modbusrtu_server"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    ```

### 4.7 修改 `src/drivers/CMakeLists.txt` — 注册新驱动

- 修改 `src/drivers/CMakeLists.txt`：
  - 新增一行：`add_subdirectory(driver_modbusrtu_server)`

## 5. 文件变更清单

### 5.1 新增文件
- `src/drivers/driver_modbusrtu_server/CMakeLists.txt` — 构建配置
- `src/drivers/driver_modbusrtu_server/modbus_rtu_server.h` — RTU 从站类声明
- `src/drivers/driver_modbusrtu_server/modbus_rtu_server.cpp` — RTU 帧解析 + CRC16 + 功能码处理
- `src/drivers/driver_modbusrtu_server/handler.h` — Handler 类声明
- `src/drivers/driver_modbusrtu_server/handler.cpp` — Handler 实现
- `src/drivers/driver_modbusrtu_server/main.cpp` — 入口函数

### 5.2 修改文件
- `src/drivers/CMakeLists.txt` — 新增 `add_subdirectory(driver_modbusrtu_server)`
- `src/tests/CMakeLists.txt` — 新增测试文件

### 5.3 测试文件
- `src/tests/test_modbusrtu_server.cpp` — CRC16 + RTU 帧解析 + Handler 命令分发测试
- 测试构建方式：在 `src/tests/CMakeLists.txt` 中新增独立测试目标 `test_modbusrtu_server`，编译 `handler.cpp`、`modbus_rtu_server.cpp` 及测试文件，链接 `stdiolink`、`GTest::gtest`、`Qt6::Core`、`Qt6::Network`。不修改 `stdiolink_tests` 主测试目标。

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: CRC16 算法、RTU 帧解析/构建、Handler 命令分发
- 用例分层: CRC16 正确性（T01–T02）、RTU 帧处理（T03–T06）、命令分发（T07–T10）
- 断言要点: CRC16 值、帧解析结果、响应 code/data
- 桩替身策略: `MockResponder` 同 M79；RTU 帧测试直接构造二进制帧数据
- 测试策略说明: 单元测试直接调用 Handler::handle()，绕过 DriverCore 的自动参数校验（code 400）。单测聚焦 Handler 业务逻辑；框架层校验由 DriverCore 既有测试覆盖
- 测试文件: `src/tests/test_modbusrtu_server.cpp`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `calculateCRC16`: 已知数据 | CRC16 值与标准值一致 | T01 |
| `calculateCRC16`: 空数据 | 返回 0xFFFF | T02 |
| RTU 帧: CRC 正确 | 正常处理并返回响应帧 | T03 |
| RTU 帧: CRC 错误 | 静默丢弃，无响应 | T04 |
| RTU 帧: 长度不足 (<4 字节) | 丢弃，无响应 | T05 |
| RTU 帧: 缓冲区含多帧 | 两帧均被正确处理 | T06 |
| Handler: start_server 正常 | 返回 started=true | T07 |
| Handler: add_unit + set/get | 数据读写一致 | T08 |
| Handler: unit_id 不存在 | 返回 error code 3 | T09 |
| `buildRtuResponse`: 响应帧格式 | UnitID + PDU + CRC16 | T10 |

#### 用例详情

**T01 — CRC16 已知数据校验**
- 前置条件: 无
- 输入: `calculateCRC16(QByteArray::fromHex("0103000A0001"))` (Unit=1, FC=3, Addr=10, Qty=1)
- 预期: CRC16 值与 Modbus 标准计算器结果一致
- 断言: 返回值 == 预计算的 CRC16 值

**T02 — CRC16 空数据**
- 前置条件: 无
- 输入: `calculateCRC16(QByteArray())`
- 预期: 返回 0xFFFF（CRC 初始值）
- 断言: 返回值 == 0xFFFF

**T03 — RTU 帧 CRC 正确时正常处理**
- 前置条件: 添加 unit_id=1，设置 holding register[0]=1234
- 输入: 构造 FC 0x03 读保持寄存器请求帧（含正确 CRC16），通过 TCP 发送
- 预期: 收到响应帧，包含 UnitID + FC + Data + CRC16
- 断言: 响应帧 CRC16 校验通过；数据字段包含 1234

**T04 — RTU 帧 CRC 错误时静默丢弃**
- 前置条件: 添加 unit_id=1
- 输入: 构造请求帧，篡改 CRC16 字节
- 预期: 无响应（50ms 超时后无数据返回）
- 断言: TCP socket 无可读数据

**T05 — RTU 帧长度不足**
- 前置条件: 无
- 输入: 发送 3 字节数据
- 预期: 无响应
- 断言: TCP socket 无可读数据

**T06 — 缓冲区含多帧**
- 前置条件: 添加 unit_id=1，设置 holding register[0]=1234
- 输入: 在一次 TCP 发送中拼接两个完整 RTU 请求帧（均含正确 CRC16）
- 预期: 两帧均被正确处理，收到两个响应帧
- 断言: 收到两个响应帧，各自 CRC16 校验通过

**T07 — Handler start_server 正常启动**
- 前置条件: 构造 Handler
- 输入: `handle("start_server", {"listen_port":0}, resp)`
- 预期: `done(0, {"started":true,"port":N})`
- 断言: `resp.code == 0`

**T08 — Handler add_unit + set/get holding register**
- 前置条件: 构造 Handler
- 输入: add_unit(1) → set_holding_register(1, 0, 1234) → get_holding_register(1, 0)
- 预期: get 返回 `{"value":1234}`
- 断言: `resp.data["value"] == 1234`

**T09 — Handler unit_id 不存在**
- 前置条件: 不添加任何 Unit
- 输入: `handle("get_coil", {"unit_id":99,"address":0}, resp)`
- 预期: `error(3, ...)`
- 断言: `resp.code == 3`

**T10 — buildRtuResponse 响应帧格式**
- 前置条件: 无
- 输入: `buildRtuResponse(1, QByteArray::fromHex("0302004D2"))` (FC=3, 2 bytes, value=1234)
- 预期: 帧格式为 `[0x01][PDU][CRC_L][CRC_H]`
- 断言: 帧长度 == 1 + PDU.size() + 2; CRC16 校验通过

#### 测试代码

```cpp
#include <gtest/gtest.h>
#include <QByteArray>
#include <QCoreApplication>
#include <QJsonObject>
#include "modbus_rtu_server.h"

// CRC16 测试
TEST(ModbusRtuServerCRC, T01_KnownData) {
    // FC03 读保持寄存器: Unit=1, FC=3, Addr=10, Qty=1
    QByteArray data = QByteArray::fromHex("0103000A0001");
    uint16_t crc = ModbusRtuServer::calculateCRC16(data);
    // 预计算值（可用在线 Modbus CRC 计算器验证）
    EXPECT_NE(crc, 0);
    // 验证对称性：附加 CRC 后整帧校验应为 0
    QByteArray frame = data;
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    EXPECT_EQ(ModbusRtuServer::calculateCRC16(frame), 0);
}

TEST(ModbusRtuServerCRC, T02_EmptyData) {
    EXPECT_EQ(ModbusRtuServer::calculateCRC16(QByteArray()), 0xFFFF);
}

// Handler 命令测试（复用 MockResponder）
TEST_F(ModbusRtuServerHandlerTest, T07_StartServer) {
    handler.handle("start_server",
        QJsonObject{{"listen_port", 0}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["started"].toBool(), true);
}

TEST_F(ModbusRtuServerHandlerTest, T08_SetGetHoldingRegister) {
    addUnit(1);
    resp.reset();
    handler.handle("set_holding_register",
        QJsonObject{{"unit_id",1},{"address",0},{"value",1234}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    resp.reset();
    handler.handle("get_holding_register",
        QJsonObject{{"unit_id",1},{"address",0}}, resp);
    EXPECT_EQ(resp.lastData["value"].toInt(), 1234);
}

TEST_F(ModbusRtuServerHandlerTest, T09_UnitNotFound) {
    handler.handle("get_coil",
        QJsonObject{{"unit_id",99},{"address",0}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
}
```

### 6.2 集成测试

- 启动 `stdio.drv.modbusrtu_server` 进程，验证 `--export-meta` 输出合法 JSON
- 使用 `driver_modbusrtu`（RTU Over TCP 主站）连接从站，验证 CRC16 校验和读写操作
- 验证主站发送 CRC 错误帧时从站不响应

### 6.3 验收标准

- [ ] `build.bat Release` 成功编译，输出 `stdio.drv.modbusrtu_server.exe`，无编译警告
- [ ] `--export-meta` 输出合法 JSON，包含 16 个命令定义（T07）
- [ ] CRC16 算法正确（T01, T02）
- [ ] CRC 正确的 RTU 帧正常处理并返回响应（T03, T10）
- [ ] CRC 错误 / 长度不足静默丢弃（T04, T05）；缓冲区含多帧时均被正确处理（T06）
- [ ] 16 个命令正常工作（T07, T08, T09）
- [ ] KeepAlive 模式下事件推送正常
- [ ] 全量既有测试无回归

## 7. 风险与控制

- 风险: 50ms 帧超时在高延迟网络环境下可能误判帧边界
  - 控制: 50ms 是 RTU Over TCP 的常用经验值，覆盖绝大多数局域网场景
  - 控制: 超时后 CRC16 校验作为二次验证，错误帧会被丢弃而非误处理

- 风险: CRC16 查找表从 `modbus_rtu_client.cpp` 复制，后续维护可能不同步
  - 控制: CRC16 算法是 Modbus 标准定义，不会变更
  - 测试覆盖: T01, T02

- 风险: 多客户端并发时帧定时器管理复杂度
  - 控制: 每个客户端连接持有独立的 `QTimer` 和缓冲区，互不干扰
  - 控制: 客户端断开时清理定时器和缓冲区

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] `stdio.drv.modbusrtu_server.exe` 可正常编译和运行
- [ ] RTU 帧 CRC16 校验正确
- [ ] 16 个命令全部实现，元数据导出正确
- [ ] KeepAlive 模式下事件推送正常
- [ ] 单元测试全部通过（T01–T10）
- [ ] 全量既有测试无回归
- [ ] 文档同步完成
- [ ] 向后兼容确认（不修改任何现有功能代码（构建配置变更除外））

