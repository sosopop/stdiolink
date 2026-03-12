#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonObject>

#include "driver_limaco_1_radar/handler.h"
#include "driver_limaco_5_radar/handler.h"
#include "driver_limaco_common/limaco_decode.h"

namespace {

class JsonResponder : public stdiolink::IResponder {
public:
    QString lastStatus;
    int lastCode = -1;
    QJsonValue lastPayload;

    void event(int code, const QJsonValue& payload) override {
        Q_UNUSED(code);
        Q_UNUSED(payload);
    }

    void event(const QString& name, int code, const QJsonValue& data) override {
        Q_UNUSED(name);
        Q_UNUSED(code);
        Q_UNUSED(data);
    }

    void done(int code, const QJsonValue& payload) override {
        lastStatus = "done";
        lastCode = code;
        lastPayload = payload;
    }

    void error(int code, const QJsonValue& payload) override {
        lastStatus = "error";
        lastCode = code;
        lastPayload = payload;
    }

    void reset() {
        lastStatus.clear();
        lastCode = -1;
        lastPayload = QJsonValue();
    }
};

const stdiolink::meta::FieldMeta* findParam(const stdiolink::meta::CommandMeta* command,
                                            const QString& name) {
    if (!command) {
        return nullptr;
    }
    for (const auto& param : command->params) {
        if (param.name == name) {
            return &param;
        }
    }
    return nullptr;
}

class LimacoHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int argc = 1;
        static char arg0[] = "test";
        static char* argv[] = {arg0};
        if (!QCoreApplication::instance()) {
            new QCoreApplication(argc, argv);
        }
    }

    Limaco1RadarHandler singleHandler;
    Limaco5RadarHandler fiveHandler;
    JsonResponder responder;
};

} // namespace

TEST(LimacoDecodeTest, SinglePointDecodeValid) {
    limaco_driver::Limaco1DistanceData decoded;
    QString errorMessage;
    ASSERT_TRUE(limaco_driver::decodeLimaco1Distance(
        QVector<uint16_t>{1u, 2u, 3u, 49u, 5u}, decoded, &errorMessage));
    EXPECT_TRUE(errorMessage.isEmpty());
    EXPECT_TRUE(decoded.distanceValid);
    EXPECT_EQ(decoded.statusRegister, 49u);
    EXPECT_DOUBLE_EQ(decoded.distanceMeters, (65535.0 + 2.0) / 10000.0);
}

TEST(LimacoDecodeTest, SinglePointDecodeInvalidStatus) {
    limaco_driver::Limaco1DistanceData decoded;
    ASSERT_TRUE(limaco_driver::decodeLimaco1Distance(
        QVector<uint16_t>{10u, 20u, 30u, 50u, 40u}, decoded, nullptr));
    EXPECT_FALSE(decoded.distanceValid);
    EXPECT_DOUBLE_EQ(decoded.distanceMeters, 0.0);
}

TEST(LimacoDecodeTest, FivePointDecodeLegacyLayout) {
    limaco_driver::Limaco5DistanceData decoded;
    ASSERT_TRUE(limaco_driver::decodeLimaco5Distance(
        QVector<uint16_t>{101u, 0u, 202u, 1u, 303u, 0u, 404u, 0u, 505u, 2u, 9u, 9u, 9u, 9u, 9u},
        decoded,
        nullptr));

    EXPECT_EQ(decoded.format, "legacy");
    ASSERT_EQ(decoded.points.size(), 5);
    EXPECT_EQ(decoded.points[0].index, 1);
    EXPECT_EQ(decoded.points[0].distanceRaw, 101u);
    EXPECT_TRUE(decoded.points[0].valid);
    EXPECT_EQ(decoded.points[1].state, 1u);
    EXPECT_FALSE(decoded.points[1].valid);
    EXPECT_EQ(decoded.points[4].distanceRaw, 505u);
}

TEST(LimacoDecodeTest, FivePointDecodeV2Layout) {
    limaco_driver::Limaco5DistanceData decoded;
    ASSERT_TRUE(limaco_driver::decodeLimaco5Distance(
        QVector<uint16_t>{0x0014u, 111u, 0u, 0x0114u, 222u, 7u, 0x0214u, 333u, 0u, 0x0314u, 444u, 0u, 0x0414u, 555u, 2u},
        decoded,
        nullptr));

    EXPECT_EQ(decoded.format, "v2");
    ASSERT_EQ(decoded.points.size(), 5);
    EXPECT_EQ(decoded.points[0].distanceRaw, 111u);
    EXPECT_EQ(decoded.points[1].distanceRaw, 222u);
    EXPECT_FALSE(decoded.points[1].valid);
    EXPECT_EQ(decoded.points[4].distanceRaw, 555u);
    EXPECT_FALSE(decoded.points[4].valid);
}

TEST_F(LimacoHandlerTest, SingleStatus) {
    singleHandler.handle("status", QJsonObject{}, responder);
    EXPECT_EQ(responder.lastStatus, "done");
    EXPECT_EQ(responder.lastCode, 0);
    EXPECT_EQ(responder.lastPayload.toObject().value("status").toString(), "ready");
}

TEST_F(LimacoHandlerTest, SerialTransportRequiresPortName) {
    singleHandler.handle("read_distance", QJsonObject{}, responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 3);
    EXPECT_TRUE(responder.lastPayload.toObject().value("message").toString().contains("port_name"));
}

TEST_F(LimacoHandlerTest, TcpTransportDefaultsHost) {
    fiveHandler.handle("read_distance", QJsonObject{
        {"transport", "tcp"},
        {"port", 59999},
        {"timeout", 100}
    }, responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 1);
    const QString message = responder.lastPayload.toObject().value("message").toString();
    EXPECT_TRUE(message.contains("127.0.0.1"));
    EXPECT_TRUE(message.contains("59999"));
}

TEST_F(LimacoHandlerTest, UnitIdMustBeInRange) {
    fiveHandler.handle("set_distance_mode", QJsonObject{
        {"transport", "tcp"},
        {"unit_id", 0}
    }, responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 3);
    EXPECT_TRUE(responder.lastPayload.toObject().value("message").toString().contains("1-247"));
}

TEST_F(LimacoHandlerTest, MetadataUsesRevisedNamesAndDefaults) {
    const auto& singleMeta = singleHandler.driverMeta();
    const auto& fiveMeta = fiveHandler.driverMeta();
    EXPECT_EQ(singleMeta.info.id, "stdio.drv.limaco_1_radar");
    EXPECT_EQ(fiveMeta.info.id, "stdio.drv.limaco_5_radar");

    const auto* singleRead = singleMeta.findCommand("read_distance");
    ASSERT_NE(singleRead, nullptr);
    const auto* transportParam = findParam(singleRead, "transport");
    const auto* hostParam = findParam(singleRead, "host");
    const auto* timeoutParam = findParam(singleRead, "timeout");
    ASSERT_NE(transportParam, nullptr);
    ASSERT_NE(hostParam, nullptr);
    ASSERT_NE(timeoutParam, nullptr);
    EXPECT_EQ(transportParam->defaultValue.toString(), "serial");
    EXPECT_EQ(hostParam->defaultValue.toString(), "127.0.0.1");
    EXPECT_EQ(timeoutParam->defaultValue.toInt(), 2000);

    const auto* fiveRead = fiveMeta.findCommand("read_distance");
    ASSERT_NE(fiveRead, nullptr);
    EXPECT_NE(findParam(fiveRead, "port_name"), nullptr);
}
