# 里程碑 81：Modbus RTU 串口从站驱动

> **前置条件**: M80 (Modbus RTU Over TCP Server 从站驱动)、`driver_modbusrtu/modbus_rtu_client.cpp` CRC16 算法
> **目标**: 实现 `driver_modbusrtu_serial_server` 从站驱动，通过 QSerialPort 监听串口，使用 RTU 帧格式 + T3.5 帧间隔检测响应主站请求

## 1. 目标

- 新增 `driver_modbusrtu_serial_server` 驱动，输出可执行文件 `stdio.drv.modbusrtu_serial_server`
- 传输层从 `QTcpServer` 改为 `QSerialPort`，单连接（串口独占）
- 实现 T3.5 帧间隔检测：波特率 ≤ 19200 按公式计算，> 19200 固定 1.75ms
- 复制 M80 的 CRC16 算法、数据区管理、功能码处理逻辑（独立副本，非提取共享）
- 命令集与 M79/M80 一致（start_server 参数改为串口参数），无 client_connected/disconnected 事件
- 支持广播地址（Unit ID=0）：仅处理写操作，不发送响应
- 单元测试覆盖 T3.5 计算、串口参数校验、命令分发

## 2. 背景与问题

- M80 实现了 RTU Over TCP 从站，但工控现场常需通过 RS-485 串口直连
- 串口通信与 TCP 的核心差异：单连接、T3.5 帧间隔检测、无连接事件
- T3.5 静默间隔是 Modbus RTU 标准的帧边界检测机制，替代 TCP 的超时策略
- 本驱动复用 M80 的 RTU 帧处理逻辑，仅替换传输层

**范围**:
- `ModbusRtuSerialServer` 类：基于 `QSerialPort`，T3.5 帧间隔 + CRC16
- 串口参数配置：port_name、baud_rate、data_bits、stop_bits、parity
- `main.cpp`：Handler + DriverCore 集成
- 单元测试：T3.5 计算、命令分发

**非目标**:
- 不修改 M79/M80 的代码
- 不实现串口自动发现（由用户指定 port_name）
- 不支持多串口并发监听（单串口独占）

## 3. 技术要点

### 3.1 与 M80 (RTU Over TCP Server) 的核心差异

| 方面 | RTU Over TCP Server (M80) | RTU Serial Server (本里程碑) |
|------|---------------------------|------------------------------|
| 传输层 | `QTcpServer` + `QTcpSocket` | `QSerialPort` |
| 连接模式 | 多客户端并发 | 单连接（串口独占） |
| 帧边界检测 | 50ms 超时 + CRC16 | T3.5 静默间隔 + CRC16 |
| 连接事件 | `client_connected` / `client_disconnected` | 无 |
| Unit ID 不存在 | 返回异常响应（0x0B） | 静默不响应（RTU 标准） |
| 广播地址 (ID=0) | 不适用 | 仅处理写操作，不发送响应 |
| CMake 依赖 | `Qt::Network` | `Qt::SerialPort` |

### 3.2 T3.5 帧间隔计算

```cpp
// T3.5 = 3.5 × (1 start + data_bits + parity_bit + stop_bits) / baud_rate × 1000 ms
double calculateT35(int baudRate, int dataBits, bool hasParity, double stopBits) {
    if (baudRate > 19200) return 1.75; // Modbus 标准建议固定值

    double bitsPerChar = 1.0 + dataBits + (hasParity ? 1 : 0) + stopBits;
    return 3.5 * bitsPerChar / baudRate * 1000.0; // 毫秒
}

// 示例：9600 baud, 8N1 → 3.5 × 10 / 9600 × 1000 = 3.646 ms
// 示例：115200 baud → 固定 1.75 ms
```

### 3.3 串口数据接收与帧组装

```
  QSerialPort::readyRead
       │
       ▼
  buffer.append(data)
       │
       ├─ buffer.size() > 256 → 丢弃缓冲区，重新同步
       │
       └─ 启动/重置 T3.5 定时器
              │
              ▼ (T3.5 超时)
         CRC16 校验
              │
              ├─ 通过 → processRtuRequest()
              │           │
              │           ├─ Unit ID 匹配 → 处理并发送响应
              │           ├─ Unit ID=0 (广播) → 仅处理写操作，不响应
              │           └─ Unit ID 不匹配 → 静默不响应
              │
              └─ 失败 → 丢弃缓冲区
```

### 3.4 错误码策略

| 错误码 | 来源 | 含义 |
|--------|------|------|
| 0 | 驱动 | 成功 |
| 1 | 驱动 | 串口打开失败 |
| 3 | 驱动 | 业务逻辑校验失败 / 状态冲突 |
| 400 | 框架 | 元数据 Schema 校验失败 |
| 404 | 框架 | 未知命令 |

### 3.5 向后兼容

- 新增独立驱动，不修改任何现有功能代码（构建配置变更除外）

## 4. 实现步骤

### 4.1 新增 `modbus_rtu_serial_server.h` — 串口从站类声明

- 新增 `src/drivers/driver_modbusrtu_serial_server/modbus_rtu_serial_server.h`：
  - 复用 `ModbusDataArea` 结构体（同 M79/M80）
  - `ModbusRtuSerialServer` 类基于 `QSerialPort`：
    ```cpp
    #pragma once
    #include <QMap>
    #include <QMutex>
    #include <QObject>
    #include <QSerialPort>
    #include <QSharedPointer>
    #include <QTimer>
    #include <QVector>

    struct ModbusDataArea { /* 同 M79 */ };

    class ModbusRtuSerialServer : public QObject {
        Q_OBJECT
    public:
        explicit ModbusRtuSerialServer(QObject* parent = nullptr);
        ~ModbusRtuSerialServer();

        bool startServer(const QString& portName, int baudRate = 9600,
                         int dataBits = 8, const QString& stopBits = "1",
                         const QString& parity = "none");
        void stopServer();
        bool isRunning() const;
        QString portName() const;

        // Unit 管理 + 数据访问（同 M79/M80）
        bool addUnit(quint8 unitId, int dataAreaSize = 10000);
        bool removeUnit(quint8 unitId);
        QList<quint8> getUnits() const;
        bool setCoil(quint8 unitId, quint16 address, bool value);
        // ... 其余 get/set 方法

    signals:
        void dataWritten(quint8 unitId, quint8 functionCode,
                         quint16 address, quint16 quantity);

    private slots:
        void onReadyRead();
        void onFrameTimeout();

    public:
        static uint16_t calculateCRC16(const QByteArray& data);
        static double calculateT35(int baudRate, int dataBits,
                                    bool hasParity, double stopBits);

    private:
        QByteArray processRtuRequest(const QByteArray& frame);

        QSerialPort* m_serial = nullptr;
        QTimer m_frameTimer;
        QByteArray m_recvBuffer;
        double m_t35Ms = 3.646; // 默认 9600 8N1
        QMap<quint8, QSharedPointer<ModbusDataArea>> m_unitDataAreas;
        mutable QMutex m_mutex;
    };
    ```
  - 理由：单连接模型，无需 `QTcpServer` 的多客户端管理

### 4.2 新增 `modbus_rtu_serial_server.cpp` — T3.5 帧间隔与 RTU 帧处理

- 新增 `src/drivers/driver_modbusrtu_serial_server/modbus_rtu_serial_server.cpp`：
  - CRC16 静态查找表（复制自 `driver_modbusrtu/modbus_rtu_client.cpp`）
  - `startServer()` 打开串口并计算 T3.5：
    ```cpp
    bool ModbusRtuSerialServer::startServer(const QString& portName,
            int baudRate, int dataBits,
            const QString& stopBits, const QString& parity) {
        if (m_serial) return false; // 已在运行

        m_serial = new QSerialPort(this);
        m_serial->setPortName(portName);
        m_serial->setBaudRate(baudRate);
        m_serial->setDataBits(static_cast<QSerialPort::DataBits>(dataBits));
        // 设置 stopBits、parity（字符串转枚举）...

        if (!m_serial->open(QIODevice::ReadWrite)) {
            delete m_serial; m_serial = nullptr;
            return false;
        }

        // 计算 T3.5
        bool hasParity = (parity != "none");
        double stopBitsVal = (stopBits == "1.5") ? 1.5 : stopBits.toDouble();
        m_t35Ms = calculateT35(baudRate, dataBits, hasParity, stopBitsVal);

        m_frameTimer.setSingleShot(true);
        connect(m_serial, &QSerialPort::readyRead, this, &ModbusRtuSerialServer::onReadyRead);
        connect(&m_frameTimer, &QTimer::timeout, this, &ModbusRtuSerialServer::onFrameTimeout);
        return true;
    }
    ```
  - `onReadyRead()` 追加数据并重置 T3.5 定时器：
    ```cpp
    void ModbusRtuSerialServer::onReadyRead() {
        m_recvBuffer.append(m_serial->readAll());
        if (m_recvBuffer.size() > 256) {
            m_recvBuffer.clear(); // 超过 RTU 最大 ADU，丢弃
            return;
        }
        m_frameTimer.start(qMax(1, static_cast<int>(qCeil(m_t35Ms))));
    }
    ```
  - `onFrameTimeout()` CRC16 校验 + 帧处理：
    ```cpp
    void ModbusRtuSerialServer::onFrameTimeout() {
        if (m_recvBuffer.size() < 4) {
            m_recvBuffer.clear(); return;
        }
        // CRC16 校验
        uint16_t received = (uint8_t(m_recvBuffer[m_recvBuffer.size()-1]) << 8)
                          | uint8_t(m_recvBuffer[m_recvBuffer.size()-2]);
        uint16_t calculated = calculateCRC16(m_recvBuffer.left(m_recvBuffer.size()-2));
        if (received != calculated) {
            m_recvBuffer.clear(); return; // CRC 失败，静默丢弃
        }

        QByteArray response = processRtuRequest(m_recvBuffer);
        m_recvBuffer.clear();
        if (!response.isEmpty() && m_serial && m_serial->isOpen()) {
            m_serial->write(response);
            m_serial->flush();
        }
    }
    ```
  - `processRtuRequest()` 解析 RTU 帧，处理广播地址和 Unit ID 匹配：
    ```cpp
    QByteArray ModbusRtuSerialServer::processRtuRequest(const QByteArray& frame) {
        quint8 unitId = static_cast<quint8>(frame[0]);
        quint8 fc = static_cast<quint8>(frame[1]);
        QByteArray pdu = frame.mid(1, frame.size() - 3); // 去掉 UnitID 和 CRC16

        // 广播地址（Unit ID=0）：仅处理写操作，不发送响应
        if (unitId == 0) {
            if (fc == 0x05 || fc == 0x06 || fc == 0x0F || fc == 0x10) {
                // 对所有已注册 Unit 执行写操作
                for (auto it = m_unitDataAreas.begin(); it != m_unitDataAreas.end(); ++it) {
                    handleWriteRequest(it.key(), pdu);
                }
            }
            return QByteArray(); // 广播不响应
        }

        // Unit ID 不匹配 → 静默不响应（RTU 标准）
        if (!m_unitDataAreas.contains(unitId)) {
            return QByteArray();
        }

        // 正常处理：分发到 handle* 方法（逻辑同 M80）
        QByteArray responsePdu = dispatchFunctionCode(unitId, pdu);
        return buildRtuResponse(unitId, responsePdu);
    }
    ```
  - `buildRtuResponse()` 构建响应帧（同 M80）：UnitID + PDU + CRC16（小端序）
  - 理由：串口 RTU 标准要求 Unit ID 不匹配时静默不响应，与 M80 的网关语义不同

### 4.3 新增 `handler.h` — Handler 类声明

- 新增 `src/drivers/driver_modbusrtu_serial_server/handler.h`：
  - `ModbusRtuSerialServerHandler` 类声明，继承 `IMetaCommandHandler`
  - 持有 `ModbusRtuSerialServer m_server` 实例
  - 持有 `StdioResponder m_eventResponder` 成员（用于事件推送）
  - 构造函数中调用 `connectEvents()` 完成事件连接
  - `handle()` / `buildMeta()` 声明
  - 理由：Handler 独立头文件便于测试文件 include

### 4.4 新增 `handler.cpp` — Handler 实现

- 新增 `src/drivers/driver_modbusrtu_serial_server/handler.cpp`：
  - `start_server` 命令参数改为串口参数：
    ```cpp
    if (cmd == "start_server") {
        if (m_server.isRunning()) {
            resp.error(3, QJsonObject{{"message", "Server already running"}});
            return;
        }
        QString portName = p["port_name"].toString();
        int baudRate = p["baud_rate"].toInt(9600);
        int dataBits = p["data_bits"].toInt(8);
        QString stopBits = p["stop_bits"].toString("1");
        QString parity = p["parity"].toString("none");

        if (!m_server.startServer(portName, baudRate, dataBits, stopBits, parity)) {
            resp.error(1, QJsonObject{{"message",
                QString("Failed to open serial port: %1").arg(portName)}});
            return;
        }
        resp.done(0, QJsonObject{{"started", true}, {"port_name", portName}});
    }
    ```
  - `status` 命令返回串口信息替代 TCP 端口：
    ```cpp
    if (cmd == "status") {
        QJsonArray units;
        for (auto id : m_server.getUnits()) units.append(id);
        resp.done(0, QJsonObject{
            {"status", "ready"},
            {"listening", m_server.isRunning()},
            {"port_name", m_server.isRunning() ? m_server.portName() : ""},
            {"units", units}});
    }
    ```
  - 事件连接使用持久 `m_eventResponder` 成员，在构造函数中调用 `connectEvents()`：
    ```cpp
    void connectEvents() {
        connect(&m_server, &ModbusRtuSerialServer::dataWritten,
                [this](quint8 unitId, quint8 fc, quint16 addr, quint16 qty) {
            m_eventResponder.event("data_written", 0, QJsonObject{
                {"unit_id", unitId}, {"function_code", fc},
                {"address", addr}, {"quantity", qty}});
        });
    }
    ```
  - 其余 14 个命令（stop_server、add_unit、remove_unit、list_units、8 个数据访问、2 个批量操作）逻辑与 M79/M80 完全相同
  - `buildMeta()` 元数据定义：`start_server` 参数 schema 改为串口参数（port_name 必填、baud_rate/data_bits/stop_bits/parity 可选带枚举约束）
  - 理由：命令集与 M79/M80 一致，仅 start_server 参数和事件推送有差异

### 4.5 新增 `main.cpp` — 入口函数

- 新增 `src/drivers/driver_modbusrtu_serial_server/main.cpp`：
    ```cpp
    int main(int argc, char* argv[]) {
        QCoreApplication app(argc, argv);
        ModbusRtuSerialServerHandler handler;
        DriverCore core;
        core.setProfile(DriverCore::Profile::KeepAlive);
        core.setMetaHandler(&handler);
        return core.run(argc, argv);
    }
    ```
  - 理由：main.cpp 仅包含入口函数，Handler 逻辑在 handler.h/cpp 中

### 4.6 新增 `CMakeLists.txt` — 构建配置

- 新增 `src/drivers/driver_modbusrtu_serial_server/CMakeLists.txt`：
    ```cmake
    cmake_minimum_required(VERSION 3.16)
    project(driver_modbusrtu_serial_server)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    find_package(Qt6 COMPONENTS Core SerialPort QUIET)
    if(NOT Qt6_FOUND)
        find_package(Qt5 COMPONENTS Core SerialPort REQUIRED)
        set(QT_LIBRARIES Qt5::Core Qt5::SerialPort)
    else()
        set(QT_LIBRARIES Qt6::Core Qt6::SerialPort)
    endif()

    if(NOT TARGET stdiolink)
        find_package(stdiolink REQUIRED)
    endif()

    set(MODBUSRTU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../driver_modbusrtu")

    add_executable(driver_modbusrtu_serial_server
        main.cpp
        handler.cpp
        modbus_rtu_serial_server.cpp
        ${MODBUSRTU_DIR}/modbus_types.cpp
    )
    target_include_directories(driver_modbusrtu_serial_server PRIVATE
        ${MODBUSRTU_DIR}
    )
    target_link_libraries(driver_modbusrtu_serial_server PRIVATE
        stdiolink ${QT_LIBRARIES}
    )
    set_target_properties(driver_modbusrtu_serial_server PROPERTIES
        OUTPUT_NAME "stdio.drv.modbusrtu_serial_server"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    ```
  - 理由：依赖 `Qt::SerialPort` 替代 M80 的 `Qt::Network`

### 4.7 修改 `src/drivers/CMakeLists.txt` — 注册新驱动

- 修改 `src/drivers/CMakeLists.txt`：
  - 新增一行：`add_subdirectory(driver_modbusrtu_serial_server)`

## 5. 文件变更清单

### 5.1 新增文件
- `src/drivers/driver_modbusrtu_serial_server/CMakeLists.txt` — 构建配置
- `src/drivers/driver_modbusrtu_serial_server/modbus_rtu_serial_server.h` — 串口从站类声明
- `src/drivers/driver_modbusrtu_serial_server/modbus_rtu_serial_server.cpp` — T3.5 帧间隔 + CRC16 + 功能码处理
- `src/drivers/driver_modbusrtu_serial_server/handler.h` — Handler 类声明
- `src/drivers/driver_modbusrtu_serial_server/handler.cpp` — Handler 实现
- `src/drivers/driver_modbusrtu_serial_server/main.cpp` — 入口函数

### 5.2 修改文件
- `src/drivers/CMakeLists.txt` — 新增 `add_subdirectory(driver_modbusrtu_serial_server)`
- `src/tests/CMakeLists.txt` — 将测试源文件及驱动源文件合并至 `stdiolink_tests` 主测试目标，通过 `--gtest_filter="*ModbusRtuSerialServer*"` 筛选运行。（用户确认后由独立测试目标改为合并方案）

### 5.3 测试文件
- `src/tests/test_modbusrtu_serial_server.cpp` — T3.5 计算 + CRC16 + Handler 命令分发测试

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: T3.5 计算、CRC16 算法、Handler 命令分发
- 用例分层: T3.5 计算（T01–T03）、CRC16（T04）、命令分发（T05–T10）
- 断言要点: T3.5 值精度、响应 code/data、串口参数校验
- 桩替身策略: `MockResponder` 同 M79/M80；串口操作通过 Handler 层测试（不依赖真实串口）
- 测试策略说明: 单元测试直接调用 Handler::handle()，绕过 DriverCore 的自动参数校验（code 400）。单测聚焦 Handler 业务逻辑；框架层校验由 DriverCore 既有测试覆盖
- 测试文件: `src/tests/test_modbusrtu_serial_server.cpp`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `calculateT35`: 9600 baud, 8N1 | 返回 ≈3.646 ms | T01 |
| `calculateT35`: 19200 baud, 8E1 | 按公式计算（≤19200） | T02 |
| `calculateT35`: 115200 baud | 返回固定 1.75 ms | T03 |
| `calculateCRC16`: 已知数据 | CRC16 值与标准值一致 | T04 |
| Handler: start_server with COM_TEST | 测试环境无 COM_TEST，预期 error code 1 | T05 |
| Handler: stop_server 未启动 | 返回 error code 3 | T06 |
| Handler: add_unit + set/get | 数据读写一致 | T07 |
| Handler: unit_id 不存在 | 返回 error code 3 | T08 |
| Handler: status 未启动 | listening=false | T09 |
| Handler: stop_server 未启动 | 返回 error code 3 | T10 |

#### 用例详情

**T01 — T3.5 计算：9600 baud, 8N1**
- 前置条件: 无
- 输入: `calculateT35(9600, 8, false, 1.0)`
- 预期: 3.5 × 10 / 9600 × 1000 ≈ 3.646 ms
- 断言: `qAbs(result - 3.646) < 0.01`

**T02 — T3.5 计算：19200 baud, 8E1（含校验位）**
- 前置条件: 无
- 输入: `calculateT35(19200, 8, true, 1.0)`
- 预期: 3.5 × 11 / 19200 × 1000 ≈ 2.005 ms
- 断言: `qAbs(result - 2.005) < 0.01`

**T03 — T3.5 计算：115200 baud 固定值**
- 前置条件: 无
- 输入: `calculateT35(115200, 8, false, 1.0)`
- 预期: 固定 1.75 ms（波特率 > 19200）
- 断言: `result == 1.75`

**T04 — CRC16 已知数据校验**
- 前置条件: 无
- 输入: `calculateCRC16(QByteArray::fromHex("0103000A0001"))`
- 预期: CRC16 值与 Modbus 标准一致；附加 CRC 后整帧校验为 0
- 断言: 整帧 CRC16 == 0

**T05 — Handler start_server：测试环境无串口**
- 前置条件: 构造 Handler
- 输入: `handle("start_server", {"port_name":"COM_TEST","baud_rate":9600}, resp)`
- 预期: 测试环境无 COM_TEST 串口，预期返回 error code 1
- 断言: `resp.code == 1`; message contains "COM_TEST"

**T06 — Handler stop_server 未启动（独立用例）**
- 前置条件: 构造 Handler，不调用 start_server
- 输入: `handle("stop_server", {}, resp)`
- 预期: `error(3, {"message":"Server not running"})`
- 断言: `resp.code == 3`

**T07 — Handler add_unit + set/get holding register**
- 前置条件: 构造 Handler
- 输入: add_unit(1) → set_holding_register(1, 0, 1234) → get_holding_register(1, 0)
- 预期: get 返回 `{"value":1234}`
- 断言: `resp.data["value"] == 1234`

**T08 — Handler unit_id 不存在**
- 前置条件: 不添加任何 Unit
- 输入: `handle("get_coil", {"unit_id":99,"address":0}, resp)`
- 预期: `error(3, ...)`
- 断言: `resp.code == 3`

**T09 — Handler status 未启动**
- 前置条件: 构造 Handler，不调用 start_server
- 输入: `handle("status", {}, resp)`
- 预期: `done(0, {"status":"ready","listening":false,"port_name":"","units":[]})`
- 断言: `resp.data["listening"] == false`

**T10 — Handler stop_server 未启动**
- 前置条件: 构造 Handler，不调用 start_server
- 输入: `handle("stop_server", {}, resp)`
- 预期: `error(3, {"message":"Server not running"})`
- 断言: `resp.code == 3`

#### 测试代码

```cpp
#include <gtest/gtest.h>
#include <QByteArray>
#include <QCoreApplication>
#include <QJsonObject>
#include "modbus_rtu_serial_server.h"
#include "handler.h"

// T3.5 计算测试
TEST(ModbusRtuSerialServerT35, T01_9600_8N1) {
    double t35 = ModbusRtuSerialServer::calculateT35(9600, 8, false, 1.0);
    EXPECT_NEAR(t35, 3.646, 0.01);
}

TEST(ModbusRtuSerialServerT35, T02_19200_8E1) {
    double t35 = ModbusRtuSerialServer::calculateT35(19200, 8, true, 1.0);
    // 3.5 × 11 / 19200 × 1000 ≈ 2.005
    EXPECT_NEAR(t35, 2.005, 0.01);
}

TEST(ModbusRtuSerialServerT35, T03_115200_Fixed) {
    double t35 = ModbusRtuSerialServer::calculateT35(115200, 8, false, 1.0);
    EXPECT_DOUBLE_EQ(t35, 1.75);
}

// CRC16 测试
TEST(ModbusRtuSerialServerCRC, T04_KnownData) {
    QByteArray data = QByteArray::fromHex("0103000A0001");
    uint16_t crc = ModbusRtuSerialServer::calculateCRC16(data);
    EXPECT_NE(crc, 0);
    // 附加 CRC 后整帧校验应为 0
    QByteArray frame = data;
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    EXPECT_EQ(ModbusRtuSerialServer::calculateCRC16(frame), 0);
}

// Handler 命令测试（复用 MockResponder）
TEST_F(ModbusRtuSerialServerHandlerTest, T05_StartServerNoSerialPort) {
    handler.handle("start_server",
        QJsonObject{{"port_name","COM_TEST"},{"baud_rate",9600}}, resp);
    EXPECT_EQ(resp.lastCode, 1);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("COM_TEST"));
}

TEST_F(ModbusRtuSerialServerHandlerTest, T06_StopServerNotRunning) {
    handler.handle("stop_server", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 3);
}

TEST_F(ModbusRtuSerialServerHandlerTest, T07_SetGetHoldingRegister) {
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

TEST_F(ModbusRtuSerialServerHandlerTest, T08_UnitNotFound) {
    handler.handle("get_coil",
        QJsonObject{{"unit_id",99},{"address",0}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
}

TEST_F(ModbusRtuSerialServerHandlerTest, T09_StatusNotStarted) {
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["listening"].toBool(), false);
}

TEST_F(ModbusRtuSerialServerHandlerTest, T10_StopServerNotRunning) {
    handler.handle("stop_server", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 3);
}
```

### 6.2 集成测试

- 启动 `stdio.drv.modbusrtu_serial_server` 进程，验证 `--export-meta` 输出合法 JSON
- 通过虚拟串口对（如 com0com）连接 RTU 主站驱动，验证 CRC16 校验和读写操作
- 验证 T3.5 帧间隔检测：不同波特率下帧边界识别正确
- 验证广播地址（Unit ID=0）仅处理写操作且不发送响应

### 6.3 验收标准

- [ ] `build.bat` 成功编译，输出 `stdio.drv.modbusrtu_serial_server.exe`，无编译警告
- [ ] `--export-meta` 输出合法 JSON，包含 16 个命令定义
- [ ] T3.5 计算正确（T01, T02, T03）
- [ ] CRC16 算法正确（T04）
- [ ] 16 个命令正常工作（T05–T10）
- [ ] Unit ID 不匹配时静默不响应（RTU 标准）
- [ ] 广播地址仅处理写操作，不发送响应
- [ ] KeepAlive 模式下 data_written 事件推送正常
- [ ] 全量既有测试无回归

## 7. 风险与控制

- 风险: T3.5 定时器精度受操作系统调度影响，QTimer 最小精度约 1ms
  - 控制: 对 T3.5 < 1ms 的情况（高波特率），使用 `qMax(1, ...)` 确保定时器至少 1ms
  - 控制: Modbus 标准对 > 19200 baud 固定 1.75ms，已在 QTimer 精度范围内

- 风险: 测试环境无真实串口，无法验证 QSerialPort 实际通信
  - 控制: 单元测试聚焦 T3.5 计算和 Handler 命令分发（不依赖真实串口）
  - 控制: 集成测试使用虚拟串口对（com0com / socat）

- 风险: CRC16 查找表从 `modbus_rtu_client.cpp` 复制，后续维护可能不同步
  - 控制: CRC16 算法是 Modbus 标准定义，不会变更
  - 测试覆盖: T04

- 风险: 串口意外断开（USB 拔出）时 QSerialPort 行为不确定
  - 控制: `onReadyRead()` 中检查 `m_serial->isOpen()` 状态
  - 控制: `stopServer()` 安全清理资源

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] `stdio.drv.modbusrtu_serial_server.exe` 可正常编译和运行
- [ ] T3.5 帧间隔计算正确（覆盖 ≤19200 和 >19200 两种场景）
- [ ] RTU 帧 CRC16 校验正确
- [ ] 16 个命令全部实现，元数据导出正确
- [ ] 广播地址（Unit ID=0）仅处理写操作，不发送响应
- [ ] Unit ID 不匹配时静默不响应
- [ ] KeepAlive 模式下 data_written 事件推送正常
- [ ] 单元测试全部通过（T01–T10）
- [ ] 全量既有测试无回归
- [ ] 文档同步完成
- [ ] 向后兼容确认（不修改任何现有功能代码（构建配置变更除外））
