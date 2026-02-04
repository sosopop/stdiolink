#include <QCoreApplication>
#include <QJsonObject>
#include <gtest/gtest.h>
#include "stdiolink/host/driver.h"
#include "stdiolink/host/task.h"

using namespace stdiolink;

// 测试 Driver 程序路径
#ifdef _WIN32
static const QString TestDriver = "test_driver.exe";
#else
static const QString TestDriver = "./test_driver";
#endif

// ============================================
// Task 基础测试
// ============================================

TEST(Task, InvalidTask) {
    Task t;
    EXPECT_FALSE(t.isValid());
    EXPECT_TRUE(t.isDone());
    EXPECT_EQ(t.exitCode(), -1);
}

TEST(Task, TryNextEmpty) {
    auto state = std::make_shared<TaskState>();
    Task t(nullptr, state);

    Message msg;
    EXPECT_FALSE(t.tryNext(msg));
}

TEST(Task, TryNextWithMessage) {
    auto state = std::make_shared<TaskState>();
    state->queue.push_back(Message{"done", 0, QJsonObject{{"result", 42}}});
    Task t(nullptr, state);

    Message msg;
    EXPECT_TRUE(t.tryNext(msg));
    EXPECT_EQ(msg.status, "done");
}

TEST(Task, TryNextMultipleMessages) {
    auto state = std::make_shared<TaskState>();
    state->queue.push_back(Message{"event", 0, QJsonObject{{"n", 1}}});
    state->queue.push_back(Message{"event", 0, QJsonObject{{"n", 2}}});
    state->queue.push_back(Message{"done", 0, QJsonObject{}});
    Task t(nullptr, state);

    Message msg;
    EXPECT_TRUE(t.tryNext(msg));
    EXPECT_EQ(msg.payload.toObject()["n"].toInt(), 1);

    EXPECT_TRUE(t.tryNext(msg));
    EXPECT_EQ(msg.payload.toObject()["n"].toInt(), 2);

    EXPECT_TRUE(t.tryNext(msg));
    EXPECT_EQ(msg.status, "done");

    EXPECT_FALSE(t.tryNext(msg));
}

TEST(Task, IsDoneStates) {
    auto state = std::make_shared<TaskState>();
    Task t(nullptr, state);

    // 未终止，队列空
    EXPECT_FALSE(t.isDone());

    // 终止，队列有消息
    state->terminal = true;
    state->queue.push_back(Message{"done", 0, QJsonObject{}});
    EXPECT_FALSE(t.isDone());

    // 终止，队列空
    Message msg;
    t.tryNext(msg);
    EXPECT_TRUE(t.isDone());
}

TEST(Task, ExitCodeSuccess) {
    auto state = std::make_shared<TaskState>();
    state->terminal = true;
    state->exitCode = 0;
    Task t(nullptr, state);

    EXPECT_EQ(t.exitCode(), 0);
}

TEST(Task, ExitCodeError) {
    auto state = std::make_shared<TaskState>();
    state->terminal = true;
    state->exitCode = 1007;
    state->errorText = "invalid input";
    Task t(nullptr, state);

    EXPECT_EQ(t.exitCode(), 1007);
    EXPECT_EQ(t.errorText(), "invalid input");
}

TEST(Task, WaitNextAlreadyDone) {
    auto state = std::make_shared<TaskState>();
    state->terminal = true;
    Task t(nullptr, state);

    Message msg;
    EXPECT_FALSE(t.waitNext(msg, 1000));
}

// ============================================
// Driver 集成测试（需要 test_driver）
// ============================================

class DriverIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 获取测试程序路径
        m_driverPath = QCoreApplication::applicationDirPath() + "/" + TestDriver;
    }

    QString m_driverPath;
};

TEST_F(DriverIntegrationTest, EchoCommand) {
    Driver d;
    ASSERT_TRUE(d.start(m_driverPath));

    Task t = d.request("echo", QJsonObject{{"msg", "hello"}});
    EXPECT_TRUE(t.isValid());

    Message msg;
    EXPECT_TRUE(t.waitNext(msg, 5000));
    EXPECT_EQ(msg.status, "done");

    d.terminate();
}

TEST_F(DriverIntegrationTest, ProgressCommand) {
    Driver d;
    ASSERT_TRUE(d.start(m_driverPath));

    Task t = d.request("progress", QJsonObject{{"steps", 3}});

    int eventCount = 0;
    Message msg;
    while (t.waitNext(msg, 5000)) {
        if (msg.status == "event")
            eventCount++;
        if (msg.status == "done")
            break;
    }

    EXPECT_EQ(eventCount, 3);
    EXPECT_TRUE(t.isDone());

    d.terminate();
}

TEST_F(DriverIntegrationTest, UnknownCommand) {
    Driver d;
    ASSERT_TRUE(d.start(m_driverPath));

    Task t = d.request("unknown");

    Message msg;
    EXPECT_TRUE(t.waitNext(msg, 5000));
    EXPECT_EQ(msg.status, "error");
    EXPECT_EQ(msg.code, 404);

    d.terminate();
}

TEST_F(DriverIntegrationTest, MultipleEvents) {
    Driver d;
    ASSERT_TRUE(d.start(m_driverPath));

    Task t = d.request("progress", QJsonObject{{"steps", 3}});

    std::vector<Message> messages;
    Message msg;
    while (t.waitNext(msg, 5000)) {
        messages.push_back(msg);
        if (msg.status == "done" || msg.status == "error")
            break;
    }

    // 3 个 event + 1 个 done
    EXPECT_EQ(messages.size(), 4);
    EXPECT_EQ(messages.back().status, "done");

    d.terminate();
}
