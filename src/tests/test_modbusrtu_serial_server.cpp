#include <gtest/gtest.h>
#include <QByteArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "driver_modbusrtu_serial_server/modbus_rtu_serial_server.h"
#include "driver_modbusrtu_serial_server/handler.h"

// T01 — T3.5 计算：9600 baud, 8N1
TEST(ModbusRtuSerialServerT35, T01_9600_8N1) {
    double t35 = ModbusRtuSerialServer::calculateT35(9600, 8, false, 1.0);
    EXPECT_NEAR(t35, 3.646, 0.01);
}

// T02 — T3.5 计算：19200 baud, 8E1
TEST(ModbusRtuSerialServerT35, T02_19200_8E1) {
    double t35 = ModbusRtuSerialServer::calculateT35(19200, 8, true, 1.0);
    EXPECT_NEAR(t35, 2.005, 0.01);
}

// T03 — T3.5 计算：115200 baud 固定值
TEST(ModbusRtuSerialServerT35, T03_115200_Fixed) {
    double t35 = ModbusRtuSerialServer::calculateT35(115200, 8, false, 1.0);
    EXPECT_DOUBLE_EQ(t35, 1.75);
}

// T04 — CRC16 已知数据校验
TEST(ModbusRtuSerialServerCRC, T04_KnownData) {
    QByteArray data = QByteArray::fromHex("0103000A0001");
    uint16_t crc = ModbusRtuSerialServer::calculateCRC16(data);
    EXPECT_NE(crc, 0u);
    // 附加 CRC 后整帧校验应为 0
    QByteArray frame = data;
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    EXPECT_EQ(ModbusRtuSerialServer::calculateCRC16(frame), 0u);
}

class SerialMockResponder : public stdiolink::IResponder {
public:
    int lastCode = -1;
    QJsonObject lastData;
    QString lastStatus;

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
        Q_UNUSED(name); Q_UNUSED(code); Q_UNUSED(data);
    }
    void reset() { lastCode = -1; lastData = {}; lastStatus.clear(); }
};

class ModbusRtuSerialServerHandlerTest : public ::testing::Test {
protected:
    ModbusRtuSerialServerHandler handler;
    SerialMockResponder resp;

    void addUnit(int unitId, int size = 10000) {
        resp.reset();
        handler.handle("add_unit",
            QJsonObject{{"unit_id", unitId}, {"data_area_size", size}}, resp);
    }
};

// T05 — start_server 测试环境无串口
TEST_F(ModbusRtuSerialServerHandlerTest, T05_StartServerNoSerialPort) {
    handler.handle("start_server",
        QJsonObject{{"port_name","COM_TEST"},{"baud_rate",9600}}, resp);
    EXPECT_EQ(resp.lastCode, 1);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("COM_TEST"));
}

// T06 — stop_server 未启动
TEST_F(ModbusRtuSerialServerHandlerTest, T06_StopServerNotRunning) {
    handler.handle("stop_server", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 3);
}

// T07 — add_unit + set/get holding register
TEST_F(ModbusRtuSerialServerHandlerTest, T07_SetGetHoldingRegister) {
    addUnit(1);
    resp.reset();
    handler.handle("set_holding_register",
        QJsonObject{{"unit_id",1},{"address",0},{"value",1234}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    resp.reset();
    handler.handle("get_holding_register",
        QJsonObject{{"unit_id",1},{"address",0}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["value"].toInt(), 1234);
}

// T08 — unit_id 不存在
TEST_F(ModbusRtuSerialServerHandlerTest, T08_UnitNotFound) {
    handler.handle("get_coil",
        QJsonObject{{"unit_id",99},{"address",0}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("not found"));
}

// T09 — status 未启动
TEST_F(ModbusRtuSerialServerHandlerTest, T09_StatusNotStarted) {
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["listening"].toBool(), false);
    EXPECT_TRUE(resp.lastData["units"].toArray().isEmpty());
}

// T10 — stop_server 未启动（独立用例）
TEST_F(ModbusRtuSerialServerHandlerTest, T10_StopServerNotRunning) {
    handler.handle("stop_server", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("not running"));
}
