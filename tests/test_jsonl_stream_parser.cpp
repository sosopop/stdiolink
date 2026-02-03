#include <gtest/gtest.h>
#include "protocol/jsonl_parser.h"

using namespace stdiolink;

// ============================================
// 流式解析器测试
// ============================================

TEST(JsonlStreamParser, SingleLine)
{
    JsonlParser parser;
    parser.append("{\"cmd\":\"test\"}\n");

    QByteArray line;
    EXPECT_TRUE(parser.tryReadLine(line));
    EXPECT_EQ(line, "{\"cmd\":\"test\"}");
}

TEST(JsonlStreamParser, MultipleLines)
{
    JsonlParser parser;
    parser.append("{\"line\":1}\n{\"line\":2}\n");

    QByteArray line;
    EXPECT_TRUE(parser.tryReadLine(line));
    EXPECT_EQ(line, "{\"line\":1}");

    EXPECT_TRUE(parser.tryReadLine(line));
    EXPECT_EQ(line, "{\"line\":2}");

    EXPECT_FALSE(parser.tryReadLine(line));
}

TEST(JsonlStreamParser, PartialLine)
{
    JsonlParser parser;
    parser.append("{\"cmd\":");

    QByteArray line;
    EXPECT_FALSE(parser.tryReadLine(line));

    parser.append("\"test\"}\n");
    EXPECT_TRUE(parser.tryReadLine(line));
    EXPECT_EQ(line, "{\"cmd\":\"test\"}");
}

TEST(JsonlStreamParser, EmptyLine)
{
    JsonlParser parser;
    parser.append("\n");

    QByteArray line;
    EXPECT_TRUE(parser.tryReadLine(line));
    EXPECT_TRUE(line.isEmpty());
}

TEST(JsonlStreamParser, MultiplePartialAppends)
{
    JsonlParser parser;
    parser.append("{");
    parser.append("\"a\"");
    parser.append(":1}");
    parser.append("\n");

    QByteArray line;
    EXPECT_TRUE(parser.tryReadLine(line));
    EXPECT_EQ(line, "{\"a\":1}");
}

TEST(JsonlStreamParser, Clear)
{
    JsonlParser parser;
    parser.append("{\"cmd\":\"test\"}\n");
    parser.clear();

    QByteArray line;
    EXPECT_FALSE(parser.tryReadLine(line));
    EXPECT_EQ(parser.bufferSize(), 0);
}

TEST(JsonlStreamParser, BufferSize)
{
    JsonlParser parser;
    EXPECT_EQ(parser.bufferSize(), 0);

    parser.append("hello");
    EXPECT_EQ(parser.bufferSize(), 5);

    parser.append(" world\n");
    EXPECT_EQ(parser.bufferSize(), 12);

    QByteArray line;
    parser.tryReadLine(line);
    EXPECT_EQ(parser.bufferSize(), 0);
}
