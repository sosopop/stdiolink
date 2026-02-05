#include <QJsonArray>
#include <QJsonObject>
#include <gtest/gtest.h>
#include "stdiolink/protocol/jsonl_serializer.h"

using namespace stdiolink;

// ============================================
// 请求序列化测试
// ============================================

TEST(JsonlSerializer, SerializeRequest_Simple) {
    auto result = serializeRequest("scan");
    EXPECT_TRUE(result.contains("\"cmd\":\"scan\""));
    EXPECT_TRUE(result.endsWith('\n'));
    EXPECT_FALSE(result.contains("\"data\""));
}

TEST(JsonlSerializer, SerializeRequest_WithData) {
    QJsonObject data{{"fps", 10}, {"mode", "frame"}};
    auto result = serializeRequest("scan", data);

    EXPECT_TRUE(result.contains("\"cmd\":\"scan\""));
    EXPECT_TRUE(result.contains("\"data\""));
    EXPECT_TRUE(result.contains("\"fps\":10"));
    EXPECT_TRUE(result.endsWith('\n'));
}

TEST(JsonlSerializer, SerializeRequest_EmptyData) {
    auto result = serializeRequest("info", QJsonObject{});
    EXPECT_TRUE(result.contains("\"cmd\":\"info\""));
    EXPECT_TRUE(result.contains("\"data\":{}"));
}

TEST(JsonlSerializer, SerializeRequest_NullData) {
    auto result = serializeRequest("info", QJsonValue::Null);
    EXPECT_TRUE(result.contains("\"cmd\":\"info\""));
    EXPECT_FALSE(result.contains("\"data\""));
}

// ============================================
// 响应序列化测试
// ============================================

TEST(JsonlSerializer, SerializeResponse_Done) {
    QJsonObject payload{{"result", 42}};
    auto result = serializeResponse("done", 0, payload);

    // 单行格式
    EXPECT_TRUE(result.contains("\"status\":\"done\""));
    EXPECT_TRUE(result.contains("\"code\":0"));
    EXPECT_TRUE(result.contains("\"data\":{\"result\":42}"));
    EXPECT_TRUE(result.endsWith('\n'));
}

TEST(JsonlSerializer, SerializeResponse_Error) {
    QJsonObject payload{{"message", "invalid input"}};
    auto result = serializeResponse("error", 1007, payload);

    EXPECT_TRUE(result.contains("\"status\":\"error\""));
    EXPECT_TRUE(result.contains("\"code\":1007"));
    EXPECT_TRUE(result.contains("\"data\":{\"message\":\"invalid input\"}"));
}

TEST(JsonlSerializer, SerializeResponse_Event) {
    QJsonObject payload{{"progress", 0.5}};
    auto result = serializeResponse("event", 0, payload);

    EXPECT_TRUE(result.contains("\"status\":\"event\""));
    EXPECT_TRUE(result.contains("\"data\":{\"progress\":0.5}"));
}
