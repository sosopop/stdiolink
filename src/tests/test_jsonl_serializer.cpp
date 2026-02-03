#include <gtest/gtest.h>
#include "stdiolink/protocol/jsonl_serializer.h"
#include <QJsonObject>
#include <QJsonArray>

using namespace stdiolink;

// ============================================
// 请求序列化测试
// ============================================

TEST(JsonlSerializer, SerializeRequest_Simple)
{
    auto result = serializeRequest("scan");
    EXPECT_TRUE(result.contains("\"cmd\":\"scan\""));
    EXPECT_TRUE(result.endsWith('\n'));
    EXPECT_FALSE(result.contains("\"data\""));
}

TEST(JsonlSerializer, SerializeRequest_WithData)
{
    QJsonObject data{{"fps", 10}, {"mode", "frame"}};
    auto result = serializeRequest("scan", data);

    EXPECT_TRUE(result.contains("\"cmd\":\"scan\""));
    EXPECT_TRUE(result.contains("\"data\""));
    EXPECT_TRUE(result.contains("\"fps\":10"));
    EXPECT_TRUE(result.endsWith('\n'));
}

TEST(JsonlSerializer, SerializeRequest_EmptyData)
{
    auto result = serializeRequest("info", QJsonObject{});
    EXPECT_TRUE(result.contains("\"cmd\":\"info\""));
    EXPECT_TRUE(result.contains("\"data\":{}"));
}

TEST(JsonlSerializer, SerializeRequest_NullData)
{
    auto result = serializeRequest("info", QJsonValue::Null);
    EXPECT_TRUE(result.contains("\"cmd\":\"info\""));
    EXPECT_FALSE(result.contains("\"data\""));
}

// ============================================
// 响应序列化测试
// ============================================

TEST(JsonlSerializer, SerializeResponse_Done)
{
    QJsonObject payload{{"result", 42}};
    auto result = serializeResponse("done", 0, payload);

    // 应该有两行
    auto lines = result.split('\n');
    EXPECT_EQ(lines.size(), 3);  // 两行内容 + 末尾空串

    // 第一行是 header
    EXPECT_TRUE(lines[0].contains("\"status\":\"done\""));
    EXPECT_TRUE(lines[0].contains("\"code\":0"));

    // 第二行是 payload
    EXPECT_TRUE(lines[1].contains("\"result\":42"));
}

TEST(JsonlSerializer, SerializeResponse_Error)
{
    QJsonObject payload{{"message", "invalid input"}};
    auto result = serializeResponse("error", 1007, payload);

    auto lines = result.split('\n');
    EXPECT_TRUE(lines[0].contains("\"status\":\"error\""));
    EXPECT_TRUE(lines[0].contains("\"code\":1007"));
    EXPECT_TRUE(lines[1].contains("\"message\":\"invalid input\""));
}

TEST(JsonlSerializer, SerializeResponse_Event)
{
    QJsonObject payload{{"progress", 0.5}};
    auto result = serializeResponse("event", 0, payload);

    auto lines = result.split('\n');
    EXPECT_TRUE(lines[0].contains("\"status\":\"event\""));
    EXPECT_TRUE(lines[1].contains("\"progress\""));
}
