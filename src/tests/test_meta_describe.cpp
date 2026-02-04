#include <QJsonArray>
#include <QJsonObject>
#include <gtest/gtest.h>
#include "stdiolink/driver/meta_command_handler.h"
#include "stdiolink/driver/mock_responder.h"
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink;
using namespace stdiolink::meta;

// 测试用 MetaHandler
class TestMetaHandler : public IMetaCommandHandler {
public:
    const DriverMeta& driverMeta() const override {
        return m_meta;
    }

    void handle(const QString& cmd, const QJsonValue& data,
                IResponder& resp) override {
        if (cmd == "echo") {
            resp.done(0, data);
        } else {
            resp.error(404, QJsonObject{{"message", "Unknown command"}});
        }
    }

    void setMeta(const DriverMeta& meta) { m_meta = meta; }

private:
    DriverMeta m_meta;
};

// ============================================
// IMetaCommandHandler 测试
// ============================================

TEST(MetaDescribe, MetaHandlerReturnsMeta) {
    TestMetaHandler handler;

    DriverMeta meta;
    meta.schemaVersion = "1.0";
    meta.info.id = "test.meta.driver";
    meta.info.name = "Test Meta Driver";
    meta.info.version = "1.0.0";

    CommandMeta cmd;
    cmd.name = "echo";
    cmd.description = "Echo input";
    meta.commands.append(cmd);

    handler.setMeta(meta);

    const auto& result = handler.driverMeta();
    EXPECT_EQ(result.schemaVersion, "1.0");
    EXPECT_EQ(result.info.id, "test.meta.driver");
    EXPECT_EQ(result.commands.size(), 1);
}

TEST(MetaDescribe, MetadataJsonFormat) {
    TestMetaHandler handler;

    DriverMeta meta;
    meta.schemaVersion = "1.0";
    meta.info.id = "test.driver";
    meta.info.name = "Test";
    meta.info.version = "1.0.0";
    handler.setMeta(meta);

    QJsonObject json = handler.driverMeta().toJson();

    EXPECT_EQ(json["schemaVersion"].toString(), "1.0");
    EXPECT_TRUE(json.contains("info"));
    EXPECT_EQ(json["info"].toObject()["id"].toString(), "test.driver");
}

TEST(MetaDescribe, AutoValidateParamsDefault) {
    TestMetaHandler handler;
    EXPECT_TRUE(handler.autoValidateParams());
}

TEST(MetaDescribe, NormalCommandStillWorks) {
    TestMetaHandler handler;
    MockResponder responder;

    handler.handle("echo", QJsonObject{{"msg", "hello"}}, responder);

    ASSERT_EQ(responder.responses.size(), 1);
    EXPECT_EQ(responder.responses[0].status, "done");
    EXPECT_EQ(responder.responses[0].code, 0);
}
