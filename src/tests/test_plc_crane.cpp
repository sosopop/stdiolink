#include <gtest/gtest.h>
#include <future>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

#include "driver_plc_crane/handler.h"
#include "driver_modbustcp_server/modbus_tcp_server.h"

class PlcCraneMockResponder : public stdiolink::IResponder {
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

class PlcCraneHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int argc = 1;
        static char arg0[] = "test";
        static char* argv[] = {arg0};
        if (!QCoreApplication::instance()) {
            new QCoreApplication(argc, argv);
        }
    }
    PlcCraneHandler handler;
    PlcCraneMockResponder resp;
};

bool waitForFuture(std::future<void>& task, int timeoutMs = 2000) {
    QElapsedTimer timer;
    timer.start();
    while (task.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        if (timer.elapsed() >= timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(5);
    }
    return true;
}

void expectReadStatusFromDiscreteInputs(PlcCraneHandler& handler, PlcCraneMockResponder& resp,
                                        const QJsonObject& rawBits,
                                        const QJsonObject& expectedStatus) {
    ModbusTcpServer server;
    ASSERT_TRUE(server.startServer(0, "127.0.0.1"));
    ASSERT_TRUE(server.addUnit(1, 32));
    ASSERT_TRUE(server.setDiscreteInput(1, 9, rawBits["cylinder_up"].toBool()));
    ASSERT_TRUE(server.setDiscreteInput(1, 10, rawBits["cylinder_down"].toBool()));
    ASSERT_TRUE(server.setDiscreteInput(1, 13, rawBits["valve_open"].toBool()));
    ASSERT_TRUE(server.setDiscreteInput(1, 14, rawBits["valve_closed"].toBool()));

    resp.reset();
    auto task = std::async(std::launch::async, [&]() {
        handler.handle("read_status",
                       QJsonObject{{"host", "127.0.0.1"},
                                   {"port", static_cast<int>(server.serverPort())},
                                   {"unit_id", 1},
                                   {"timeout", 500}},
                       resp);
    });

    ASSERT_TRUE(waitForFuture(task)) << "read_status timed out";
    task.get();

    EXPECT_EQ(resp.lastStatus, "done");
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["cylinder_up"].toBool(), expectedStatus["cylinder_up"].toBool());
    EXPECT_EQ(resp.lastData["cylinder_down"].toBool(), expectedStatus["cylinder_down"].toBool());
    EXPECT_EQ(resp.lastData["valve_open"].toBool(), expectedStatus["valve_open"].toBool());
    EXPECT_EQ(resp.lastData["valve_closed"].toBool(), expectedStatus["valve_closed"].toBool());
}

// T01 — Handler status 命令
TEST_F(PlcCraneHandlerTest, T01_Status) {
    handler.handle("status", QJsonObject{}, resp);
    EXPECT_EQ(resp.lastCode, 0);
    EXPECT_EQ(resp.lastData["status"].toString(), "ready");
}

// T02 — Handler read_status 连接失败
TEST_F(PlcCraneHandlerTest, T02_ReadStatusConnectionFailed) {
    handler.handle("read_status", QJsonObject{
        {"host", "127.0.0.1"}, {"port", 59999},
        {"unit_id", 1}, {"timeout", 100}
    }, resp);
    EXPECT_EQ(resp.lastCode, 1);
}

// T03 — Handler cylinder_control 合法 action（因连接失败返回 code 1）
TEST_F(PlcCraneHandlerTest, T03_CylinderControlValidAction) {
    handler.handle("cylinder_control", QJsonObject{
        {"host", "127.0.0.1"}, {"port", 59999},
        {"action", "up"}, {"timeout", 100}
    }, resp);
    EXPECT_EQ(resp.lastCode, 1);
}

// T04 — Handler cylinder_control 非法 action
TEST_F(PlcCraneHandlerTest, T04_CylinderControlInvalidAction) {
    handler.handle("cylinder_control", QJsonObject{
        {"host", "127.0.0.1"}, {"action", "invalid"}
    }, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("invalid"));
}

// T05 — Handler valve_control 非法 action
TEST_F(PlcCraneHandlerTest, T05_ValveControlInvalidAction) {
    handler.handle("valve_control", QJsonObject{
        {"host", "127.0.0.1"}, {"action", "invalid"}
    }, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("open, close, stop"));
}

// T06 — Handler set_mode 非法 mode
TEST_F(PlcCraneHandlerTest, T06_SetModeInvalidMode) {
    handler.handle("set_mode", QJsonObject{
        {"host", "127.0.0.1"}, {"mode", "invalid"}
    }, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("manual, auto"));
}

// T07 — Handler set_run 非法 action
TEST_F(PlcCraneHandlerTest, T07_SetRunInvalidAction) {
    handler.handle("set_run", QJsonObject{
        {"host", "127.0.0.1"}, {"action", "invalid"}
    }, resp);
    EXPECT_EQ(resp.lastCode, 3);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("start, stop"));
}

// T08 — Handler read_status 连接参数提取
TEST_F(PlcCraneHandlerTest, T08_ReadStatusConnectionParams) {
    handler.handle("read_status", QJsonObject{
        {"host", "10.0.0.1"}, {"port", 59999},
        {"unit_id", 5}, {"timeout", 100}
    }, resp);
    EXPECT_NE(resp.lastCode, 0);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("10.0.0.1"));
    EXPECT_TRUE(resp.lastData["message"].toString().contains("59999"));
}

// T09 — Handler read_status: cylinder_up 低电平有效，其余高电平有效
TEST_F(PlcCraneHandlerTest, T09_ReadStatusMapsDiscreteInputsWithMixedPolarity) {
    expectReadStatusFromDiscreteInputs(
        handler, resp,
        QJsonObject{{"cylinder_up", false},
                    {"cylinder_down", false},
                    {"valve_open", false},
                    {"valve_closed", true}},
        QJsonObject{{"cylinder_up", true},
                    {"cylinder_down", false},
                    {"valve_open", false},
                    {"valve_closed", true}});
}

// T10 — Handler read_status: 非上到位状态按高电平有效
TEST_F(PlcCraneHandlerTest, T10_ReadStatusMapsHighActiveStatusBits) {
    expectReadStatusFromDiscreteInputs(
        handler, resp,
        QJsonObject{{"cylinder_up", true},
                    {"cylinder_down", true},
                    {"valve_open", true},
                    {"valve_closed", false}},
        QJsonObject{{"cylinder_up", false},
                    {"cylinder_down", true},
                    {"valve_open", true},
                    {"valve_closed", false}});
}
