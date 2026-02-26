#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonValue>

#include "driver_plc_crane/handler.h"

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
    EXPECT_EQ(resp.lastCode, 1);
    EXPECT_TRUE(resp.lastData["message"].toString().contains("10.0.0.1"));
    EXPECT_TRUE(resp.lastData["message"].toString().contains("59999"));
}
