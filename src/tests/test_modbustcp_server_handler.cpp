#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "driver_modbustcp_server/handler.h"

class MockResponder : public stdiolink::IResponder {
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

// T01 — status 服务未启动
TEST_F(ModbusTcpServerHandlerTest, T01_StatusNotStarted) {
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["listening"].toBool(), false);
    EXPECT_TRUE(resp.lastData["units"].toArray().isEmpty());
}

// T02 — status 服务已启动且有 Unit
TEST_F(ModbusTcpServerHandlerTest, T02_StatusStartedWithUnit) {
    startServer();
    addUnit(1);
    resp.reset();
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["listening"].toBool(), true);
    EXPECT_GT(resp.lastData["port"].toInt(), 0);
    EXPECT_EQ(resp.lastData["units"].toArray().size(), 1);
}

// T03 — start_server 正常启动
TEST_F(ModbusTcpServerHandlerTest, T03_StartServer) {
    handler.handle("start_server",
        QJsonObject{{"listen_port", 0}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["started"].toBool(), true);
}

// T04 — stop_server 正常停止
TEST_F(ModbusTcpServerHandlerTest, T04_StopServer) {
    startServer();
    resp.reset();
    handler.handle("stop_server", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["stopped"].toBool(), true);
}

// T05 — add_unit 正常添加
TEST_F(ModbusTcpServerHandlerTest, T05_AddUnit) {
    handler.handle("add_unit",
        QJsonObject{{"unit_id", 1}, {"data_area_size", 100}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["unit_id"].toInt(), 1);
    EXPECT_EQ(resp.lastData["data_area_size"].toInt(), 100);
}

// T06 — remove_unit 正常移除
TEST_F(ModbusTcpServerHandlerTest, T06_RemoveUnit) {
    addUnit(1);
    resp.reset();
    handler.handle("remove_unit", QJsonObject{{"unit_id", 1}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["removed"].toBool(), true);
}

// T07 — list_units 列出所有 Unit
TEST_F(ModbusTcpServerHandlerTest, T07_ListUnits) {
    addUnit(1);
    addUnit(2);
    resp.reset();
    handler.handle("list_units", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["units"].toArray().size(), 2);
}

// T08 — set_coil + get_coil 正常读写
TEST_F(ModbusTcpServerHandlerTest, T08_SetGetCoil) {
    addUnit(1);
    resp.reset();
    handler.handle("set_coil",
        QJsonObject{{"unit_id",1},{"address",0},{"value",true}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    resp.reset();
    handler.handle("get_coil",
        QJsonObject{{"unit_id",1},{"address",0}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["value"].toBool(), true);
}

// T09 — set_holding_register + get_holding_register
TEST_F(ModbusTcpServerHandlerTest, T09_SetGetHoldingRegister) {
    addUnit(1);
    resp.reset();
    handler.handle("set_holding_register",
        QJsonObject{{"unit_id",1},{"address",100},{"value",1234}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    resp.reset();
    handler.handle("get_holding_register",
        QJsonObject{{"unit_id",1},{"address",100}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["value"].toInt(), 1234);
}

// T10 — set_discrete_input + get_discrete_input
TEST_F(ModbusTcpServerHandlerTest, T10_SetGetDiscreteInput) {
    addUnit(1);
    resp.reset();
    handler.handle("set_discrete_input",
        QJsonObject{{"unit_id",1},{"address",5},{"value",true}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    resp.reset();
    handler.handle("get_discrete_input",
        QJsonObject{{"unit_id",1},{"address",5}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["value"].toBool(), true);
}

// T11 — set_input_register + get_input_register
TEST_F(ModbusTcpServerHandlerTest, T11_SetGetInputRegister) {
    addUnit(1);
    resp.reset();
    handler.handle("set_input_register",
        QJsonObject{{"unit_id",1},{"address",50},{"value",5678}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    resp.reset();
    handler.handle("get_input_register",
        QJsonObject{{"unit_id",1},{"address",50}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["value"].toInt(), 5678);
}

// T12 — start_server 重复启动
TEST_F(ModbusTcpServerHandlerTest, T12_StartServerDuplicate) {
    startServer();
    resp.reset();
    handler.handle("start_server",
        QJsonObject{{"listen_port", 0}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("already running"));
}

// T13 — stop_server 未启动时停止
TEST_F(ModbusTcpServerHandlerTest, T13_StopServerNotRunning) {
    handler.handle("stop_server", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 3);
}

// T14 — add_unit unit_id 已存在
TEST_F(ModbusTcpServerHandlerTest, T14_AddUnitDuplicate) {
    addUnit(1);
    resp.reset();
    handler.handle("add_unit", QJsonObject{{"unit_id", 1}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("already exists"));
}

// T15 — 数据操作 unit_id 不存在
TEST_F(ModbusTcpServerHandlerTest, T15_DataOpUnitNotFound) {
    handler.handle("get_coil",
        QJsonObject{{"unit_id",99},{"address",0}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("not found"));
}

// T16 — 数据操作 address 越界
TEST_F(ModbusTcpServerHandlerTest, T16_DataOpAddressOutOfRange) {
    addUnit(1, 100);
    resp.reset();
    handler.handle("get_coil",
        QJsonObject{{"unit_id",1},{"address",200}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("out of range"));
}

// T17 — set_registers_batch float32 写入
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

// T18 — get_registers_batch float32 读取
TEST_F(ModbusTcpServerHandlerTest, T18_GetRegistersBatchFloat32) {
    addUnit(1);
    // 先写入
    resp.reset();
    handler.handle("set_registers_batch", QJsonObject{
        {"unit_id",1},{"area","holding"},{"address",0},
        {"values", QJsonArray{50.0}},
        {"data_type","float32"},{"byte_order","big_endian"}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    // 再读取
    resp.reset();
    handler.handle("get_registers_batch", QJsonObject{
        {"unit_id",1},{"area","holding"},{"address",0},{"count",2},
        {"data_type","float32"},{"byte_order","big_endian"}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_NEAR(resp.lastData["values"].toArray()[0].toDouble(), 50.0, 0.01);
    EXPECT_EQ(resp.lastData["raw"].toArray().size(), 2);
}

// T19 — get_registers_batch count 非类型整数倍
TEST_F(ModbusTcpServerHandlerTest, T19_GetRegistersBatchCountMismatch) {
    addUnit(1);
    resp.reset();
    handler.handle("get_registers_batch", QJsonObject{
        {"unit_id",1},{"area","holding"},{"address",0},{"count",3},
        {"data_type","float32"}}, resp);
    EXPECT_EQ(resp.lastCode, 3);
}

// T20 — set_registers_batch uint64 字符串输入 + get_registers_batch 字符串输出
TEST_F(ModbusTcpServerHandlerTest, T20_UInt64StringRoundtrip) {
    addUnit(1);
    // 写入：uint64 值通过字符串传入以保留精度（超过 2^53）
    resp.reset();
    handler.handle("set_registers_batch", QJsonObject{
        {"unit_id",1},{"area","holding"},{"address",0},
        {"values", QJsonArray{QString("18446744073709551615")}},
        {"data_type","uint64"},{"byte_order","big_endian"}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["written"].toInt(), 4);

    // 读取：uint64 输出应为字符串
    resp.reset();
    handler.handle("get_registers_batch", QJsonObject{
        {"unit_id",1},{"area","holding"},{"address",0},{"count",4},
        {"data_type","uint64"},{"byte_order","big_endian"}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    QJsonArray vals = resp.lastData["values"].toArray();
    ASSERT_EQ(vals.size(), 1);
    EXPECT_TRUE(vals[0].isString());
    EXPECT_EQ(vals[0].toString(), "18446744073709551615");
}

// T21 — set_registers_batch int64 字符串输入
TEST_F(ModbusTcpServerHandlerTest, T21_Int64StringInput) {
    addUnit(1);
    resp.reset();
    handler.handle("set_registers_batch", QJsonObject{
        {"unit_id",1},{"area","holding"},{"address",0},
        {"values", QJsonArray{QString("-9223372036854775808")}},
        {"data_type","int64"},{"byte_order","big_endian"}}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["written"].toInt(), 4);
}
