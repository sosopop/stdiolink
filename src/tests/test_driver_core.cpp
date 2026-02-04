#include <QJsonObject>
#include <gtest/gtest.h>
#include "stdiolink/driver/icommand_handler.h"
#include "stdiolink/driver/mock_responder.h"

using namespace stdiolink;

// 测试用命令处理器
class EchoHandler : public ICommandHandler {
public:
    void handle(const QString& cmd, const QJsonValue& data, IResponder& r) override {
        if (cmd == "echo") {
            r.done(0, data);
        } else if (cmd == "progress") {
            int steps = data.toObject()["steps"].toInt(3);
            for (int i = 1; i <= steps; ++i) {
                r.event(0, QJsonObject{{"step", i}});
            }
            r.done(0, QJsonObject{{"total", steps}});
        } else {
            r.error(404, QJsonObject{{"message", "unknown command"}});
        }
    }
};

// ============================================
// MockResponder 测试
// ============================================

TEST(MockResponder, RecordEvent) {
    MockResponder r;
    r.event(0, QJsonObject{{"progress", 0.5}});

    ASSERT_EQ(r.responses.size(), 1);
    EXPECT_EQ(r.responses[0].status, "event");
    EXPECT_EQ(r.responses[0].code, 0);
}

TEST(MockResponder, RecordDone) {
    MockResponder r;
    r.done(0, QJsonObject{{"result", 42}});

    ASSERT_EQ(r.responses.size(), 1);
    EXPECT_EQ(r.responses[0].status, "done");
}

TEST(MockResponder, RecordError) {
    MockResponder r;
    r.error(1007, QJsonObject{{"message", "failed"}});

    ASSERT_EQ(r.responses.size(), 1);
    EXPECT_EQ(r.responses[0].status, "error");
    EXPECT_EQ(r.responses[0].code, 1007);
}

// ============================================
// EchoHandler 测试
// ============================================

TEST(EchoHandler, EchoCommand) {
    EchoHandler handler;
    MockResponder r;

    handler.handle("echo", QJsonObject{{"msg", "hello"}}, r);

    ASSERT_EQ(r.responses.size(), 1);
    EXPECT_EQ(r.responses[0].status, "done");
    EXPECT_EQ(r.responses[0].code, 0);
}

TEST(EchoHandler, UnknownCommand) {
    EchoHandler handler;
    MockResponder r;

    handler.handle("unknown", QJsonObject{}, r);

    ASSERT_EQ(r.responses.size(), 1);
    EXPECT_EQ(r.responses[0].status, "error");
    EXPECT_EQ(r.responses[0].code, 404);
}

TEST(EchoHandler, ProgressCommand) {
    EchoHandler handler;
    MockResponder r;

    handler.handle("progress", QJsonObject{{"steps", 3}}, r);

    // 3 个 event + 1 个 done
    ASSERT_EQ(r.responses.size(), 4);
    EXPECT_EQ(r.responses[0].status, "event");
    EXPECT_EQ(r.responses[1].status, "event");
    EXPECT_EQ(r.responses[2].status, "event");
    EXPECT_EQ(r.responses[3].status, "done");
}
