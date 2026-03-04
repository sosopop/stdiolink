#include <gtest/gtest.h>

#include "stdiolink/driver/example_auto_fill.h"
#include "stdiolink/protocol/meta_types.h"

namespace {

QJsonObject findExampleByMode(const QVector<QJsonObject>& examples, const QString& mode) {
    for (const auto& ex : examples) {
        if (ex.value("mode").toString() == mode) {
            return ex;
        }
    }
    return QJsonObject();
}

} // namespace

TEST(ExampleAutoFillTest, FillsMissingDescriptionAndAddsConsoleExample) {
    using namespace stdiolink::meta;

    DriverMeta meta;
    CommandMeta cmd;
    cmd.name = "run";
    cmd.description = "启动服务";
    cmd.examples.append(QJsonObject{
        {"mode", "stdio"},
        {"params", QJsonObject{{"port", 502}}},
    });
    meta.commands.append(cmd);

    ensureCommandExamples(meta, true);

    ASSERT_EQ(meta.commands.size(), 1);
    const auto& examples = meta.commands[0].examples;
    ASSERT_EQ(examples.size(), 2);

    const QJsonObject stdioEx = findExampleByMode(examples, "stdio");
    const QJsonObject consoleEx = findExampleByMode(examples, "console");
    ASSERT_FALSE(stdioEx.isEmpty());
    ASSERT_FALSE(consoleEx.isEmpty());

    EXPECT_EQ(stdioEx.value("description").toString(), QString("启动服务 示例（stdio）"));
    EXPECT_EQ(consoleEx.value("description").toString(), QString("启动服务 示例（console）"));
}

TEST(ExampleAutoFillTest, KeepsExistingDescriptionUnchanged) {
    using namespace stdiolink::meta;

    DriverMeta meta;
    CommandMeta cmd;
    cmd.name = "status";
    cmd.description = "读取状态";
    cmd.examples.append(QJsonObject{
        {"description", "手工说明"},
        {"mode", "stdio"},
        {"params", QJsonObject{}},
    });
    meta.commands.append(cmd);

    ensureCommandExamples(meta, true);

    const QJsonObject stdioEx = findExampleByMode(meta.commands[0].examples, "stdio");
    ASSERT_FALSE(stdioEx.isEmpty());
    EXPECT_EQ(stdioEx.value("description").toString(), QString("手工说明"));
}

TEST(ExampleAutoFillTest, UsesTitleWhenCommandDescriptionIsEmpty) {
    using namespace stdiolink::meta;

    DriverMeta meta;
    CommandMeta cmd;
    cmd.name = "status";
    cmd.title = "Get service status";
    cmd.examples.append(QJsonObject{
        {"mode", "stdio"},
        {"params", QJsonObject{}},
    });
    meta.commands.append(cmd);

    ensureCommandExamples(meta, false);

    const QJsonObject stdioEx = findExampleByMode(meta.commands[0].examples, "stdio");
    ASSERT_FALSE(stdioEx.isEmpty());
    EXPECT_EQ(stdioEx.value("description").toString(),
              QString("Get service status 示例（stdio）"));
}
