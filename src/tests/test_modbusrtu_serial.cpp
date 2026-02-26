#include <gtest/gtest.h>
#include <QByteArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "driver_modbusrtu_serial/modbus_rtu_serial_client.h"
#include "driver_modbusrtu_serial/handler.h"

// T01 — T3.5 计算：9600 baud, 8N1
TEST(ModbusRtuSerialClientT35, T01_9600_8N1) {
    double t35 = ModbusRtuSerialClient::calculateT35(9600, 8, false, 1.0);
    EXPECT_NEAR(t35, 3.646, 0.01);
}

// T02 — T3.5 计算：19200 baud, 8E1
TEST(ModbusRtuSerialClientT35, T02_19200_8E1) {
    double t35 = ModbusRtuSerialClient::calculateT35(19200, 8, true, 1.0);
    EXPECT_NEAR(t35, 2.005, 0.01);
}

// T03 — T3.5 计算：115200 baud 固定值
TEST(ModbusRtuSerialClientT35, T03_115200_Fixed) {
    double t35 = ModbusRtuSerialClient::calculateT35(115200, 8, false, 1.0);
    EXPECT_DOUBLE_EQ(t35, 1.75);
}

// T04 — CRC16 已知数据校验
TEST(ModbusRtuSerialClientCRC, T04_KnownData) {
    QByteArray data = QByteArray::fromHex("0103000A0001");
    uint16_t crc = ModbusRtuSerialClient::calculateCRC16(data);
    EXPECT_NE(crc, 0u);
    // 附加 CRC 后整帧校验应为 0
    QByteArray frame = data;
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    EXPECT_EQ(ModbusRtuSerialClient::calculateCRC16(frame), 0u);
}

// T05 — CRC16 空数据
TEST(ModbusRtuSerialClientCRC, T05_EmptyData) {
    EXPECT_EQ(ModbusRtuSerialClient::calculateCRC16(QByteArray()), 0xFFFF);
}

class SerialClientMockResponder : public stdiolink::IResponder {
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

class ModbusRtuSerialHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int argc = 1;
        static char arg0[] = "test";
        static char* argv[] = {arg0};
        if (!QCoreApplication::instance()) {
            new QCoreApplication(argc, argv);
        }
    }
    ModbusRtuSerialHandler handler;
    SerialClientMockResponder resp;
};

// T06 — Handler status 命令
TEST_F(ModbusRtuSerialHandlerTest, T06_Status) {
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["status"].toString(), "ready");
}

// T07 — Handler read_holding_registers 参数解析（无真实串口，预期 error code 1）
TEST_F(ModbusRtuSerialHandlerTest, T07_ReadHoldingRegisters_NoPort) {
    handler.handle("read_holding_registers", QJsonObject{
        {"port_name", "COM_TEST"}, {"baud_rate", 9600},
        {"address", 0}, {"count", 1}
    }, resp);
    EXPECT_EQ(resp.lastCode, 1);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("COM_TEST"));
}

// T08 — Handler write_holding_registers 类型转换参数（无真实串口，预期 error code 1）
TEST_F(ModbusRtuSerialHandlerTest, T08_WriteHoldingRegisters_NoPort) {
    handler.handle("write_holding_registers", QJsonObject{
        {"port_name", "COM_TEST"}, {"address", 0},
        {"value", 50.0}, {"data_type", "float32"},
        {"byte_order", "big_endian"}
    }, resp);
    EXPECT_EQ(resp.lastCode, 1);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("COM_TEST"));
}

// T09 — Handler count 非类型整数倍（float32 需要 2 寄存器，count=3 不是 2 的整数倍）
TEST_F(ModbusRtuSerialHandlerTest, T09_CountMismatch) {
    handler.handle("read_holding_registers", QJsonObject{
        {"port_name", "COM_TEST"}, {"address", 0},
        {"count", 3}, {"data_type", "float32"}
    }, resp);
    EXPECT_EQ(resp.lastCode, 3);
}

// T10 — Handler unit_id 越界（0 不在 1-247 范围内）
TEST_F(ModbusRtuSerialHandlerTest, T10_UnitIdOutOfRange) {
    handler.handle("read_coils", QJsonObject{
        {"port_name", "COM_TEST"}, {"unit_id", 0}, {"address", 0}
    }, resp);
    EXPECT_EQ(resp.lastCode, 3);
}
