#include <gtest/gtest.h>

#include "stdiolink/driver/meta_builder.h"

using namespace stdiolink::meta;

// FieldBuilder 测试
class FieldBuilderTest : public ::testing::Test {};

TEST_F(FieldBuilderTest, BasicStringField) {
    auto field = FieldBuilder("name", FieldType::String)
                     .required()
                     .description("User name")
                     .build();

    EXPECT_EQ(field.name, "name");
    EXPECT_EQ(field.type, FieldType::String);
    EXPECT_TRUE(field.required);
    EXPECT_EQ(field.description, "User name");
}

TEST_F(FieldBuilderTest, IntFieldWithRange) {
    auto field = FieldBuilder("age", FieldType::Int)
                     .range(0, 150)
                     .defaultValue(18)
                     .build();

    EXPECT_EQ(field.name, "age");
    EXPECT_EQ(field.type, FieldType::Int);
    EXPECT_EQ(field.constraints.min, 0);
    EXPECT_EQ(field.constraints.max, 150);
    EXPECT_EQ(field.defaultValue.toInt(), 18);
}

TEST_F(FieldBuilderTest, StringFieldWithConstraints) {
    auto field = FieldBuilder("email", FieldType::String)
                     .minLength(5)
                     .maxLength(100)
                     .pattern(R"(^[\w.-]+@[\w.-]+\.\w+$)")
                     .format("email")
                     .build();

    EXPECT_EQ(field.constraints.minLength, 5);
    EXPECT_EQ(field.constraints.maxLength, 100);
    EXPECT_EQ(field.constraints.pattern, R"(^[\w.-]+@[\w.-]+\.\w+$)");
    EXPECT_EQ(field.constraints.format, "email");
}

TEST_F(FieldBuilderTest, EnumField) {
    auto field = FieldBuilder("status", FieldType::Enum)
                     .enumValues(QStringList{"active", "inactive", "pending"})
                     .defaultValue("active")
                     .build();

    EXPECT_EQ(field.type, FieldType::Enum);
    EXPECT_EQ(field.constraints.enumValues.size(), 3);
    EXPECT_EQ(field.constraints.enumValues[0].toString(), "active");
}

TEST_F(FieldBuilderTest, UIHints) {
    auto field = FieldBuilder("password", FieldType::String)
                     .widget("password")
                     .group("Security")
                     .order(1)
                     .placeholder("Enter password")
                     .advanced()
                     .build();

    EXPECT_EQ(field.ui.widget, "password");
    EXPECT_EQ(field.ui.group, "Security");
    EXPECT_EQ(field.ui.order, 1);
    EXPECT_EQ(field.ui.placeholder, "Enter password");
    EXPECT_TRUE(field.ui.advanced);
}

TEST_F(FieldBuilderTest, ObjectFieldWithNestedFields) {
    auto field = FieldBuilder("address", FieldType::Object)
                     .addField(FieldBuilder("street", FieldType::String).required())
                     .addField(FieldBuilder("city", FieldType::String).required())
                     .addField(FieldBuilder("zip", FieldType::String))
                     .requiredKeys({"street", "city"})
                     .additionalProperties(false)
                     .build();

    EXPECT_EQ(field.type, FieldType::Object);
    EXPECT_EQ(field.fields.size(), 3);
    EXPECT_EQ(field.fields[0].name, "street");
    EXPECT_EQ(field.requiredKeys, QStringList({"street", "city"}));
    EXPECT_FALSE(field.additionalProperties);
}

TEST_F(FieldBuilderTest, ArrayFieldWithItems) {
    auto field = FieldBuilder("tags", FieldType::Array)
                     .items(FieldBuilder("tag", FieldType::String).maxLength(50))
                     .minItems(1)
                     .maxItems(10)
                     .build();

    EXPECT_EQ(field.type, FieldType::Array);
    ASSERT_NE(field.items, nullptr);
    EXPECT_EQ(field.items->type, FieldType::String);
    EXPECT_EQ(field.items->constraints.maxLength, 50);
    EXPECT_EQ(field.constraints.minItems, 1);
    EXPECT_EQ(field.constraints.maxItems, 10);
}

// CommandBuilder 测试
class CommandBuilderTest : public ::testing::Test {};

TEST_F(CommandBuilderTest, BasicCommand) {
    auto cmd = CommandBuilder("echo")
                   .description("Echo back the message")
                   .title("Echo Command")
                   .summary("Simple echo")
                   .build();

    EXPECT_EQ(cmd.name, "echo");
    EXPECT_EQ(cmd.description, "Echo back the message");
    EXPECT_EQ(cmd.title, "Echo Command");
    EXPECT_EQ(cmd.summary, "Simple echo");
}

TEST_F(CommandBuilderTest, CommandWithParams) {
    auto cmd = CommandBuilder("greet")
                   .param(FieldBuilder("name", FieldType::String).required())
                   .param(FieldBuilder("times", FieldType::Int).defaultValue(1))
                   .build();

    EXPECT_EQ(cmd.params.size(), 2);
    EXPECT_EQ(cmd.params[0].name, "name");
    EXPECT_TRUE(cmd.params[0].required);
    EXPECT_EQ(cmd.params[1].name, "times");
    EXPECT_EQ(cmd.params[1].defaultValue.toInt(), 1);
}

TEST_F(CommandBuilderTest, CommandWithReturns) {
    auto cmd = CommandBuilder("calculate")
                   .returns(FieldType::Double, "Calculation result")
                   .build();

    EXPECT_EQ(cmd.returns.type, FieldType::Double);
    EXPECT_EQ(cmd.returns.description, "Calculation result");
}

TEST_F(CommandBuilderTest, CommandWithEvents) {
    auto cmd = CommandBuilder("download")
                   .event("progress", "Download progress update")
                   .event("complete", "Download completed")
                   .build();

    EXPECT_EQ(cmd.events.size(), 2);
    EXPECT_EQ(cmd.events[0].name, "progress");
    EXPECT_EQ(cmd.events[0].description, "Download progress update");
}

TEST_F(CommandBuilderTest, CommandWithUIHints) {
    auto cmd = CommandBuilder("settings")
                   .group("Configuration")
                   .order(5)
                   .build();

    EXPECT_EQ(cmd.ui.group, "Configuration");
    EXPECT_EQ(cmd.ui.order, 5);
}

// DriverMetaBuilder 测试
class DriverMetaBuilderTest : public ::testing::Test {};

TEST_F(DriverMetaBuilderTest, BasicDriverMeta) {
    auto meta = DriverMetaBuilder()
                    .schemaVersion("1.0")
                    .info("com.example.echo", "Echo Driver", "1.0.0", "A simple echo driver")
                    .vendor("Example Inc.")
                    .build();

    EXPECT_EQ(meta.schemaVersion, "1.0");
    EXPECT_EQ(meta.info.id, "com.example.echo");
    EXPECT_EQ(meta.info.name, "Echo Driver");
    EXPECT_EQ(meta.info.version, "1.0.0");
    EXPECT_EQ(meta.info.description, "A simple echo driver");
    EXPECT_EQ(meta.info.vendor, "Example Inc.");
}

TEST_F(DriverMetaBuilderTest, DriverMetaWithEntry) {
    auto meta = DriverMetaBuilder()
                    .info("test", "Test", "1.0")
                    .entry("test_driver.exe", {"--mode", "stdio"})
                    .build();

    EXPECT_EQ(meta.info.entry["program"].toString(), "test_driver.exe");
    auto args = meta.info.entry["defaultArgs"].toArray();
    EXPECT_EQ(args.size(), 2);
    EXPECT_EQ(args[0].toString(), "--mode");
}

TEST_F(DriverMetaBuilderTest, DriverMetaWithCapabilities) {
    auto meta = DriverMetaBuilder()
                    .info("test", "Test", "1.0")
                    .capability("streaming")
                    .capability("batch")
                    .profile("basic")
                    .profile("advanced")
                    .build();

    EXPECT_EQ(meta.info.capabilities, QStringList({"streaming", "batch"}));
    EXPECT_EQ(meta.info.profiles, QStringList({"basic", "advanced"}));
}

TEST_F(DriverMetaBuilderTest, DriverMetaWithConfig) {
    auto meta = DriverMetaBuilder()
                    .info("test", "Test", "1.0")
                    .configField(FieldBuilder("timeout", FieldType::Int)
                                     .defaultValue(30)
                                     .unit("seconds"))
                    .configField(FieldBuilder("verbose", FieldType::Bool)
                                     .defaultValue(false))
                    .configApply("command", "configure")
                    .build();

    EXPECT_EQ(meta.config.fields.size(), 2);
    EXPECT_EQ(meta.config.fields[0].name, "timeout");
    EXPECT_EQ(meta.config.fields[0].ui.unit, "seconds");
    EXPECT_EQ(meta.config.apply.method, "command");
    EXPECT_EQ(meta.config.apply.command, "configure");
}

TEST_F(DriverMetaBuilderTest, DriverMetaWithCommands) {
    auto meta = DriverMetaBuilder()
                    .info("test", "Test", "1.0")
                    .command(CommandBuilder("echo")
                                 .description("Echo message")
                                 .param(FieldBuilder("msg", FieldType::String).required()))
                    .command(CommandBuilder("ping")
                                 .description("Ping test"))
                    .build();

    EXPECT_EQ(meta.commands.size(), 2);
    EXPECT_EQ(meta.commands[0].name, "echo");
    EXPECT_EQ(meta.commands[0].params.size(), 1);
    EXPECT_EQ(meta.commands[1].name, "ping");
}

TEST_F(DriverMetaBuilderTest, CompleteDriverMeta) {
    auto meta = DriverMetaBuilder()
                    .schemaVersion("1.0")
                    .info("com.example.complete", "Complete Driver", "2.0.0",
                          "A complete example driver")
                    .vendor("Example Corp")
                    .entry("complete_driver", {"--stdio"})
                    .capability("streaming")
                    .capability("progress")
                    .profile("standard")
                    .configField(FieldBuilder("bufferSize", FieldType::Int)
                                     .range(1024, 65536)
                                     .defaultValue(4096))
                    .configApply("command", "setConfig")
                    .command(CommandBuilder("process")
                                 .description("Process data")
                                 .param(FieldBuilder("data", FieldType::String).required())
                                 .param(FieldBuilder("options", FieldType::Object)
                                            .addField(FieldBuilder("compress", FieldType::Bool)))
                                 .returns(FieldType::Object, "Processing result")
                                 .event("progress", "Progress update"))
                    .build();

    EXPECT_EQ(meta.schemaVersion, "1.0");
    EXPECT_EQ(meta.info.id, "com.example.complete");
    EXPECT_EQ(meta.info.capabilities.size(), 2);
    EXPECT_EQ(meta.config.fields.size(), 1);
    EXPECT_EQ(meta.commands.size(), 1);
    EXPECT_EQ(meta.commands[0].params.size(), 2);
    EXPECT_EQ(meta.commands[0].events.size(), 1);
}
