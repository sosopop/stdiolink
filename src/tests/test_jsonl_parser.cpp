#include <QJsonArray>
#include <QJsonObject>
#include <gtest/gtest.h>
#include "stdiolink/protocol/jsonl_serializer.h"

using namespace stdiolink;

// ============================================
// 请求解析测试
// ============================================

TEST(JsonlParser, ParseRequest_Valid) {
    Request req;
    bool ok = parseRequest(R"({"cmd":"scan","data":{"fps":10}})", req);

    EXPECT_TRUE(ok);
    EXPECT_EQ(req.cmd, "scan");
    EXPECT_TRUE(req.data.isObject());
    EXPECT_EQ(req.data.toObject()["fps"].toInt(), 10);
}

TEST(JsonlParser, ParseRequest_NoData) {
    Request req;
    bool ok = parseRequest(R"({"cmd":"info"})", req);

    EXPECT_TRUE(ok);
    EXPECT_EQ(req.cmd, "info");
    EXPECT_TRUE(req.data.isUndefined());
}

TEST(JsonlParser, ParseRequest_InvalidJson) {
    Request req;
    bool ok = parseRequest("not json", req);
    EXPECT_FALSE(ok);
}

TEST(JsonlParser, ParseRequest_MissingCmd) {
    Request req;
    bool ok = parseRequest(R"({"data":{"fps":10}})", req);
    EXPECT_FALSE(ok);
}

TEST(JsonlParser, ParseRequest_EmptyObject) {
    Request req;
    bool ok = parseRequest(R"({})", req);
    EXPECT_FALSE(ok);
}

// ============================================
// 响应头解析测试
// ============================================

TEST(JsonlParser, ParseHeader_Event) {
    FrameHeader hdr;
    bool ok = parseHeader(R"({"status":"event","code":0})", hdr);

    EXPECT_TRUE(ok);
    EXPECT_EQ(hdr.status, "event");
    EXPECT_EQ(hdr.code, 0);
}

TEST(JsonlParser, ParseHeader_Done) {
    FrameHeader hdr;
    bool ok = parseHeader(R"({"status":"done","code":0})", hdr);

    EXPECT_TRUE(ok);
    EXPECT_EQ(hdr.status, "done");
}

TEST(JsonlParser, ParseHeader_Error) {
    FrameHeader hdr;
    bool ok = parseHeader(R"({"status":"error","code":1007})", hdr);

    EXPECT_TRUE(ok);
    EXPECT_EQ(hdr.status, "error");
    EXPECT_EQ(hdr.code, 1007);
}

TEST(JsonlParser, ParseHeader_InvalidStatus) {
    FrameHeader hdr;
    bool ok = parseHeader(R"({"status":"unknown","code":0})", hdr);
    EXPECT_FALSE(ok);
}

TEST(JsonlParser, ParseHeader_MissingStatus) {
    FrameHeader hdr;
    bool ok = parseHeader(R"({"code":0})", hdr);
    EXPECT_FALSE(ok);
}

TEST(JsonlParser, ParseHeader_MissingCode) {
    FrameHeader hdr;
    bool ok = parseHeader(R"({"status":"done"})", hdr);
    EXPECT_FALSE(ok);
}

// ============================================
// Payload 解析测试
// ============================================

TEST(JsonlParser, ParsePayload_Object) {
    auto val = parsePayload(R"({"result":42})");
    EXPECT_TRUE(val.isObject());
    EXPECT_EQ(val.toObject()["result"].toInt(), 42);
}

TEST(JsonlParser, ParsePayload_Array) {
    auto val = parsePayload(R"([1,2,3])");
    EXPECT_TRUE(val.isArray());
    EXPECT_EQ(val.toArray().size(), 3);
}

TEST(JsonlParser, ParsePayload_Number) {
    auto val = parsePayload("42");
    EXPECT_TRUE(val.isDouble());
    EXPECT_EQ(val.toInt(), 42);
}

TEST(JsonlParser, ParsePayload_Bool) {
    EXPECT_EQ(parsePayload("true").toBool(), true);
    EXPECT_EQ(parsePayload("false").toBool(), false);
}

TEST(JsonlParser, ParsePayload_Null) {
    auto val = parsePayload("null");
    EXPECT_TRUE(val.isNull());
}

TEST(JsonlParser, ParsePayload_String) {
    auto val = parsePayload("hello world");
    EXPECT_TRUE(val.isString());
    EXPECT_EQ(val.toString(), "hello world");
}

// ============================================
// 响应解析测试 (单行格式)
// ============================================

TEST(JsonlParser, ParseResponse_Done) {
    Message msg;
    bool ok = parseResponse(R"({"status":"done","code":0,"data":{"result":42}})", msg);

    EXPECT_TRUE(ok);
    EXPECT_EQ(msg.status, "done");
    EXPECT_EQ(msg.code, 0);
    EXPECT_EQ(msg.payload.toObject()["result"].toInt(), 42);
}

TEST(JsonlParser, ParseResponse_Error) {
    Message msg;
    bool ok = parseResponse(R"({"status":"error","code":1007,"data":{"message":"fail"}})", msg);

    EXPECT_TRUE(ok);
    EXPECT_EQ(msg.status, "error");
    EXPECT_EQ(msg.code, 1007);
}

TEST(JsonlParser, ParseResponse_Event) {
    Message msg;
    bool ok = parseResponse(R"({"status":"event","code":0,"data":{"progress":0.5}})", msg);

    EXPECT_TRUE(ok);
    EXPECT_EQ(msg.status, "event");
}

TEST(JsonlParser, ParseResponse_InvalidStatus) {
    Message msg;
    bool ok = parseResponse(R"({"status":"unknown","code":0,"data":{}})", msg);
    EXPECT_FALSE(ok);
}

TEST(JsonlParser, ParseResponse_MissingStatus) {
    Message msg;
    bool ok = parseResponse(R"({"code":0,"data":{}})", msg);
    EXPECT_FALSE(ok);
}
