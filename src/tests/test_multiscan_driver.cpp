#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <stdiolink/driver/mock_responder.h>
#include "demo/multiscan_driver/multiscan_driver.h"

using stdiolink::MockResponder;
using namespace stdiolink::meta;

class MultiscanDriverTest : public ::testing::Test {
protected:
    MultiscanDriver driver;
    MockResponder responder;
};

TEST_F(MultiscanDriverTest, M92_CPP_01_ScanTargets_ValidInput_ReturnsResults) {
    const QJsonValue data = QJsonDocument::fromJson(R"({
        "targets": [
            {"host": "127.0.0.1", "port": 2368},
            {"host": "127.0.0.2", "port": 2369}
        ],
        "mode": "quick"
    })").object();
    driver.handle("scan_targets", data, responder);

    ASSERT_EQ(responder.responses.size(), 3u);
    EXPECT_EQ(responder.responses.back().status, QString("done"));

    const auto results = responder.responses.back().payload.toObject().value("results").toArray();
    EXPECT_EQ(results.size(), 2);
    const auto first = results[0].toObject();
    EXPECT_EQ(first.value("host").toString(), QString("127.0.0.1"));
    EXPECT_TRUE(first.contains("status"));
    EXPECT_TRUE(first.contains("latency_ms"));
}

TEST_F(MultiscanDriverTest, M92_CPP_02_ScanTargets_EmptyTargets_Returns400) {
    const QJsonValue data = QJsonDocument::fromJson(R"({"targets":[]})").object();
    driver.handle("scan_targets", data, responder);
    ASSERT_EQ(responder.responses.size(), 1u);
    EXPECT_EQ(responder.responses.back().status, QString("error"));
    EXPECT_EQ(responder.responses.back().code, 400);
}

TEST_F(MultiscanDriverTest, M92_CPP_03_ScanTargets_UnknownSubfield_Ignored) {
    const QJsonValue data = QJsonDocument::fromJson(R"({
        "targets": [{"host":"127.0.0.1","port":2368,"unknown":"x"}]
    })").object();
    driver.handle("scan_targets", data, responder);

    ASSERT_EQ(responder.responses.size(), 2u);
    EXPECT_EQ(responder.responses.back().status, QString("done"));
}

TEST_F(MultiscanDriverTest, M92_CPP_04_ConfigureChannels_ValidInput_ReturnsConfigured) {
    const QJsonValue data = QJsonDocument::fromJson(R"({
        "channels": [
            {"id":0,"label":"ch0","enabled":true,"sample_hz":100},
            {"id":1,"label":"ch1"}
        ]
    })").object();
    driver.handle("configure_channels", data, responder);

    ASSERT_EQ(responder.responses.size(), 1u);
    EXPECT_EQ(responder.responses.back().status, QString("done"));
    EXPECT_EQ(responder.responses.back().payload.toObject().value("configured").toInt(), 2);
}

TEST_F(MultiscanDriverTest, M92_CPP_05_ConfigureChannels_MissingRequiredSubfields_Skipped) {
    const QJsonValue data = QJsonDocument::fromJson(R"({
        "channels": [{"id":0,"label":"ch0"}, {"only_sample_hz":100}]
    })").object();
    driver.handle("configure_channels", data, responder);

    ASSERT_EQ(responder.responses.size(), 1u);
    const auto payload = responder.responses.back().payload.toObject();
    EXPECT_EQ(responder.responses.back().status, QString("done"));
    EXPECT_EQ(payload.value("configured").toInt(), 1);
    EXPECT_EQ(payload.value("skipped").toInt(), 1);
}

TEST_F(MultiscanDriverTest, M92_CPP_06a_ConfigureChannels_EmptyChannels_Returns400) {
    const QJsonValue data = QJsonDocument::fromJson(R"({"channels":[]})").object();
    driver.handle("configure_channels", data, responder);
    ASSERT_EQ(responder.responses.size(), 1u);
    EXPECT_EQ(responder.responses.back().status, QString("error"));
    EXPECT_EQ(responder.responses.back().code, 400);
}

TEST_F(MultiscanDriverTest, M92_CPP_06_DriverMeta_ScanTargets_ItemsFieldsCorrect) {
    const auto& meta = driver.driverMeta();
    const auto* scanCmd = meta.findCommand("scan_targets");
    ASSERT_NE(scanCmd, nullptr);
    ASSERT_FALSE(scanCmd->params.isEmpty());

    const auto& targetsParam = scanCmd->params[0];
    EXPECT_EQ(targetsParam.name, QString("targets"));
    ASSERT_NE(targetsParam.items, nullptr);
    EXPECT_EQ(targetsParam.items->fields.size(), 3);
    EXPECT_EQ(targetsParam.items->fields[0].name, QString("host"));
    EXPECT_EQ(targetsParam.items->fields[0].type, FieldType::String);
    EXPECT_EQ(targetsParam.items->fields[1].name, QString("port"));
    EXPECT_EQ(targetsParam.items->fields[1].type, FieldType::Int);
    EXPECT_EQ(targetsParam.items->fields[2].name, QString("timeout_ms"));
    EXPECT_EQ(targetsParam.items->fields[2].type, FieldType::Int);
}
