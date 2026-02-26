# 里程碑 82：Modbus RTU 串口主站驱动

> **前置条件**: M81 (Modbus RTU 串口从站驱动) T3.5 算法、`driver_modbusrtu/` CRC16 + RTU 帧构建 + 响应解析 + `modbus_types.h/cpp`
> **目标**: 实现 `driver_modbusrtu_serial` 主站驱动，通过 QSerialPort 直连 Modbus RTU 从站，复用现有 RTU 帧逻辑，传输层从 QTcpSocket 改为 QSerialPort

## 1. 目标

- 新增 `driver_modbusrtu_serial` 驱动，输出可执行文件 `stdio.drv.modbusrtu_serial`
- 传输层从 `QTcpSocket` 改为 `QSerialPort`，连接参数从 host/port 改为串口参数
- 实现 T3.5 帧间隔：发送前静默 + 响应接收帧边界检测
- 复用 `driver_modbusrtu/` 的 CRC16 算法、RTU 帧构建、响应解析、`modbus_types.h/cpp`
- 命令集与 `driver_modbusrtu` 一致（10 个命令），连接参数改为串口参数
- 连接池以 `portName` 为唯一键，同一串口不同参数视为配置冲突
- 单元测试覆盖 T3.5 计算、串口参数校验、Handler 命令分发

## 2. 背景与问题

- 现有 `driver_modbusrtu` 通过 TCP 网关（如 Moxa NPort）与 RTU 从站通信
- 工控现场常需 PC 通过 USB-RS485 转换器直连从站，无 TCP 网关可用
- 串口通信与 TCP 的核心差异：QSerialPort 替代 QTcpSocket、T3.5 帧间隔、串口参数配置
- 本驱动复用 `driver_modbusrtu` 的 RTU 帧逻辑，仅替换传输层和连接管理

**范围**:
- `ModbusRtuSerialClient` 类：基于 `QSerialPort`，T3.5 帧间隔 + CRC16
- 串口连接参数：port_name、baud_rate、data_bits、stop_bits、parity
- `ConnectionManager` 适配：连接池以 `portName` 为唯一键
- `main.cpp`：Handler + DriverCore 集成
- 单元测试：T3.5 计算、命令分发

**非目标**:
- 不修改 `driver_modbusrtu` 的代码
- 不实现串口自动发现
- 不支持同一串口并发访问（串口独占）

## 3. 技术要点

### 3.1 与 `driver_modbusrtu` (RTU Over TCP) 的核心差异

| 方面 | RTU Over TCP (现有) | RTU Serial (本里程碑) |
|------|---------------------|----------------------|
| 传输层 | `QTcpSocket` | `QSerialPort` |
| 连接参数 | host、port | port_name、baud_rate、data_bits、stop_bits、parity |
| 帧间隔 | 无（TCP 流） | T3.5 静默间隔（发送前 + 响应接收） |
| 连接池 Key | `{host, port}` | `portName`（同一串口不同参数视为冲突） |
| CMake 依赖 | `Qt::Network` | `Qt::SerialPort` |
| 响应超时 | `waitForReadyRead(timeout)` | T3.5 定时器判定帧结束 + 总超时 |

### 3.2 T3.5 帧间隔（主站侧）

主站侧 T3.5 用于两个场景：

1. **发送前静默**：发送请求帧前等待 T3.5，确保从站识别新帧边界
2. **响应接收**：收到数据后启动 T3.5 定时器，超时前收到新数据则重置，超时后视为帧完整

```cpp
// 发送请求的完整流程
QByteArray ModbusRtuSerialClient::sendRequest(const QByteArray& request, int timeout) {
    // 1. 发送前等待 T3.5 静默
    QThread::usleep(static_cast<unsigned long>(m_t35Ms * 1000));

    // 2. 发送请求帧
    m_serial->write(request);
    m_serial->flush();

    // 3. 等待响应，使用 T3.5 判定帧结束
    QByteArray response;
    QElapsedTimer totalTimer;
    totalTimer.start();

    while (totalTimer.elapsed() < timeout) {
        if (m_serial->waitForReadyRead(qMax(1, static_cast<int>(qCeil(m_t35Ms))))) {
            response.append(m_serial->readAll());
            // 继续等待，直到 T3.5 超时无新数据
            while (m_serial->waitForReadyRead(qMax(1, static_cast<int>(qCeil(m_t35Ms))))) {
                response.append(m_serial->readAll());
            }
            break; // T3.5 超时，帧接收完成
        }
    }
    return response; // 空则超时
}
```

T3.5 计算公式同 M81（复用 `calculateT35` 静态方法）。

### 3.3 连接池适配

```cpp
// 连接池以 portName 为唯一键
// 同一串口不同参数视为配置冲突，返回错误
class SerialConnectionManager {
public:
    static SerialConnectionManager& instance();
    // 返回 nullptr 时通过 errorMsg 区分：打开失败 vs 参数冲突
    ModbusRtuSerialClient* getConnection(
        const QString& portName, int baudRate, int dataBits,
        const QString& stopBits, const QString& parity,
        QString& errorMsg);
private:
    struct ConnectionInfo {
        ModbusRtuSerialClient* client;
        int baudRate;
        int dataBits;
        QString stopBits;
        QString parity;
    };
    QMap<QString, ConnectionInfo> m_connections; // Key: portName
};
```

### 3.4 错误码策略

| 错误码 | 来源 | 含义 |
|--------|------|------|
| 0 | 驱动 | 成功 |
| 1 | 驱动 | 串口打开/连接失败 |
| 2 | 驱动 | Modbus 通讯错误（超时、CRC 失败、异常码） |
| 3 | 驱动 | 业务逻辑校验失败 |
| 400 | 框架 | 元数据 Schema 校验失败 |
| 404 | 框架 | 未知命令 |

### 3.5 向后兼容

- 新增独立驱动，不修改任何现有功能代码（构建配置变更除外）
- 复用 `modbus_types.h/cpp` 通过 CMake `target_include_directories` 引用

## 4. 实现步骤

### 4.1 新增 `modbus_rtu_serial_client.h` — 串口主站类声明

- 新增 `src/drivers/driver_modbusrtu_serial/modbus_rtu_serial_client.h`：
  - 复用 `ModbusResult` 结构体（同 `driver_modbusrtu`）
  - `ModbusRtuSerialClient` 类基于 `QSerialPort`：
    ```cpp
    #pragma once
    #include <QByteArray>
    #include <QSerialPort>
    #include <QString>
    #include <QVector>

    struct ModbusResult {
        bool success = false;
        quint8 exceptionCode = 0;
        QString errorMessage;
        QVector<bool> coils;
        QVector<quint16> registers;
    };

    class ModbusRtuSerialClient {
    public:
        ModbusRtuSerialClient();
        ~ModbusRtuSerialClient();

        bool open(const QString& portName, int baudRate = 9600,
                  int dataBits = 8, const QString& stopBits = "1",
                  const QString& parity = "none");
        void close();
        bool isOpen() const;

        // 8 个功能码方法（签名同 ModbusRtuClient，去掉 host/port 参数）
        ModbusResult readCoils(quint8 unitId, quint16 address,
                               quint16 count, int timeout = 3000);
        ModbusResult readDiscreteInputs(quint8 unitId, quint16 address,
                                         quint16 count, int timeout = 3000);
        ModbusResult readHoldingRegisters(quint8 unitId, quint16 address,
                                           quint16 count, int timeout = 3000);
        ModbusResult readInputRegisters(quint8 unitId, quint16 address,
                                         quint16 count, int timeout = 3000);
        ModbusResult writeSingleCoil(quint8 unitId, quint16 address,
                                      bool value, int timeout = 3000);
        ModbusResult writeMultipleCoils(quint8 unitId, quint16 address,
                                         const QVector<bool>& values, int timeout = 3000);
        ModbusResult writeSingleRegister(quint8 unitId, quint16 address,
                                          quint16 value, int timeout = 3000);
        ModbusResult writeMultipleRegisters(quint8 unitId, quint16 address,
                                             const QVector<quint16>& values, int timeout = 3000);

    public:
        static uint16_t calculateCRC16(const QByteArray& data);
        static double calculateT35(int baudRate, int dataBits,
                                    bool hasParity, double stopBits);

    private:
        QByteArray buildRequest(quint8 unitId, quint8 fc, const QByteArray& pdu);
        QByteArray sendRequest(const QByteArray& request, int timeout);
        ModbusResult parseResponse(const QByteArray& response, quint8 expectedFc);

        QSerialPort* m_serial = nullptr;
        double m_t35Ms = 3.646;
    };
    ```
  - 理由：接口与 `ModbusRtuClient` 一致，仅连接方式从 TCP 改为串口

### 4.2 新增 `modbus_rtu_serial_client.cpp` — 串口通信与 T3.5 帧间隔

- 新增 `src/drivers/driver_modbusrtu_serial/modbus_rtu_serial_client.cpp`：
  - CRC16 静态查找表（复制自 `driver_modbusrtu/modbus_rtu_client.cpp`）
  - `open()` 打开串口并计算 T3.5：
    ```cpp
    bool ModbusRtuSerialClient::open(const QString& portName, int baudRate,
            int dataBits, const QString& stopBits, const QString& parity) {
        if (m_serial) return false;
        m_serial = new QSerialPort();
        m_serial->setPortName(portName);
        m_serial->setBaudRate(baudRate);
        m_serial->setDataBits(static_cast<QSerialPort::DataBits>(dataBits));
        // 设置 stopBits、parity（字符串转枚举）...
        if (!m_serial->open(QIODevice::ReadWrite)) {
            delete m_serial; m_serial = nullptr;
            return false;
        }
        bool hasParity = (parity != "none");
        double stopBitsVal = (stopBits == "1.5") ? 1.5 : stopBits.toDouble();
        m_t35Ms = calculateT35(baudRate, dataBits, hasParity, stopBitsVal);
        return true;
    }
    ```
  - `sendRequest()` 实现 T3.5 发送前静默 + 响应接收（见 §3.2 伪代码）
  - `buildRequest()` 构建 RTU 帧：UnitID + FC + PDU + CRC16（逻辑同 `ModbusRtuClient`）
  - `parseResponse()` 解析响应帧：CRC16 校验 → 异常码检测 → 数据提取（逻辑同 `ModbusRtuClient`）
  - 8 个功能码方法的实现逻辑与 `ModbusRtuClient` 完全相同，仅调用 `sendRequest()` 替代 TCP 发送
  - 理由：RTU 帧格式和解析逻辑不变，仅传输层替换

### 4.3 新增 `handler.h` — Handler 类声明（含 SerialConnectionManager）

- 新增 `src/drivers/driver_modbusrtu_serial/handler.h`：
  - `SerialConnectionManager` 管理串口连接池：
    ```cpp
    class SerialConnectionManager {
    public:
        static SerialConnectionManager& instance();
        // 返回 nullptr 时通过 errorMsg 区分：打开失败 vs 参数冲突
        ModbusRtuSerialClient* getConnection(
            const QString& portName, int baudRate, int dataBits,
            const QString& stopBits, const QString& parity,
            QString& errorMsg);
    private:
        struct ConnectionInfo {
            ModbusRtuSerialClient* client;
            int baudRate;
            int dataBits;
            QString stopBits;
            QString parity;
        };
        QMap<QString, ConnectionInfo> m_connections; // Key: portName
    };
    ```
  - `ModbusRtuSerialHandler` 类声明，实现 `IMetaCommandHandler`
  - `handle()` 与 `buildMeta()` 方法声明

### 4.4 新增 `handler.cpp` — Handler 实现

- 新增 `src/drivers/driver_modbusrtu_serial/handler.cpp`：
  - `handle()` 方法分发 10 个命令，连接参数从 JSON 中提取串口参数：
    ```cpp
    void ModbusRtuSerialHandler::handle(const QString& cmd,
            const QJsonValue& data, IResponder& resp) {
        QJsonObject p = data.toObject();
        if (cmd == "status") {
            resp.done(0, QJsonObject{{"status", "ready"}});
            return;
        }
        // 提取串口连接参数
        QString portName = p["port_name"].toString();
        int baudRate = p["baud_rate"].toInt(9600);
        int dataBits = p["data_bits"].toInt(8);
        QString stopBits = p["stop_bits"].toString("1");
        QString parity = p["parity"].toString("none");
        int unitId = p["unit_id"].toInt(1);
        int timeout = p["timeout"].toInt(3000);

        QString errorMsg;
        auto* client = SerialConnectionManager::instance()
            .getConnection(portName, baudRate, dataBits, stopBits, parity, errorMsg);
        if (!client) {
            resp.error(1, QJsonObject{{"message", errorMsg}});
            return;
        }
        // 分发到具体命令...
    }
    ```
  - `buildMeta()` 使用 DriverMetaBuilder 定义 10 个命令，每个命令包含串口连接参数字段
  - 理由：命令集与 `driver_modbusrtu` 完全一致，仅连接参数不同

### 4.5 新增 `main.cpp` — 入口函数

- 新增 `src/drivers/driver_modbusrtu_serial/main.cpp`：
  - `main()` 函数（OneShot 模式，与 `driver_modbusrtu` 一致）：
    ```cpp
    int main(int argc, char* argv[]) {
        QCoreApplication app(argc, argv);
        ModbusRtuSerialHandler handler;
        DriverCore core;
        core.setMetaHandler(&handler);
        return core.run(argc, argv);
    }
    ```

### 4.6 新增 `CMakeLists.txt` — 构建配置

- 新增 `src/drivers/driver_modbusrtu_serial/CMakeLists.txt`：
    ```cmake
    cmake_minimum_required(VERSION 3.16)
    project(driver_modbusrtu_serial)
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

    add_executable(driver_modbusrtu_serial
        main.cpp
        handler.cpp
        modbus_rtu_serial_client.cpp
        ${MODBUSRTU_DIR}/modbus_types.cpp
    )
    target_include_directories(driver_modbusrtu_serial PRIVATE
        ${MODBUSRTU_DIR}
    )
    target_link_libraries(driver_modbusrtu_serial PRIVATE
        stdiolink ${QT_LIBRARIES}
    )
    set_target_properties(driver_modbusrtu_serial PROPERTIES
        OUTPUT_NAME "stdio.drv.modbusrtu_serial"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    ```

### 4.7 修改 `src/drivers/CMakeLists.txt` — 注册新驱动

- 修改 `src/drivers/CMakeLists.txt`：
  - 新增一行：`add_subdirectory(driver_modbusrtu_serial)`

## 5. 文件变更清单

### 5.1 新增文件
- `src/drivers/driver_modbusrtu_serial/CMakeLists.txt` — 构建配置
- `src/drivers/driver_modbusrtu_serial/modbus_rtu_serial_client.h` — 串口主站类声明
- `src/drivers/driver_modbusrtu_serial/modbus_rtu_serial_client.cpp` — T3.5 + CRC16 + 8 个功能码
- `src/drivers/driver_modbusrtu_serial/handler.h` — Handler 类声明（含 SerialConnectionManager）
- `src/drivers/driver_modbusrtu_serial/handler.cpp` — Handler 实现
- `src/drivers/driver_modbusrtu_serial/main.cpp` — 入口函数

### 5.2 修改文件
- `src/drivers/CMakeLists.txt` — 新增 `add_subdirectory(driver_modbusrtu_serial)`
- `src/tests/CMakeLists.txt` — 新增测试文件

### 5.3 测试文件
- `src/tests/test_modbusrtu_serial.cpp` — T3.5 计算 + CRC16 + Handler 命令分发测试

测试构建方式：在 `src/tests/CMakeLists.txt` 中新增独立测试目标 `test_modbusrtu_serial`，编译 `handler.cpp`、`modbus_rtu_serial_client.cpp`、`modbus_types.cpp` 及测试文件，链接 `stdiolink`、`GTest::gtest`、`Qt6::Core`、`Qt6::SerialPort`。不修改 `stdiolink_tests` 主测试目标。

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: T3.5 计算、CRC16 算法、Handler 命令分发
- 用例分层: T3.5 计算（T01–T03）、CRC16（T04–T05）、命令分发（T06–T10）
- 断言要点: T3.5 值精度、CRC16 正确性、响应 code/data
- 桩替身策略: `MockResponder` 同 M79；串口操作通过 Handler 层测试
- 测试策略说明: 单元测试直接调用 Handler::handle()，绕过 DriverCore 的自动参数校验（code 400）。单测聚焦 Handler 业务逻辑；框架层校验由 DriverCore 既有测试覆盖
- 测试文件: `src/tests/test_modbusrtu_serial.cpp`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `calculateT35`: 9600 baud, 8N1 | 返回 ≈3.646 ms | T01 |
| `calculateT35`: 19200 baud, 8E1 | 按公式计算 | T02 |
| `calculateT35`: 115200 baud | 返回固定 1.75 ms | T03 |
| `calculateCRC16`: 已知数据 | CRC16 值正确 | T04 |
| `calculateCRC16`: 空数据 | 返回 0xFFFF | T05 |
| Handler: status | 返回 ready | T06 |
| Handler: read_holding_registers 参数解析 | 测试环境无 COM_TEST 串口，预期返回 error code 1，message 包含 'COM_TEST' | T07 |
| Handler: write_holding_registers 类型转换 | 测试环境无 COM_TEST 串口，预期返回 error code 1，message 包含 'COM_TEST' | T08 |
| Handler: count 非类型整数倍 | 返回 error code 3 | T09 |
| Handler: unit_id 越界 | 返回 error code 3 | T10 |

#### 用例详情

**T01 — T3.5 计算：9600 baud, 8N1**
- 输入: `calculateT35(9600, 8, false, 1.0)`
- 预期: 3.5 × 10 / 9600 × 1000 ≈ 3.646 ms
- 断言: `qAbs(result - 3.646) < 0.01`

**T02 — T3.5 计算：19200 baud, 8E1**
- 输入: `calculateT35(19200, 8, true, 1.0)`
- 预期: 3.5 × 11 / 19200 × 1000 ≈ 2.005 ms
- 断言: `qAbs(result - 2.005) < 0.01`

**T03 — T3.5 计算：115200 baud 固定值**
- 输入: `calculateT35(115200, 8, false, 1.0)`
- 预期: 固定 1.75 ms
- 断言: `result == 1.75`

**T04 — CRC16 已知数据校验**
- 输入: `calculateCRC16(QByteArray::fromHex("0103000A0001"))`
- 预期: 附加 CRC 后整帧校验为 0
- 断言: 整帧 CRC16 == 0

**T05 — CRC16 空数据**
- 输入: `calculateCRC16(QByteArray())`
- 预期: 返回 0xFFFF
- 断言: 返回值 == 0xFFFF

**T06 — Handler status 命令**
- 前置条件: 构造 Handler
- 输入: `handle("status", {}, resp)`
- 预期: `done(0, {"status":"ready"})`
- 断言: `resp.code == 0`; `resp.data["status"] == "ready"`

**T07 — Handler read_holding_registers 参数解析**
- 前置条件: 构造 Handler
- 输入: `handle("read_holding_registers", {"port_name":"COM_TEST","baud_rate":9600,"address":0,"count":1}, resp)`
- 预期: 测试环境无 COM_TEST 串口，预期返回 error code 1，message 包含 'COM_TEST'
- 断言: `resp.code == 1`; message 包含 "COM_TEST"

**T08 — Handler write_holding_registers 类型转换参数**
- 前置条件: 构造 Handler
- 输入: `handle("write_holding_registers", {"port_name":"COM_TEST","address":0,"value":50.0,"data_type":"float32","byte_order":"big_endian"}, resp)`
- 预期: 测试环境无 COM_TEST 串口，预期返回 error code 1，message 包含 'COM_TEST'
- 断言: `resp.code == 1`; message 包含 "COM_TEST"

**T09 — Handler count 非类型整数倍**
- 前置条件: 构造 Handler
- 输入: `handle("read_holding_registers", {"port_name":"COM_TEST","address":0,"count":3,"data_type":"float32"}, resp)`
- 预期: `error(3, ...)` count=3 不是 float32 所需 2 寄存器的整数倍
- 断言: `resp.code == 3`

**T10 — Handler unit_id 越界**
- 前置条件: 构造 Handler
- 输入: `handle("read_coils", {"port_name":"COM_TEST","unit_id":0,"address":0}, resp)`
- 预期: `error(3, ...)` unit_id 超出 1–247 范围
- 断言: `resp.code == 3`

#### 测试代码

```cpp
#include <gtest/gtest.h>
#include <QByteArray>
#include <QJsonObject>
#include "modbus_rtu_serial_client.h"

// T3.5 计算测试
TEST(ModbusRtuSerialClientT35, T01_9600_8N1) {
    double t35 = ModbusRtuSerialClient::calculateT35(9600, 8, false, 1.0);
    EXPECT_NEAR(t35, 3.646, 0.01);
}

TEST(ModbusRtuSerialClientT35, T02_19200_8E1) {
    double t35 = ModbusRtuSerialClient::calculateT35(19200, 8, true, 1.0);
    EXPECT_NEAR(t35, 2.005, 0.01);
}

TEST(ModbusRtuSerialClientT35, T03_115200_Fixed) {
    double t35 = ModbusRtuSerialClient::calculateT35(115200, 8, false, 1.0);
    EXPECT_DOUBLE_EQ(t35, 1.75);
}

// CRC16 测试
TEST(ModbusRtuSerialClientCRC, T04_KnownData) {
    QByteArray data = QByteArray::fromHex("0103000A0001");
    uint16_t crc = ModbusRtuSerialClient::calculateCRC16(data);
    QByteArray frame = data;
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    EXPECT_EQ(ModbusRtuSerialClient::calculateCRC16(frame), 0);
}

TEST(ModbusRtuSerialClientCRC, T05_EmptyData) {
    EXPECT_EQ(ModbusRtuSerialClient::calculateCRC16(QByteArray()), 0xFFFF);
}

// Handler 命令测试
TEST_F(ModbusRtuSerialHandlerTest, T06_Status) {
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["status"].toString(), "ready");
}

TEST_F(ModbusRtuSerialHandlerTest, T09_CountMismatch) {
    handler.handle("read_holding_registers", QJsonObject{
        {"port_name","COM_TEST"},{"address",0},{"count",3},
        {"data_type","float32"}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
}
```

### 6.2 集成测试

- 启动 `stdio.drv.modbusrtu_serial` 进程，验证 `--export-meta` 输出合法 JSON
- 通过虚拟串口对连接 M81 的串口从站驱动，验证端到端读写操作
- 验证 T3.5 帧间隔：不同波特率下帧边界识别正确
- 验证连接池：同一串口复用连接，同一串口不同参数返回冲突错误

### 6.3 验收标准

- [ ] `build.bat Release` 成功编译，输出 `stdio.drv.modbusrtu_serial.exe`，无编译警告
- [ ] `--export-meta` 输出合法 JSON，包含 10 个命令定义
- [ ] T3.5 计算正确（T01, T02, T03）
- [ ] CRC16 算法正确（T04, T05）
- [ ] 串口参数（port_name、baud_rate、parity 等）枚举约束正确
- [ ] 10 个命令参数解析正确（T06–T10）
- [ ] 连接池正常工作
- [ ] 全量既有测试无回归

## 7. 风险与控制

- 风险: T3.5 定时器精度受操作系统调度影响
  - 控制: 对 > 19200 baud 固定 1.75ms，在 QTimer/usleep 精度范围内
  - 控制: 响应接收使用 `waitForReadyRead()` 阻塞等待，精度优于 QTimer

- 风险: 测试环境无真实串口，无法验证端到端通信
  - 控制: 单元测试聚焦 T3.5 计算、CRC16、Handler 参数解析
  - 控制: 集成测试使用虚拟串口对（com0com / socat）

- 风险: 串口独占导致连接池中同一串口不同参数的配置冲突
  - 控制: 连接池以 `portName` 为唯一键，同一串口不同参数返回错误而非创建新连接
  - 控制: 文档明确说明同一串口不支持并发访问

- 风险: CRC16 查找表从 `modbus_rtu_client.cpp` 复制
  - 控制: CRC16 算法是 Modbus 标准定义，不会变更
  - 测试覆盖: T04, T05

## 8. 里程碑完成定义（DoD）

- [ ] 代码与测试完成
- [ ] `stdio.drv.modbusrtu_serial.exe` 可正常编译和运行
- [ ] T3.5 帧间隔计算正确
- [ ] CRC16 校验正确
- [ ] 10 个命令全部实现，元数据导出正确
- [ ] 串口连接池正常工作
- [ ] 单元测试全部通过（T01–T10）
- [ ] 全量既有测试无回归
- [ ] 文档同步完成
- [ ] 向后兼容确认（不修改任何现有功能代码（构建配置变更除外））
