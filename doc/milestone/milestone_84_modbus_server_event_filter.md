# 里程碑 84：Modbus Server 驱动可配置事件过滤

> **前置条件**: M79 (ModbusTCP Server)、M80 (ModbusRTU Server)、M81 (ModbusRTU Serial Server)
> **目标**: 为三个 Modbus 从站驱动添加 `event_mode` 参数，支持按需输出读/写数据事件，默认仅输出写事件（向后兼容）

## 1. 目标

- 三个从站 Server 类新增 `dataRead` 信号，在主站读取数据时触发（FC 0x01–0x04）
- `start_server` 命令新增 `event_mode` 参数，控制推送哪些数据事件
- 默认值 `"write"` 保持向后兼容，不影响现有行为
- `client_connected` / `client_disconnected` 事件不受 `event_mode` 影响，始终推送
- 单元测试验证 `event_mode` 参数接受与 `status` 返回；集成测试覆盖三个驱动的事件过滤行为

## 2. 背景与问题

- 当前三个从站驱动仅在主站写入时推送 `data_written` 事件，读操作无任何事件输出
- 在 DriverLab 调试场景中，用户需要观察主站的读取行为（如轮询频率、读取范围），但无法获取读事件
- 高频读取场景下无条件推送读事件会产生大量 JSONL 输出，影响性能，因此需要可配置的过滤机制

**范围**:
- 三个 Server 类（`ModbusTcpServer`、`ModbusRtuServer`、`ModbusRtuSerialServer`）新增 `dataRead` 信号
- 三个 Handler 类新增 `m_eventMode` 成员与条件过滤逻辑
- `start_server` 命令 meta 新增 `event_mode` 参数
- 单元测试验证参数接受与 `status` 返回；集成测试验证事件过滤行为

**非目标**:
- 不修改 `client_connected` / `client_disconnected` 事件的推送逻辑
- 不新增独立的 `set_event_mode` 命令（通过 `start_server` 参数一次性配置）
- 不修改 stdiolink 核心库或 DriverCore 框架
- 不修改现有主站驱动代码

## 3. 技术要点

### 3.1 `event_mode` 参数设计

`start_server` 命令新增 `event_mode` 枚举参数：

| 值 | 推送 `data_written` | 推送 `data_read` | 说明 |
|----|---------------------|-------------------|------|
| `"write"` (默认) | 是 | 否 | 向后兼容，与当前行为一致 |
| `"all"` | 是 | 是 | 调试模式，推送所有数据事件 |
| `"read"` | 否 | 是 | 仅关注读取行为 |
| `"none"` | 否 | 否 | 静默模式，不推送数据事件 |

### 3.2 `dataRead` 信号与事件格式

新增信号签名与 `dataWritten` 完全一致：

```cpp
signals:
    void dataRead(quint8 unitId, quint8 functionCode,
                  quint16 address, quint16 quantity);
```

事件输出格式：
```json
{"status":"event","code":0,"data":{"event":"data_read","data":{"unit_id":1,"function_code":1,"address":0,"quantity":10}}}
```

### 3.3 运行时过滤策略

`connectEvents()` 在构造函数中调用（早于 `start_server`），因此信号始终连接，在 lambda 内部通过 `m_eventMode` 做运行时过滤：

```cpp
// handler.h 新增成员
QString m_eventMode = "write";

// connectEvents() 中的条件过滤
QObject::connect(&m_server, &ModbusTcpServer::dataWritten,
        [this](quint8 unitId, quint8 fc, quint16 addr, quint16 qty) {
    if (m_eventMode == "none" || m_eventMode == "read") return;
    m_eventResponder.event("data_written", 0, QJsonObject{
        {"unit_id", unitId}, {"function_code", fc},
        {"address", addr}, {"quantity", qty}});
});

QObject::connect(&m_server, &ModbusTcpServer::dataRead,
        [this](quint8 unitId, quint8 fc, quint16 addr, quint16 qty) {
    if (m_eventMode == "none" || m_eventMode == "write") return;
    m_eventResponder.event("data_read", 0, QJsonObject{
        {"unit_id", unitId}, {"function_code", fc},
        {"address", addr}, {"quantity", qty}});
});
```

### 3.4 向后兼容

- `m_eventMode` 默认值为 `"write"`，不传参时行为与当前完全一致
- `stop_server` 后再次 `start_server` 可切换 `event_mode`，无需断开/重连信号
- 现有测试无需修改（默认模式下 `data_read` 不推送）

## 4. 实现步骤

### 4.1 ModbusTcpServer — 新增 `dataRead` 信号

- 修改 `src/drivers/driver_modbustcp_server/modbus_tcp_server.h`：
  - 在 `signals:` 区新增 `dataRead` 信号：
    ```cpp
    signals:
        void clientConnected(QString address, quint16 port);
        void clientDisconnected(QString address, quint16 port);
        void dataWritten(quint8 unitId, quint8 functionCode,
                         quint16 address, quint16 quantity);
        void dataRead(quint8 unitId, quint8 functionCode,
                      quint16 address, quint16 quantity);  // 新增
    ```

- 修改 `src/drivers/driver_modbustcp_server/modbus_tcp_server.cpp`：
  - 在 4 个 `handleRead*()` 方法中，先 `locker.unlock()` 释放互斥锁，再 `emit dataRead()`，最后 `return response`：
    ```cpp
    // handleReadCoils() — 约 L272
    QByteArray ModbusTcpServer::handleReadCoils(..., quint16 startAddress, quint16 quantity) {
        QMutexLocker locker(&m_mutex);
        // ... 校验 + 读取数据构建 response ...
        locker.unlock();  // 先释放锁
        emit dataRead(header.unitId, READ_COILS, startAddress, quantity);  // 锁外 emit
        return response;
    }

    // handleReadDiscreteInputs() — 同理
    locker.unlock();
    emit dataRead(header.unitId, READ_DISCRETE_INPUTS, startAddress, quantity);

    // handleReadHoldingRegisters() — 同理
    locker.unlock();
    emit dataRead(header.unitId, READ_HOLDING_REGISTERS, startAddress, quantity);

    // handleReadInputRegisters() — 同理
    locker.unlock();
    emit dataRead(header.unitId, READ_INPUT_REGISTERS, startAddress, quantity);
    ```
  - 理由：与写分支（`handleWrite*()` 已有 `locker.unlock()` 后 emit）保持一致，避免锁内触发信号导致阻塞或潜在重入问题

### 4.2 ModbusRtuServer — 新增 `dataRead` 信号

- 修改 `src/drivers/driver_modbusrtu_server/modbus_rtu_server.h`：
  - 在 `signals:` 区新增 `dataRead` 信号（签名同 4.1）：
    ```cpp
    signals:
        void clientConnected(QString address, quint16 port);
        void clientDisconnected(QString address, quint16 port);
        void dataWritten(quint8 unitId, quint8 functionCode,
                         quint16 address, quint16 quantity);
        void dataRead(quint8 unitId, quint8 functionCode,
                      quint16 address, quint16 quantity);  // 新增
    ```

- 修改 `src/drivers/driver_modbusrtu_server/modbus_rtu_server.cpp`：
  - 在 `processRtuRequest()` 的 4 个 read case 中，先 `locker.unlock()` 释放互斥锁，再 `emit dataRead()`：
    ```cpp
    case READ_COILS: {
        // ... 校验 + 读取数据构建 pdu ...
        locker.unlock();
        emit dataRead(unitId, fc, startAddr, qty);
        break;
    }
    case READ_DISCRETE_INPUTS: {
        // ... 同理 ...
        locker.unlock();
        emit dataRead(unitId, fc, startAddr, qty);
        break;
    }
    case READ_HOLDING_REGISTERS: {
        // ... 同理 ...
        locker.unlock();
        emit dataRead(unitId, fc, startAddr, qty);
        break;
    }
    case READ_INPUT_REGISTERS: {
        // ... 同理 ...
        locker.unlock();
        emit dataRead(unitId, fc, startAddr, qty);
        break;
    }
    ```
  - 理由：与写分支（`WRITE_SINGLE_COIL` 等已有 `locker.unlock()` 后 emit）保持一致，锁外发信号

### 4.3 ModbusRtuSerialServer — 新增 `dataRead` 信号

- 修改 `src/drivers/driver_modbusrtu_serial_server/modbus_rtu_serial_server.h`：
  - 在 `signals:` 区新增 `dataRead` 信号：
    ```cpp
    signals:
        void dataWritten(quint8 unitId, quint8 functionCode,
                         quint16 address, quint16 quantity);
        void dataRead(quint8 unitId, quint8 functionCode,
                      quint16 address, quint16 quantity);  // 新增
    ```
  - 注意：串口 Server 无 `clientConnected`/`clientDisconnected` 信号（串口独占）

- 修改 `src/drivers/driver_modbusrtu_serial_server/modbus_rtu_serial_server.cpp`：
  - 在 `processRtuRequest()` 的 4 个 read case 中，先 `locker.unlock()` 释放互斥锁，再 `emit dataRead()`：
    ```cpp
    case SFC_READ_COILS: {
        // ... 校验 + 读取数据构建 pdu ...
        locker.unlock();
        emit dataRead(unitId, fc, startAddr, qty);
        break;
    }
    // SFC_READ_DISCRETE_INPUTS、SFC_READ_HOLDING_REGISTERS、SFC_READ_INPUT_REGISTERS 同理
    ```
  - 理由：与写分支（`SFC_WRITE_SINGLE_COIL` 等已有 `locker.unlock()` 后 emit）保持一致，锁外发信号

### 4.4 ModbusTcpServerHandler — 添加 `m_eventMode` 与条件过滤

- 修改 `src/drivers/driver_modbustcp_server/handler.h`：
  - 新增成员变量：
    ```cpp
    class ModbusTcpServerHandler : public IMetaCommandHandler {
        // ... 现有成员 ...
    private:
        QString m_eventMode = "write";  // 新增
    };
    ```

- 修改 `src/drivers/driver_modbustcp_server/handler.cpp`：
  - `handle()` 中 `start_server` 分支：先校验 `event_mode` 白名单，启动成功后再赋值 `m_eventMode`：
    ```cpp
    if (cmd == "start_server") {
        if (m_server.isRunning()) { ... }

        // 校验 event_mode 白名单（新增）
        QString eventMode = p["event_mode"].toString("write");
        static const QStringList validModes = {"write", "all", "read", "none"};
        if (!validModes.contains(eventMode)) {
            resp.error(3, QJsonObject{{"message",
                QString("Invalid event_mode: %1").arg(eventMode)}});
            return;
        }

        int port = p["listen_port"].toInt(502);
        if (!m_server.startServer(static_cast<quint16>(port))) {
            resp.error(1, QJsonObject{{"message", ...}});
            return;  // 启动失败，m_eventMode 保持原值
        }
        m_eventMode = eventMode;  // 启动成功后才切换（新增）
        resp.done(0, QJsonObject{{"started", true}, {"port", m_server.serverPort()}});
        return;
    }
    ```
  - `connectEvents()` 中为 `dataWritten` 添加过滤守卫，并新增 `dataRead` 连接：
    ```cpp
    void ModbusTcpServerHandler::connectEvents() {
        // client_connected / client_disconnected 保持不变（始终推送）

        QObject::connect(&m_server, &ModbusTcpServer::dataWritten,
                [this](quint8 unitId, quint8 fc, quint16 addr, quint16 qty) {
            if (m_eventMode == "none" || m_eventMode == "read") return;  // 新增守卫
            m_eventResponder.event("data_written", 0, QJsonObject{
                {"unit_id", unitId}, {"function_code", fc},
                {"address", addr}, {"quantity", qty}});
        });

        // 新增 dataRead 连接
        QObject::connect(&m_server, &ModbusTcpServer::dataRead,
                [this](quint8 unitId, quint8 fc, quint16 addr, quint16 qty) {
            if (m_eventMode == "none" || m_eventMode == "write") return;
            m_eventResponder.event("data_read", 0, QJsonObject{
                {"unit_id", unitId}, {"function_code", fc},
                {"address", addr}, {"quantity", qty}});
        });
    }
    ```
  - `buildMeta()` 中 `start_server` 命令新增 `event_mode` 参数：
    ```cpp
    .command(CommandBuilder("start_server")
        .description("启动从站服务")
        .param(FieldBuilder("listen_port", FieldType::Int)
            .defaultValue(502).range(1, 65535)
            .description("监听端口"))
        .param(FieldBuilder("event_mode", FieldType::Enum)       // 新增
            .defaultValue("write")
            .enumValues(QStringList{"write", "all", "read", "none"})
            .description("数据事件推送模式")))
    ```
  - 理由：运行时过滤避免 `connectEvents()` 时序问题；`event_mode` 在 `start_server` 中设置，`stop_server` 后可重新配置

### 4.5 ModbusRtuServerHandler — 添加 `m_eventMode` 与条件过滤

- 修改 `src/drivers/driver_modbusrtu_server/handler.h`：
  - 新增成员变量 `QString m_eventMode = "write";`（同 4.4）

- 修改 `src/drivers/driver_modbusrtu_server/handler.cpp`：
  - `handle()` 中 `start_server` 分支：校验 `event_mode` 白名单，启动成功后赋值 `m_eventMode`（逻辑同 4.4）
  - `connectEvents()` 中：为 `dataWritten` 添加过滤守卫，新增 `dataRead` 连接（代码模式同 4.4，信号源改为 `ModbusRtuServer`）
  - `buildMeta()` 中 `start_server` 命令：新增 `event_mode` 参数（同 4.4）
  - 理由：三个 Handler 保持一致的事件过滤接口

### 4.6 ModbusRtuSerialServerHandler — 添加 `m_eventMode` 与条件过滤

- 修改 `src/drivers/driver_modbusrtu_serial_server/handler.h`：
  - 新增成员变量 `QString m_eventMode = "write";`（同 4.4）

- 修改 `src/drivers/driver_modbusrtu_serial_server/handler.cpp`：
  - `handle()` 中 `start_server` 分支：校验 `event_mode` 白名单，启动成功后赋值 `m_eventMode`（逻辑同 4.4）
  - `connectEvents()` 中：为 `dataWritten` 添加过滤守卫，新增 `dataRead` 连接（信号源为 `ModbusRtuSerialServer`）
  - 注意：串口 Server 无 `clientConnected`/`clientDisconnected` 信号，`connectEvents()` 仅包含 `dataWritten` 和 `dataRead` 两个连接
  - `buildMeta()` 中 `start_server` 命令：新增 `event_mode` 参数（同 4.4）
  - 理由：三个 Handler 保持一致的事件过滤接口

## 5. 文件变更清单

### 5.1 新增文件
- 无

### 5.2 修改文件
- `src/drivers/driver_modbustcp_server/modbus_tcp_server.h` — 新增 `dataRead` 信号声明
- `src/drivers/driver_modbustcp_server/modbus_tcp_server.cpp` — 4 个 `handleRead*()` 中 emit `dataRead`
- `src/drivers/driver_modbustcp_server/handler.h` — 新增 `m_eventMode` 成员
- `src/drivers/driver_modbustcp_server/handler.cpp` — `start_server` 读取 `event_mode`、`connectEvents()` 条件过滤、`buildMeta()` 新增参数
- `src/drivers/driver_modbusrtu_server/modbus_rtu_server.h` — 新增 `dataRead` 信号声明
- `src/drivers/driver_modbusrtu_server/modbus_rtu_server.cpp` — 4 个 read case 中 emit `dataRead`
- `src/drivers/driver_modbusrtu_server/handler.h` — 新增 `m_eventMode` 成员
- `src/drivers/driver_modbusrtu_server/handler.cpp` — 同 TCP Handler 改动
- `src/drivers/driver_modbusrtu_serial_server/modbus_rtu_serial_server.h` — 新增 `dataRead` 信号声明
- `src/drivers/driver_modbusrtu_serial_server/modbus_rtu_serial_server.cpp` — 4 个 read case 中 emit `dataRead`
- `src/drivers/driver_modbusrtu_serial_server/handler.h` — 新增 `m_eventMode` 成员
- `src/drivers/driver_modbusrtu_serial_server/handler.cpp` — 同 TCP Handler 改动

### 5.3 文档文件
- `doc/device/modbustcp_server.md` — 新增 `event_mode` 参数说明、`data_read` 事件格式
- `doc/device/modbusrtu_server.md` — 同上
- `doc/device/modbusrtu_serial_server.md` — 同上

### 5.4 测试文件
- `src/tests/test_modbustcp_server_handler.cpp` — 新增 T22–T25 事件过滤测试
- `src/tests/test_modbusrtu_server_handler.cpp` — 新增 T21–T24 事件过滤测试
- `src/tests/test_modbusrtu_serial_server.cpp` — 新增 T11–T14 事件过滤测试

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: 三个 Handler 的 `start_server` 命令对 `event_mode` 参数的接受与存储
- 用例分层: 正常路径（4 种 event_mode 值）、默认值回退、stop/restart 切换模式、非法值拒绝
- 断言要点: `start_server` 返回 code=0 且 `started=true`；`status` 返回正确的 `event_mode` 值；非法 `event_mode` 返回 code=3
- 桩替身策略: 复用现有 `MockResponder`/`RtuMockResponder`/`SerialMockResponder`
- 测试策略说明: 事件推送通过 `m_eventResponder`（StdioResponder 成员）输出到 stdout，MockResponder 无法直接捕获。单元测试聚焦于参数接受、白名单校验与状态查询；事件过滤的端到端行为由集成测试覆盖。为支持单元测试验证 `event_mode`，需在 `status` 命令的响应中新增 `event_mode` 字段。
- 测试文件: `src/tests/test_modbustcp_server_handler.cpp`、`src/tests/test_modbusrtu_server_handler.cpp`、`src/tests/test_modbusrtu_serial_server.cpp`

#### 补充改动：`status` 命令返回 `event_mode`

为支持单元测试验证 `event_mode` 已正确存储，需在三个 Handler 的 `status` 命令响应中新增 `event_mode` 字段：

```cpp
if (cmd == "status") {
    QJsonArray units;
    for (auto id : m_server.getUnits()) units.append(id);
    resp.done(0, QJsonObject{
        {"status", "ready"},
        {"listening", m_server.isRunning()},
        {"port", m_server.isRunning() ? m_server.serverPort() : 0},
        {"units", units},
        {"event_mode", m_eventMode}});  // 新增
    return;
}
```

#### 路径矩阵（ModbusTcpServerHandler — T22–T25）

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `start_server`: event_mode="write"（默认） | 启动成功，status 返回 event_mode="write" | T22 |
| `start_server`: event_mode="all" | 启动成功，status 返回 event_mode="all" | T23 |
| `start_server`: event_mode="none" | 启动成功，status 返回 event_mode="none"; stop 后以 "read" 重启，status 返回 "read" | T24 |
| `start_server`: event_mode="invalid" | 返回 code=3，服务未启动 | T25 |

#### 路径矩阵（ModbusRtuServerHandler — T21–T24）

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `start_server`: event_mode 默认值 | 启动成功，status 返回 event_mode="write" | T21 |
| `start_server`: event_mode="all" | 启动成功，status 返回 event_mode="all" | T22 |
| `start_server`: stop 后切换 event_mode | stop → start(event_mode="none") → status 返回 "none" | T23 |
| `start_server`: event_mode="invalid" | 返回 code=3，服务未启动 | T24 |

#### 用例详情（ModbusTcpServerHandler）

**T22 — start_server 默认 event_mode**
- 前置条件: 构造 Handler
- 输入: `handle("start_server", {"listen_port": 0}, resp)` 后 `handle("status", {}, resp)`
- 预期: start 返回 code=0；status 返回 `event_mode="write"`
- 断言: `resp.lastCode == 0`; `resp.lastData["event_mode"].toString() == "write"`

**T23 — start_server event_mode="all"**
- 前置条件: 构造 Handler
- 输入: `handle("start_server", {"listen_port": 0, "event_mode": "all"}, resp)` 后 `handle("status", {}, resp)`
- 预期: start 返回 code=0；status 返回 `event_mode="all"`
- 断言: `resp.lastCode == 0`; `resp.lastData["event_mode"].toString() == "all"`

**T24 — stop 后切换 event_mode**
- 前置条件: 已 start_server(event_mode="none")
- 输入: `handle("stop_server", {}, resp)` 后 `handle("start_server", {"listen_port": 0, "event_mode": "read"}, resp)` 后 `handle("status", {}, resp)`
- 预期: status 返回 `event_mode="read"`
- 断言: `resp.lastData["event_mode"].toString() == "read"`

**T25 — start_server 非法 event_mode**
- 前置条件: 构造 Handler
- 输入: `handle("start_server", {"listen_port": 0, "event_mode": "invalid"}, resp)`
- 预期: 返回 code=3，服务未启动
- 断言: `resp.lastCode == 3`; `resp.lastData["message"].toString().contains("Invalid event_mode")`

#### 用例详情（ModbusRtuServerHandler）

**T21 — start_server 默认 event_mode**
- 前置条件: 构造 Handler
- 输入: `handle("start_server", {"listen_port": 0}, resp)` 后 `handle("status", {}, resp)`
- 预期: status 返回 `event_mode="write"`
- 断言: `resp.lastData["event_mode"].toString() == "write"`

**T22 — start_server event_mode="all"**
- 前置条件: 构造 Handler
- 输入: `handle("start_server", {"listen_port": 0, "event_mode": "all"}, resp)` 后 `handle("status", {}, resp)`
- 预期: status 返回 `event_mode="all"`
- 断言: `resp.lastData["event_mode"].toString() == "all"`

**T23 — stop 后切换 event_mode**
- 前置条件: 已 start_server(event_mode="write")
- 输入: stop → start(event_mode="none") → status
- 预期: status 返回 `event_mode="none"`
- 断言: `resp.lastData["event_mode"].toString() == "none"`

**T24 — start_server 非法 event_mode**
- 前置条件: 构造 Handler
- 输入: `handle("start_server", {"listen_port": 0, "event_mode": "xyz"}, resp)`
- 预期: 返回 code=3，服务未启动
- 断言: `resp.lastCode == 3`; `resp.lastData["message"].toString().contains("Invalid event_mode")`

#### 测试代码（ModbusTcpServerHandler）

```cpp
// T22 — start_server 默认 event_mode
TEST_F(ModbusTcpServerHandlerTest, T22_StartServerDefaultEventMode) {
    startServer();
    resp.reset();
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["event_mode"].toString(), "write");
}

// T23 — start_server event_mode="all"
TEST_F(ModbusTcpServerHandlerTest, T23_StartServerEventModeAll) {
    handler.handle("start_server",
        QJsonObject{{"listen_port", 0}, {"event_mode", "all"}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    resp.reset();
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastData["event_mode"].toString(), "all");
}

// T24 — stop 后切换 event_mode
TEST_F(ModbusTcpServerHandlerTest, T24_SwitchEventModeAfterRestart) {
    handler.handle("start_server",
        QJsonObject{{"listen_port", 0}, {"event_mode", "none"}}, resp);
    resp.reset();
    handler.handle("stop_server", QJsonObject{}, resp);
    resp.reset();
    handler.handle("start_server",
        QJsonObject{{"listen_port", 0}, {"event_mode", "read"}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    resp.reset();
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastData["event_mode"].toString(), "read");
}

// T25 — start_server 非法 event_mode
TEST_F(ModbusTcpServerHandlerTest, T25_StartServerInvalidEventMode) {
    handler.handle("start_server",
        QJsonObject{{"listen_port", 0}, {"event_mode", "invalid"}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("Invalid event_mode"));
}
```

#### 测试代码（ModbusRtuServerHandler）

```cpp
// T21 — start_server 默认 event_mode
TEST_F(ModbusRtuServerHandlerTest, T21_StartServerDefaultEventMode) {
    startServer();
    resp.reset();
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["event_mode"].toString(), "write");
}

// T22 — start_server event_mode="all"
TEST_F(ModbusRtuServerHandlerTest, T22_StartServerEventModeAll) {
    handler.handle("start_server",
        QJsonObject{{"listen_port", 0}, {"event_mode", "all"}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    resp.reset();
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastData["event_mode"].toString(), "all");
}

// T23 — stop 后切换 event_mode
TEST_F(ModbusRtuServerHandlerTest, T23_SwitchEventModeAfterRestart) {
    startServer();
    resp.reset();
    handler.handle("stop_server", QJsonObject{}, resp);
    resp.reset();
    handler.handle("start_server",
        QJsonObject{{"listen_port", 0}, {"event_mode", "none"}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    resp.reset();
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastData["event_mode"].toString(), "none");
}

// T24 — start_server 非法 event_mode
TEST_F(ModbusRtuServerHandlerTest, T24_StartServerInvalidEventMode) {
    handler.handle("start_server",
        QJsonObject{{"listen_port", 0}, {"event_mode", "xyz"}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("Invalid event_mode"));
}
```

#### 路径矩阵（ModbusRtuSerialServerHandler — T11–T14）

注意：串口 Server 的 `start_server` 需要真实串口，CI 环境无法启动。因此 T11–T13 通过直接设置 `m_eventMode` 的替代方式验证 `status` 返回值，T14 验证非法值拒绝（不依赖串口）。

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| 构造后默认 event_mode | status 返回 event_mode="write" | T11 |
| `start_server` event_mode="all"（无串口，启动失败） | 启动失败后 event_mode 保持默认 "write" | T12 |
| `start_server` event_mode="invalid" | 返回 code=3，白名单校验拒绝 | T13 |
| status 包含 event_mode 字段 | 未启动时 status 返回 event_mode="write" | T14 |

#### 用例详情（ModbusRtuSerialServerHandler）

**T11 — 默认 event_mode**
- 前置条件: 构造 Handler
- 输入: `handle("status", {}, resp)`
- 预期: status 返回 `event_mode="write"`（默认值）
- 断言: `resp.lastCode == 0`; `resp.lastData["event_mode"].toString() == "write"`

**T12 — start_server 启动失败不改变 event_mode**
- 前置条件: 构造 Handler
- 输入: `handle("start_server", {"port_name": "COM_TEST", "event_mode": "all"}, resp)` 后 `handle("status", {}, resp)`
- 预期: start 返回 code=1（串口打开失败）；status 返回 `event_mode="write"`（未被修改）
- 断言: `resp.lastCode == 1`（start）; `resp.lastData["event_mode"].toString() == "write"`（status）

**T13 — start_server 非法 event_mode**
- 前置条件: 构造 Handler
- 输入: `handle("start_server", {"port_name": "COM_TEST", "event_mode": "bad"}, resp)`
- 预期: 返回 code=3，白名单校验拒绝
- 断言: `resp.lastCode == 3`; `resp.lastData["message"].toString().contains("Invalid event_mode")`

**T14 — status 包含 event_mode 字段**
- 前置条件: 构造 Handler，添加 Unit
- 输入: `handle("status", {}, resp)`
- 预期: 返回包含 `event_mode` 字段
- 断言: `resp.lastData.contains("event_mode")`; `resp.lastData["event_mode"].toString() == "write"`

#### 测试代码（ModbusRtuSerialServerHandler）

```cpp
// T11 — 默认 event_mode
TEST_F(ModbusRtuSerialServerHandlerTest, T11_DefaultEventMode) {
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["event_mode"].toString(), "write");
}

// T12 — start_server 启动失败不改变 event_mode
TEST_F(ModbusRtuSerialServerHandlerTest, T12_StartFailureKeepsEventMode) {
    handler.handle("start_server",
        QJsonObject{{"port_name", "COM_TEST"}, {"event_mode", "all"}}, resp);
    EXPECT_EQ(resp.lastCode, 1);
    resp.reset();
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastData["event_mode"].toString(), "write");
}

// T13 — start_server 非法 event_mode
TEST_F(ModbusRtuSerialServerHandlerTest, T13_StartServerInvalidEventMode) {
    handler.handle("start_server",
        QJsonObject{{"port_name", "COM_TEST"}, {"event_mode", "bad"}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("Invalid event_mode"));
}

// T14 — status 包含 event_mode 字段
TEST_F(ModbusRtuSerialServerHandlerTest, T14_StatusContainsEventMode) {
    addUnit(1);
    resp.reset();
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_TRUE(resp.lastData.contains("event_mode"));
    EXPECT_EQ(resp.lastData["event_mode"].toString(), "write");
}
```

### 6.2 集成测试

#### 6.2.1 ModbusTCP Server（`stdio.drv.modbustcp_server`）

- 启动进程，通过 stdin 发送 `start_server` 命令（含 `event_mode` 参数）
- 使用外部 Modbus TCP 主站工具连接从站，分别执行读操作（FC 0x01–0x04）和写操作（FC 0x05/0x06/0x0F/0x10）
- 验证 stdout 输出的事件 JSONL 行：
  - `event_mode="write"`: 仅出现 `data_written` 事件，无 `data_read`
  - `event_mode="all"`: 同时出现 `data_written` 和 `data_read` 事件
  - `event_mode="read"`: 仅出现 `data_read` 事件，无 `data_written`
  - `event_mode="none"`: 无任何数据事件
- 验证 `client_connected` / `client_disconnected` 事件在所有模式下均正常推送

#### 6.2.2 ModbusRTU Server（`stdio.drv.modbusrtu_server`）

- 启动进程，通过 stdin 发送 `start_server` 命令（含 `event_mode` 参数）
- 使用外部 Modbus RTU over TCP 主站工具连接从站，分别执行读/写操作
- 验证事件过滤行为与 6.2.1 一致（4 种 event_mode 值）
- 验证 `client_connected` / `client_disconnected` 事件在所有模式下均正常推送

#### 6.2.3 ModbusRTU Serial Server（`stdio.drv.modbusrtu_serial_server`）

- 需要真实串口或虚拟串口对（如 `com0com`、`socat`）
- 启动进程，通过 stdin 发送 `start_server` 命令（含串口参数和 `event_mode` 参数）
- 使用外部 Modbus RTU 主站工具通过串口连接从站，分别执行读/写操作
- 验证事件过滤行为与 6.2.1 一致（4 种 event_mode 值）
- 注意：串口 Server 无 `client_connected`/`client_disconnected` 事件，无需验证

### 6.3 验收标准

- [ ] 构建通过：`build.bat` 成功编译受影响的 6 个驱动目标
- [ ] Meta 验证：`build/bin/stdio.drv.modbustcp_server.exe --export-meta`、`build/bin/stdio.drv.modbusrtu_server.exe --export-meta`、`build/bin/stdio.drv.modbusrtu_serial_server.exe --export-meta` 输出中 `start_server` 命令均包含 `event_mode` 参数定义
- [ ] 单元测试通过：`build/bin/stdiolink_tests --gtest_filter="ModbusTcpServerHandler*:ModbusRtuServerHandler*:ModbusRtuSerialServer*"` 全部通过
- [ ] 新增测试用例通过：TCP T22–T25、RTU T21–T24、Serial T11–T14
- [ ] 既有测试无回归：TCP T01–T21、RTU T01–T20、Serial T01–T10 全部通过
- [ ] 非法 `event_mode` 值被拒绝（T25, T24, T13）
- [ ] 启动失败时 `event_mode` 保持原值（Serial T12 验证）

## 7. 风险与控制

- 风险: 高频读取场景下 `event_mode="all"` 产生大量 `data_read` 事件，可能导致 stdout 缓冲区压力
  - 控制: 默认值为 `"write"`，用户需显式开启读事件；文档中注明高频场景建议
  - 控制: 事件推送为异步信号槽机制，不阻塞协议处理主路径
  - 测试覆盖: TCP T22、RTU T21、Serial T11（默认模式验证）

- 风险: `connectEvents()` 在构造函数中调用，`m_eventMode` 尚未被 `start_server` 设置，lambda 捕获的 `this->m_eventMode` 可能读到默认值
  - 控制: `m_eventMode` 初始化为 `"write"`，与不传参时的预期行为一致；lambda 通过 `this` 指针读取成员变量，运行时取最新值
  - 测试覆盖: TCP T22、RTU T21、Serial T11（默认值验证）

- 风险: 三个驱动的改动高度相似，容易遗漏某个驱动的某处改动
  - 控制: 文件变更清单逐文件列出；实现时按 Server 类 → Handler 类的顺序逐驱动完成
  - 控制: 构建通过即验证信号声明与连接的完整性（Qt MOC 编译检查）
  - 测试覆盖: TCP T22–T25、RTU T21–T24、Serial T11–T14

- 风险: 非法 `event_mode` 值绕过 meta 校验（如单测直接调用 `handle()`）导致隐式行为
  - 控制: Handler 内部做白名单校验，非法值返回 code=3 错误，不启动服务
  - 测试覆盖: TCP T25、RTU T24、Serial T13

- 风险: `start_server` 失败时 `m_eventMode` 已被修改，`status` 返回不一致的状态
  - 控制: `m_eventMode` 仅在启动成功后赋值，失败时保持原值
  - 测试覆盖: Serial T12（启动失败后 event_mode 保持默认值）

## 8. 里程碑完成定义（DoD）

- [ ] 三个 Server 类均新增 `dataRead` 信号，4 个读操作成功路径均在锁外 emit
- [ ] 三个 Handler 类均支持 `event_mode` 参数，白名单校验 + 启动成功后赋值 + `connectEvents()` 条件过滤
- [ ] `status` 命令响应中包含 `event_mode` 字段
- [ ] `start_server` 的 meta 定义包含 `event_mode` 枚举参数
- [ ] 默认行为（不传 `event_mode`）与改动前完全一致（向后兼容）
- [ ] 非法 `event_mode` 值被拒绝，返回 code=3
- [ ] 启动失败时 `m_eventMode` 保持原值
- [ ] 新增单元测试全部通过：TCP T22–T25、RTU T21–T24、Serial T11–T14
- [ ] 既有测试无回归：`stdiolink_tests --gtest_filter="ModbusTcpServerHandler*:ModbusRtuServerHandler*:ModbusRtuSerialServer*"`
- [ ] 驱动设备文档同步：`doc/device/modbustcp_server.md`、`doc/device/modbusrtu_server.md`、`doc/device/modbusrtu_serial_server.md` 更新 `event_mode` 参数与 `data_read` 事件说明
