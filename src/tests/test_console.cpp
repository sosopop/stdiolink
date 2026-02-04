#include <QJsonArray>
#include <gtest/gtest.h>
#include "stdiolink/console/console_args.h"

using namespace stdiolink;

// ============================================
// 类型推断测试
// ============================================

TEST(InferType, Bool) {
    EXPECT_EQ(inferType("true"), QJsonValue(true));
    EXPECT_EQ(inferType("false"), QJsonValue(false));
}

TEST(InferType, Null) {
    EXPECT_TRUE(inferType("null").isNull());
}

TEST(InferType, Integer) {
    EXPECT_EQ(inferType("42"), QJsonValue(42));
    EXPECT_EQ(inferType("-10"), QJsonValue(-10));
    EXPECT_EQ(inferType("0"), QJsonValue(0));
}

TEST(InferType, Double) {
    EXPECT_EQ(inferType("3.14"), QJsonValue(3.14));
    EXPECT_EQ(inferType("-2.5"), QJsonValue(-2.5));
}

TEST(InferType, JsonObject) {
    auto val = inferType("{\"x\":1,\"y\":2}");
    EXPECT_TRUE(val.isObject());
    EXPECT_EQ(val.toObject()["x"].toInt(), 1);
    EXPECT_EQ(val.toObject()["y"].toInt(), 2);
}

TEST(InferType, JsonArray) {
    auto val = inferType("[1,2,3]");
    EXPECT_TRUE(val.isArray());
    EXPECT_EQ(val.toArray().size(), 3);
}

TEST(InferType, String) {
    EXPECT_EQ(inferType("hello"), QJsonValue("hello"));
    EXPECT_EQ(inferType("123abc"), QJsonValue("123abc"));
    EXPECT_EQ(inferType(""), QJsonValue(""));
}

TEST(InferType, InvalidJson) {
    // 无效 JSON 应被当作字符串
    EXPECT_EQ(inferType("{invalid}"), QJsonValue("{invalid}"));
    EXPECT_EQ(inferType("[1,2,"), QJsonValue("[1,2,"));
}

// ============================================
// 嵌套路径测试
// ============================================

TEST(SetNestedValue, Simple) {
    QJsonObject obj;
    setNestedValue(obj, "key", 42);
    EXPECT_EQ(obj["key"].toInt(), 42);
}

TEST(SetNestedValue, Nested) {
    QJsonObject obj;
    setNestedValue(obj, "a.b.c", 100);
    EXPECT_EQ(obj["a"].toObject()["b"].toObject()["c"].toInt(), 100);
}

TEST(SetNestedValue, MultipleNested) {
    QJsonObject obj;
    setNestedValue(obj, "roi.x", 10);
    setNestedValue(obj, "roi.y", 20);

    auto roi = obj["roi"].toObject();
    EXPECT_EQ(roi["x"].toInt(), 10);
    EXPECT_EQ(roi["y"].toInt(), 20);
}

// ============================================
// ConsoleArgs 参数解析测试
// ============================================

TEST(ConsoleArgs, ParseCmd) {
    const char* argv[] = {"prog", "--cmd=scan"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(2, const_cast<char**>(argv)));
    EXPECT_EQ(args.cmd, "scan");
}

TEST(ConsoleArgs, ParseMode) {
    const char* argv[] = {"prog", "--mode=console", "--profile=oneshot", "--cmd=test"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(4, const_cast<char**>(argv)));
    EXPECT_EQ(args.mode, "console");
    EXPECT_EQ(args.profile, "oneshot");
}

TEST(ConsoleArgs, ParseHelp) {
    const char* argv[] = {"prog", "--help"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(2, const_cast<char**>(argv)));
    EXPECT_TRUE(args.showHelp);
}

TEST(ConsoleArgs, ParseVersion) {
    const char* argv[] = {"prog", "--version"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(2, const_cast<char**>(argv)));
    EXPECT_TRUE(args.showVersion);
}

// ============================================
// Data 参数测试
// ============================================

TEST(ConsoleArgs, DataSimple) {
    const char* argv[] = {"prog", "--cmd=scan", "--fps=10"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(3, const_cast<char**>(argv)));
    EXPECT_EQ(args.data["fps"].toInt(), 10);
}

TEST(ConsoleArgs, DataMultiple) {
    const char* argv[] = {"prog", "--cmd=scan", "--fps=10", "--enable=true", "--name=test"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(5, const_cast<char**>(argv)));
    EXPECT_EQ(args.data["fps"].toInt(), 10);
    EXPECT_EQ(args.data["enable"].toBool(), true);
    EXPECT_EQ(args.data["name"].toString(), "test");
}

TEST(ConsoleArgs, DataNested) {
    const char* argv[] = {"prog", "--cmd=scan", "--roi.x=10", "--roi.y=20"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(4, const_cast<char**>(argv)));

    auto roi = args.data["roi"].toObject();
    EXPECT_EQ(roi["x"].toInt(), 10);
    EXPECT_EQ(roi["y"].toInt(), 20);
}

TEST(ConsoleArgs, DataArgPrefix) {
    // --arg-mode 用于避免与 --mode 冲突
    const char* argv[] = {"prog", "--cmd=scan", "--mode=console", "--arg-mode=frame"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(4, const_cast<char**>(argv)));
    EXPECT_EQ(args.mode, "console");
    EXPECT_EQ(args.data["mode"].toString(), "frame");
}

// ============================================
// 边界情况测试
// ============================================

TEST(ConsoleArgs, EmptyData) {
    const char* argv[] = {"prog", "--cmd=info"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(2, const_cast<char**>(argv)));
    EXPECT_TRUE(args.data.isEmpty());
}

TEST(ConsoleArgs, InvalidJson) {
    const char* argv[] = {"prog", "--cmd=test", "--obj={invalid}"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(3, const_cast<char**>(argv)));
    // 无效 JSON 应被当作字符串
    EXPECT_EQ(args.data["obj"].toString(), "{invalid}");
}

TEST(ConsoleArgs, MissingCmd) {
    const char* argv[] = {"prog", "--mode=console"};
    ConsoleArgs args;
    EXPECT_FALSE(args.parse(2, const_cast<char**>(argv)));
    EXPECT_FALSE(args.errorMessage.isEmpty());
}

TEST(ConsoleArgs, InvalidArgument) {
    const char* argv[] = {"prog", "invalid"};
    ConsoleArgs args;
    EXPECT_FALSE(args.parse(2, const_cast<char**>(argv)));
}

// ============================================
// M12: 双模式检测测试
// ============================================

TEST(DualMode, StdioModeByDefault) {
    // 无参数时应允许（默认 stdio 模式）
    const char* argv[] = {"prog"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(1, const_cast<char**>(argv)));
    EXPECT_TRUE(args.cmd.isEmpty());
    EXPECT_TRUE(args.mode.isEmpty());
}

TEST(DualMode, ExplicitStdioMode) {
    // 显式 --mode=stdio 不需要 --cmd
    const char* argv[] = {"prog", "--mode=stdio"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(2, const_cast<char**>(argv)));
    EXPECT_EQ(args.mode, "stdio");
}

TEST(DualMode, ExplicitConsoleMode) {
    // 显式 --mode=console 需要 --cmd
    const char* argv[] = {"prog", "--mode=console", "--cmd=scan"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(3, const_cast<char**>(argv)));
    EXPECT_EQ(args.mode, "console");
    EXPECT_EQ(args.cmd, "scan");
}

TEST(DualMode, ConsoleModeWithDataRequiresCmd) {
    // 有数据参数但没有 --cmd 应失败
    const char* argv[] = {"prog", "--fps=30"};
    ConsoleArgs args;
    EXPECT_FALSE(args.parse(2, const_cast<char**>(argv)));
    EXPECT_FALSE(args.errorMessage.isEmpty());
}

TEST(DualMode, HelpFlagNoCmd) {
    // --help 不需要 --cmd
    const char* argv[] = {"prog", "--help"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(2, const_cast<char**>(argv)));
    EXPECT_TRUE(args.showHelp);
}

TEST(DualMode, VersionFlagNoCmd) {
    // --version 不需要 --cmd
    const char* argv[] = {"prog", "--version"};
    ConsoleArgs args;
    EXPECT_TRUE(args.parse(2, const_cast<char**>(argv)));
    EXPECT_TRUE(args.showVersion);
}

TEST(DualMode, IsInteractiveStdinExists) {
    // 验证 isInteractiveStdin 方法存在且可调用
    // 注意：实际返回值取决于测试运行环境
    bool result = ConsoleArgs::isInteractiveStdin();
    // 在管道/重定向环境下通常返回 false
    Q_UNUSED(result);
}
